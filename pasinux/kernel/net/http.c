#include "http.h"
#include "net_tcp.h"

#include "serial.h"
#include "timer.h"

#include <stdint.h>

/* Internal strlen for freestanding */
static uint16_t http_strlen(const char* s) {
    uint16_t n = 0u;
    while (s && s[n]) ++n;
    return n;
}

/* Internal u32-to-string */
static void http_u32_to_str(uint32_t val, char* buf, uint16_t* pos) {
    char tmp[12];
    uint16_t i = 0u;
    if (val == 0u) {
        tmp[i++] = '0';
    } else {
        while (val > 0u && i < 11u) {
            tmp[i++] = (char)('0' + (val % 10u));
            val /= 10u;
        }
    }
    while (i > 0u) {
        buf[*pos] = tmp[--i];
        (*pos)++;
    }
    buf[*pos] = '\0';
}

/*
 * Build an HTTP request over TCP.
 * host can be an IP address in string form (e.g. "10.0.2.2") or hostname.
 *
 * The function:
 * 1. Resolves the host IP from the string
 * 2. TCP connects
 * 3. Sends the HTTP request
 * 4. Reads the response
 * 5. Returns the response body
 */

static uint32_t parse_ip(const char* host) {
    uint32_t ip = 0u;
    uint32_t octet = 0u;
    int shift = 24;
    for (uint16_t i = 0u; host[i] != '\0'; ++i) {
        char c = host[i];
        if (c >= '0' && c <= '9') {
            octet = octet * 10u + (uint32_t)(c - '0');
        } else if (c == '.') {
            ip |= (octet << shift);
            shift -= 8;
            octet = 0u;
        } else {
            return 0u;  /* invalid character */
        }
    }
    ip |= (octet << shift);
    return ip;
}

