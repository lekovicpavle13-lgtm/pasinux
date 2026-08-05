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