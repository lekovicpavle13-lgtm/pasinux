#ifndef HTTP_H
#define HTTP_H

#include <stdint.h>

#define HTTP_RESP_BUF_SIZE 2048u

/* Perform an HTTP/1.1 request.
 * Returns 0 on success, -1 on failure.
 * response will contain the HTTP response body (null-terminated).
 */
int http_request(const char* method, const char* host,
                 const char* path, const char* headers, const char* body,
                 char* response, uint16_t response_size);

/* Download a URL body into out (binary-safe).
 * Stops on Content-Length, connection close, or ~3s idle.
 * Returns 0 on success and sets *out_len to body byte count.
 */
int http_download(const char* host, const char* path,
                  uint8_t* out, uint32_t cap, uint32_t* out_len);

#endif /* HTTP_H */