int http_request(const char* method, const char* host,
                 const char* path, const char* headers, const char* body,
                 char* response, uint16_t response_size) {
    if (!method || !host || !path || !response || response_size == 0u) {
        return -1;
    }

    uint32_t remote_ip = parse_ip(host);
    if (remote_ip == 0u) {
        serial_puts("[HTTP] invalid host IP\n");
        return -1;
    }

    /* Build the HTTP request */
    char request_buf[512];
    uint16_t pos = 0u;
    uint16_t mi, hi, pi, bi;

    /* Method + path */
    for (mi = 0u; method[mi]; ++mi) request_buf[pos++] = method[mi];
    request_buf[pos++] = ' ';
    for (pi = 0u; path[pi]; ++pi) request_buf[pos++] = path[pi];
    request_buf[pos++] = ' '; request_buf[pos++] = 'H'; request_buf[pos++] = 'T';
    request_buf[pos++] = 'T'; request_buf[pos++] = 'P'; request_buf[pos++] = '/';
    request_buf[pos++] = '1'; request_buf[pos++] = '.'; request_buf[pos++] = '1';
    request_buf[pos++] = '\r'; request_buf[pos++] = '\n';

    /* Host header */
    request_buf[pos++] = 'H'; request_buf[pos++] = 'o'; request_buf[pos++] = 's';
    request_buf[pos++] = 't'; request_buf[pos++] = ':'; request_buf[pos++] = ' ';
    for (hi = 0u; host[hi]; ++hi) request_buf[pos++] = host[hi];
    request_buf[pos++] = '\r'; request_buf[pos++] = '\n';

    /* Extra headers */
    if (headers) {
        for (hi = 0u; headers[hi]; ++hi) request_buf[pos++] = headers[hi];
    }

    /* Content-Length if body provided */
    uint16_t body_len = body ? http_strlen(body) : 0u;
    if (body_len > 0u) {
        request_buf[pos++] = 'C'; request_buf[pos++] = 'o'; request_buf[pos++] = 'n';
        request_buf[pos++] = 't'; request_buf[pos++] = 'e'; request_buf[pos++] = 'n';
        request_buf[pos++] = 't'; request_buf[pos++] = '-'; request_buf[pos++] = 'L';
        request_buf[pos++] = 'e'; request_buf[pos++] = 'n'; request_buf[pos++] = 'g';
        request_buf[pos++] = 't'; request_buf[pos++] = 'h'; request_buf[pos++] = ':';
        request_buf[pos++] = ' ';
        http_u32_to_str(body_len, request_buf, &pos);
        request_buf[pos++] = '\r'; request_buf[pos++] = '\n';
    }

    /* End of headers */
    request_buf[pos++] = '\r'; request_buf[pos++] = '\n';

    /* Body */
    if (body_len > 0u) {
        for (bi = 0u; body[bi]; ++bi) request_buf[pos++] = body[bi];
    }
    request_buf[pos] = '\0';

    serial_puts("[HTTP] request:\n");
    serial_puts(request_buf);
    serial_puts("\n");

    /* Connect to server */
    tcp_conn_t* conn = tcp_get_conn();
    if (tcp_connect(remote_ip, 80) != 0) {
        serial_puts("[HTTP] connect failed\n");
        return -1;
    }

    /* Send request */
    int sent = tcp_send(conn, (const uint8_t*)request_buf, pos);
    if (sent < 0 || (uint16_t)sent != pos) {
        serial_puts("[HTTP] send failed\n");
        tcp_close(conn);
        return -1;
    }
    serial_puts("[HTTP] request sent, receiving...\n");

    /* Receive response (up to response_size) */
    uint16_t total = 0u;
    uint16_t timeout = 100u;  /* ~1 second at 100Hz */
    uint32_t start = timer_ticks();

    while (total < response_size - 1u && (timer_ticks() - start) < timeout) {
        uint8_t chunk[128];
        int n = tcp_recv(conn, chunk, sizeof(chunk) - 1u);
        if (n > 0) {
            for (int ci = 0; ci < n && total < response_size - 1u; ++ci) {
                response[total++] = (char)chunk[ci];
            }
            start = timer_ticks();  /* reset timeout on new data */
        }
    }
    response[total] = '\0';

    /* Close connection */
    tcp_close(conn);

    serial_puts("[HTTP] received ");
    serial_put_u32(total);
    serial_puts(" bytes\n");

    /* Find start of body (after \r\n\r\n) */
    uint16_t body_start = 0u;
    for (uint16_t ii = 0u; ii + 3u < total; ++ii) {
        if (response[ii] == '\r' && response[ii+1] == '\n' &&
            response[ii+2] == '\r' && response[ii+3] == '\n') {
            body_start = ii + 4u;
            break;
        }
    }

    if (body_start > 0u) {
        /* Shift body to beginning of response buffer */
        uint16_t body_len_out = total - body_start;
        for (uint16_t jj = 0u; jj < body_len_out && jj < response_size - 1u; ++jj) {
            response[jj] = response[body_start + jj];
        }
        response[body_len_out] = '\0';
        serial_puts("[HTTP] response body:\n");
        serial_puts(response);
        serial_puts("\n");
    } else {
        serial_puts("[HTTP] response headers only:\n");
        serial_puts(response);
        serial_puts("\n");
    }

    return 0;
}

static void hdr_append(char* hdr, uint16_t* len, uint16_t cap,
                       const char* data, uint16_t n) {
    for (uint16_t i = 0u; i < n && *len < (uint16_t)(cap - 1u); ++i) {
        hdr[(*len)++] = data[i];
    }
    hdr[*len] = '\0';
}

static int hdr_ends(const char* hdr, uint16_t len) {
    if (len < 4u) return 0;
    return hdr[len - 4u] == '\r' && hdr[len - 3u] == '\n' &&
           hdr[len - 2u] == '\r' && hdr[len - 1u] == '\n';
}

