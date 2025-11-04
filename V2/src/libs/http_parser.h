/* protocol_http/parser scaffold */
#ifndef v2_src_protocol_http_http_parser_h
#define v2_src_protocol_http_http_parser_h

#include <stddef.h>

/* Very small parser API for scaffold. Real parser belongs in libs/http_parser.
 * These functions are placeholders to show the integration points.
 */

typedef struct protocol_http_request_t protocol_http_request_t;
typedef struct protocol_http_response_t protocol_http_response_t;

protocol_http_request_t *protocol_http_parser_parse(const char *data,
                                                    size_t len);
void protocol_http_response_send(int client_fd, protocol_http_response_t *resp);

#endif /* v2_src_protocol_http_http_parser_h */
