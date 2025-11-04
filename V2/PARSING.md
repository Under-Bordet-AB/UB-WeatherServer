# HTTP parsing guide — state-machine approach

This document describes a robust, incremental HTTP request parser implemented as a state machine suitable for a cooperative (yielding) scheduler. The parser works on a per-connection buffer and handles partial reads, pipelining, Content-Length and chunked bodies, and keep-alive/pipelined requests.

Goals
- Parse requests incrementally from a single growable buffer.
- Never block: return when more bytes are needed.
- Support pipelined requests: leave unread bytes in the buffer and continue parsing.
- Be resilient: enforce sensible limits (header size, number of headers, body size).

Concepts and data model
- Each connection/session owns a byte buffer (`req_buf`, `req_len`, `req_cap`).
- Parser state is an enum (see example below). The session state machine calls the parser step.
- Parser returns one of:
  - consumed bytes / request ready
  - need more bytes (wait for readable)
  - error (malformed request / too large)
- When a full request is parsed, the app is notified (callback) with a pointer/length into the buffer or with a copied payload.

Parser states (recommended)
- `parse_state_read_headers` — scanning for CRLF CRLF (end of headers)
- `parse_state_parse_request_line` — parse method / path / version
- `parse_state_parse_headers` — parse header lines into table
- `parse_state_determine_body` — decide on body handling:
  - no body (GET, HEAD, certain status)
  - content-length -> read exact bytes
  - chunked -> enter chunked-substate
- `parse_state_read_body_content_length` — read N bytes
- `parse_state_chunk_size` — read next chunk-size line
- `parse_state_chunk_data` — read chunk bytes and trailing CRLF
- `parse_state_chunk_trailer` — read optional trailers and final CRLF
- `parse_state_done` — request ready to hand to application
- `parse_state_error` — fatal parse error

Buffer management rules
- Use a single incoming buffer: append new data at `req_buf + req_len`.
- Always operate on available bytes only: `req_len`.
- When a request completes, shift remaining bytes to the front:
  - memmove(req_buf, req_buf + consumed, req_len - consumed); req_len -= consumed;
- Avoid excessive copying; for app callback you can pass (req_buf, request_length) and leave it to app to copy if needed.

Header parsing algorithm (incremental)
1. Search for the header terminator sequence `"\r\n\r\n"`. If not found, return NEED_MORE_DATA.
2. Once found, split into request-line and header-lines.
3. Parse request-line into method, path, version (validate tokens and length).
4. Parse headers line-by-line: split at `\r\n` and each at the first `:`. Trim OWS.
5. Enforce limits: max header size, max header count, max name/value length.
6. After parsing headers, determine body handling.

Body handling
- Content-Length:
  - Read exactly N bytes. If `req_len - headers_end < N`, return NEED_MORE_DATA.
  - When N bytes available, pass the body with the request and advance buffer by headers_end+N.
- Chunked (Transfer-Encoding: chunked):
  - Repeatedly:
    - Read chunk-size line (hex digits) ending with CRLF.
    - Parse size S. If not all bytes available yet, return NEED_MORE_DATA.
    - Read S bytes of chunk-data plus trailing CRLF. If incomplete, return NEED_MORE_DATA.
    - If S == 0: then read optional trailers (headers) until CRLF CRLF; then request is complete.
- No body: request complete after headers.

Partial reads and resumable parsing
- Never assume a read returns a full header/body. On EAGAIN/EWOULDBLOCK, update `wakeup_ms` or register the FD with the event poller and yield.
- The parser function should be reentrant: keep parse-state and any temporary counters in the session structure.
- Example flow:
  - Read bytes -> append to buffer -> call `http_parser_step(sess)`.
  - `http_parser_step` advances state and returns NEED_MORE_DATA or REQUEST_READY.
  - If NEED_MORE_DATA: scheduler waits for fd readable.
  - If REQUEST_READY: call app callback; the app prepares response and session returns to reading state (for pipelined request).

Pipelining & keep-alive
- After request complete, don't discard unread bytes. Use memmove to compact buffer, then loop to parse the next request if bytes remain.
- Respect `Connection: close` or HTTP/1.0 without `Connection: keep-alive`: schedule close after response.
- For pipelined requests, ensure `session_step` can re-enter parse loop immediately (return 0 to reschedule immediately).

Error handling & limits
- Set limits to avoid abuse:
  - max header block size (e.g., 64 KiB)
  - max single header length (e.g., 4 KiB)
  - max number of headers (e.g., 512)
  - max request body size (configurable)
- On parse error, prepare a 4xx response and move to closing state.
- Avoid half-constructed state leaks: free parsed header storage on error.

Integration with the session state machine
- Map parser outcomes to session states:
  - `session_state_reading` — call `read_from_socket()` until parser says NEED_MORE_DATA or returns request ready.
  - `session_state_parsing` — run `http_parser_step()`; it may consume bytes and either produce a request or set WAIT.
  - `session_state_handling` — call app callback and prepare response into `resp_buf`.
  - `session_state_writing` — non-blocking write response.
- The parser itself can be implemented as `int http_parser_step(session_t *sess)` which returns:
  - `>= 0` : bytes consumed or 0 meaning more work possible;
  - negative : error; or a specific code meaning REQUEST_READY.
- Prefer explicit status codes: e.g. enum { HTTP_PARSER_NEED_MORE = 1, HTTP_PARSER_REQUEST_READY = 2, HTTP_PARSER_ERROR = -1 }.

Example parser-state skeleton (C pseudo-code)
```c
// filepath: V2/src/protocol_http/parsing_guide.md (example snippet)
typedef enum {
  parse_state_read_headers,
  parse_state_read_body_content_length,
  parse_state_chunk_size,
  parse_state_chunk_data,
  parse_state_chunk_trailer,
  parse_state_done,
  parse_state_error
} http_parse_state_t;

/* per-session parser context (store inside session_t) */
typedef struct {
  http_parse_state_t state;
  size_t headers_end;      // index of CRLFCRLF if found
  size_t body_expected;    // for content-length
  size_t chunk_size_remaining;
  // add header table, counters, etc.
} http_parser_ctx_t;

/* Step */
int http_parser_step(session_t *sess, http_parser_ctx_t *p) {
  switch (p->state) {
  case parse_state_read_headers:
    if (!find_double_crlf(sess->req_buf, sess->req_len, &p->headers_end))
      return NEED_MORE_DATA;
    // parse request-line & headers; set p->body_expected or chunked
    if (p->body_expected > 0) {
      p->state = parse_state_read_body_content_length;
      return 0;
    }
    if (chunked) {
      p->state = parse_state_chunk_size;
      return 0;
    }
    p->state = parse_state_done;
    return REQUEST_READY;

  case parse_state_read_body_content_length:
    if (sess->req_len - p->headers_end < p->body_expected)
      return NEED_MORE_DATA;
    // body available -> REQUEST_READY
    p->state = parse_state_done;
    return REQUEST_READY;

  case parse_state_chunk_size:
    // read chunk-size line, parse hex size
    // if not complete -> NEED_MORE_DATA
    // if size == 0 -> p->state = parse_state_chunk_trailer
    // else p->chunk_size_remaining = size; p->state = parse_state_chunk_data
    return 0;

  case parse_state_chunk_data:
    // ensure chunk_size_remaining bytes + CRLF present; else NEED_MORE_DATA
    // consume and loop back to chunk_size
    return 0;

  case parse_state_chunk_trailer:
    // wait for CRLFCRLF, then REQUEST_READY
    return 0;

  default:
    return HTTP_PARSER_ERROR;
  }
}