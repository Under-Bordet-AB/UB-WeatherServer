# Common short variable names for C 
## Looping & iteration

| Variable | Meaning |
|---|---|
| `i` | primary loop counter |
| `j` | secondary loop counter |
| `k` | tertiary loop counter |
| `ii` | alternate loop counter |
| `jj` | alternate secondary loop counter |
| `kk` | alternate tertiary loop counter |
| `n` | iteration count / number of items |
| `m` | secondary count or dimension |
| `t` | temporary or time step |
| `step` | loop step size |
| `limit` | loop or array boundary |
| `iter` | iteration counter |
| `cnt` | count of items |
| `idx` | array index |
| `idx1` | first index |
| `idx2` | second index |
| `pos` | position in buffer or file |
| `cur` | current index or pointer |
| `next` | next index or pointer |
| `prev` | previous index or pointer |
| `start` | start index or pointer |
| `end` | end index or pointer |
| `mid` | midpoint index |
| `row` | row index |
| `col` | column index |
| `lhs` | left-hand side index/value |
| `rhs` | right-hand side index/value |

## Pointers & references

| Variable | Meaning |
|---|---|
| `p` | generic pointer |
| `q` | secondary pointer |
| `r` | result pointer |
| `ptr` | generic pointer variable |
| `ptr1` | first pointer |
| `ptr2` | second pointer |
| `base` | base pointer |
| `head` | head of list/array |
| `tail` | tail pointer |
| `it` | iterator pointer |
| `ref` | reference pointer |
| `node` | pointer to struct node |
| `elem` | pointer to array element |
| `self` | pointer to current object (OOP-like C) |
| `next_ptr` | pointer to next element |
| `prev_ptr` | pointer to previous element |

## Memory, buffers & blocks

| Variable | Meaning |
|---|---|
| `buf` | general buffer |
| `buf1` | first buffer |
| `buf2` | second buffer |
| `blk` | memory block |
| `blk1` | first block |
| `blk2` | second block |
| `mem` | memory pointer |
| `mem_end` | end of memory region |
| `mem_avail` | memory available |
| `data` | generic data pointer |
| `data1` | first data chunk |
| `data2` | second data chunk |
| `dst` | destination buffer/pointer |
| `src` | source buffer/pointer |
| `tmp` | generic temporary value |
| `tmp1` | temporary #1 |
| `tmp2` | temporary #2 |
| `tmp3` | temporary #3 |
| `off` | offset into buffer |
| `ofs` | alternative offset name |
| `cap` | capacity of buffer |
| `len` | length of buffer/string |
| `rem` | remainder / remaining bytes |
| `freep` | free pointer |
| `pool` | memory pool reference |
| `chunk` | chunk buffer pointer |
| `chunklen` | chunk length |

## Numeric values, math & range

| Variable | Meaning |
|---|---|
| `x` | x-coordinate / scalar |
| `y` | y-coordinate / scalar |
| `z` | z-coordinate / depth |
| `w` | width |
| `h` | height |
| `d` | delta / double |
| `dx` | delta x |
| `dy` | delta y |
| `dz` | delta z |
| `low` | lower bound |
| `high` | upper bound |
| `min` | minimum value |
| `max` | maximum value |
| `avg` | average |
| `sum` | running sum |
| `prod` | product accumulator |
| `val` | generic value |
| `val1` | first value |
| `val2` | second value |
| `num` | numeric value |
| `den` | denominator |
| `rng` | range struct/index |
| `mag` | magnitude |
| `norm` | normalized value |
| `absval` | absolute value |
| `rate` | rate or speed |
| `freq` | frequency |

## Flags, states & modes

| Variable | Meaning |
|---|---|
| `b` | boolean-like flag |
| `flg` | generic flag |
| `bit` | bit index |
| `msk` | bitmask |
| `ok` | success flag |
| `done` | completion flag |
| `valid` | validation flag |
| `dirty` | change-tracking flag |
| `alive` | life-state flag |
| `dead` | dead-state flag |
| `enabled` | enabled/disabled state |
| `active` | active state |
| `mode` | operating mode |
| `state` | overall state machine variable |
| `st` | short "state" identifier |
| `lvl` | level indicator |
| `pri` | priority |
| `warn` | warning flag |
| `dbg` | debug flag |

## Characters, strings & text

| Variable | Meaning |
|---|---|
| `c` | character |
| `ch` | character (explicit) |
| `s` | C-string pointer |
| `str` | string pointer |
| `tok` | token value (lexer) |
| `sym` | symbol identifier |
| `name` | name string |
| `line` | text line buffer |
| `word` | tokenized word |
| `bufstr` | string buffer |
| `fmt` | printf/format string |
| `msg` | message buffer/pointer |
| `ctype` | content-type string |
| `path` | path string |
| `url` | URL string |
| `query` | query string |