static long parse_content_length(const char* hdr) {
    static const char key[] = "Content-Length:";
    for (uint16_t i = 0u; hdr[i]; ++i) {
        uint16_t j = 0u;
        while (key[j] && hdr[i + j] == key[j]) ++j;
        if (key[j] == '\0') {
            long val = 0;
            uint16_t k = i + j;
            while (hdr[k] == ' ' || hdr[k] == '\t') ++k;
            if (hdr[k] < '0' || hdr[k] > '9') return -1;
            while (hdr[k] >= '0' && hdr[k] <= '9') {
                val = val * 10L + (long)(hdr[k] - '0');
                ++k;
            }
            return val;
        }
    }
    return -1;
}

int http_download(const char* host, const char* path,
                  uint8_t* out, uint32_t cap, uint32_t* out_len) {
    *out_len = 0u;
    if (!host || !path || !out || cap == 0u) return -1;

    uint32_t remote_ip = parse_ip(host);
    if (remote_ip == 0u) {
        serial_puts("[HTTP] invalid host IP\n");
        return -1;
    }

    char req[512];
    uint16_t pos = 0u;
    static const char pfx[] = "GET ";
    static const char mid[] = " HTTP/1.1\r\nHost: ";
    static const char sfx[] = "\r\nConnection: close\r\n\r\n";

    for (uint16_t i = 0u; pfx[i]; ++i) req[pos++] = pfx[i];
    for (uint16_t i = 0u; path[i] && pos < sizeof(req) - 64u; ++i) req[pos++] = path[i];
    for (uint16_t i = 0u; mid[i]; ++i) req[pos++] = mid[i];
    for (uint16_t i = 0u; host[i] && pos < sizeof(req) - 48u; ++i) req[pos++] = host[i];
    for (uint16_t i = 0u; sfx[i]; ++i) req[pos++] = sfx[i];

    tcp_conn_t* conn = tcp_get_conn();
    if (tcp_connect(remote_ip, 80) != 0) {
        serial_puts("[HTTP] connect failed\n");
        return -1;
    }

    int sent = tcp_send(conn, (const uint8_t*)req, pos);
    if (sent < 0 || (uint16_t)sent != pos) {
        serial_puts("[HTTP] send failed\n");
        tcp_close(conn);
        return -1;
    }

    char hdr[1024];
    uint16_t hdr_len = 0u;
    int have_hdr = 0;
    long content_len = -1;
    uint32_t total = 0u;
    uint32_t start = timer_ticks();
    const uint32_t idle_timeout = 300u;

    while ((timer_ticks() - start) < idle_timeout) {
        uint8_t chunk[256];
        int n = tcp_recv(conn, chunk, (uint16_t)sizeof(chunk));
        if (n <= 0) continue;
        start = timer_ticks();

        if (!have_hdr) {
            hdr_append(hdr, &hdr_len, (uint16_t)sizeof(hdr),
                       (const char*)chunk, (uint16_t)n);
            if (hdr_ends(hdr, hdr_len)) {
                have_hdr = 1;
                content_len = parse_content_length(hdr);
                uint16_t body_at = 0u;
                for (uint16_t i = 0u; i + 3u < hdr_len; ++i) {
                    if (hdr[i] == '\r' && hdr[i + 1u] == '\n' &&
                        hdr[i + 2u] == '\r' && hdr[i + 3u] == '\n') {
                        body_at = (uint16_t)(i + 4u);
                        break;
                    }
                }
                for (uint16_t i = body_at; i < hdr_len && total < cap; ++i) {
                    out[total++] = (uint8_t)hdr[i];
                }
            }
        } else {
            for (int i = 0; i < n && total < cap; ++i) {
                out[total++] = chunk[i];
            }
        }

        if (content_len >= 0 && total >= (uint32_t)content_len) break;
        if (total >= cap) break;
    }

    tcp_close(conn);

    *out_len = total;
    serial_puts("[HTTP] downloaded ");
    serial_put_u32(total);
    serial_puts(" bytes\n");
    return 0;
}