## Indexing, offsets & segments

| Variable | Meaning |
|---|---|
| `seg` | memory/file segment |
| `sect` | section index |
| `page` | memory/file page index |
| `ptr_off` | pointer offset |
| `base_off` | base offset |
| `abs_off` | absolute offset |
| `dim` | dimension |
| `layer` | drawing or logic layer |
| `slot` | table/list slot |
| `blk_idx` | block index |

## Files, I/O & streams

| Variable | Meaning |
|---|---|
| `fp` | `FILE*` pointer |
| `fd` | file descriptor |
| `fpos` | file position |
| `fsize` | file size |
| `stream` | stream handle |
| `in` | input buffer / stream |
| `out` | output buffer / stream |
| `log` | logging handle/buffer |

## Time, timing & events

| Variable | Meaning |
|---|---|
| `ts` | timestamp |
| `dt` | delta time |
| `t0` | start time |
| `t1` | end time |
| `tick` | tick counter |
| `clock` | clock value |
| `timeout` | timeout duration |
| `delay` | millisecond delay |
| `evt` | event code |
| `ev` | event struct |

## Identifiers & keys

| Variable | Meaning |
|---|---|
| `id` | generic identifier |
| `id1` | primary id |
| `id2` | secondary id |
| `uid` | unique id |
| `gid` | group id |
| `pid` | process id |
| `tid` | thread id |
| `key` | lookup/hash key |
| `tag` | label/tag value |
| `hash` | hash value |
| `sig` | signal number or signature |
| `seq` | sequence number |

## Networking, HTTP & messages

| Variable | Meaning |
|---|---|
| `ip` | IP address |
| `ip4` | IPv4 address |
| `ip6` | IPv6 address |
| `port` | TCP/UDP port |
| `addr` | socket address pointer/string |
| `sock` | socket descriptor |
| `conn` | connection pointer |
| `cli` | client pointer/descriptor |
| `srv` | server pointer/descriptor |
| `pkt` | packet pointer |
| `pktlen` | packet length |
| `hdr` | packet/header pointer |
| `hdrlen` | header length |
| `body` | message body pointer |
| `bodylen` | message body length |
| `payload` | payload pointer |
| `payloadlen` | payload length |
| `msgid` | message identifier |
| `ack` | acknowledgement flag/number |
| `req` | request struct/pointer |
| `resp` | response struct/pointer |
| `reqbuf` | HTTP request buffer |
| `reqbuflen` | HTTP request buffer length |
| `respbuf` | HTTP response buffer |
| `respbuflen` | HTTP response buffer length |
| `header` | HTTP header pointer |
| `headerlen` | header length |
| `content` | content/body pointer |
| `contentlen` | content length |
| `method` | HTTP method string |
| `status` | HTTP status code |
| `statusmsg` | HTTP status message |
| `keepalive` | connection keepalive flag |
| `tls` | TLS/SSL context pointer |
| `ssl` | SSL handle |
| `sent` | bytes sent counter |
| `recv` | bytes received counter |
| `avail` | bytes available to read |

## Geometry & graphics

| Variable | Meaning |
|---|---|
| `pt` | point structure |
| `vec` | vector structure |
| `ang` | angle value |
| `rad` | radians |
| `deg` | degrees |
| `colr` | color value |
| `uv` | texture coordinates |
| `pix` | pixel value |
| `frame` | frame index |
| `layer` | drawing layer |

## Containers, structures & context

| Variable | Meaning |
|---|---|
| `ctx` | context pointer |
| `env` | environment struct |
| `cfg` | config pointer |
| `opt` | option variable |
| `map` | map/dictionary handle |
| `set` | set handle |
| `arr` | array pointer |
| `lst` | list pointer |
| `tbl` | table pointer |
| `rec` | record struct |
| `obj` | object pointer |
| `elm` | element in collection |

## Miscellaneous & utility

| Variable | Meaning |
|---|---|
| `arg` | argument placeholder |
| `argc` | argument count |
| `argv` | argument vector |
| `res` | result code/value |
| `ret` | return value holder |
| `tmpv` | temporary value |
| `ver` | version number |
| `rev` | revision counter |
| `mod` | module id or modulus |
| `stat` | status value |
| `info` | info struct pointer |
| `seed` | random seed |
| `rnd` | random value |

---

If you want, I can also produce a condensed visual cheat-sheet (per category) or a single-column list for copy-pasting into editors that don't show tables well.
```
