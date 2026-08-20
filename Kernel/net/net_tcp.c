#include "net_tcp.h"
#include "net_ip.h"
#include "net_eth.h"

#include "rtl8139.h"
#include "serial.h"
#include "timer.h"

#include <stdint.h>

static tcp_conn_t g_tcp_conn;

tcp_conn_t* tcp_get_conn(void) {
    return &g_tcp_conn;
}

/* Poll NIC for one incoming packet and dispatch it */
static void net_poll_once(void) {
    rtl8139_t* nic = rtl8139_get();
    if (!nic || nic->io_base == 0u) return;

    uint8_t buf[ETH_HLEN + ETH_MTU];
    uint16_t len = rtl8139_poll(nic, buf, sizeof(buf));
    if (len > 0u) {
        eth_dispatch(buf, len);
    }
}

void tcp_init(void) {
    g_tcp_conn.state = TCP_CLOSED;
    g_tcp_conn.seq_num = 1000u;  /* arbitrary initial seq */
    g_tcp_conn.ack_num = 0u;
    g_tcp_conn.recv_len = 0u;
    g_tcp_conn.connected = 0;
    g_tcp_conn.waiting_for_ack = 0;
    g_tcp_conn.ack_received = 0;
}

static uint16_t tcp_checksum(const tcp_pseudo_t* pseudo, const uint8_t* tcp_seg,
                             uint16_t seg_len) {
    uint32_t sum = 0u;

    /* Sum pseudo-header (6 half-words). Read via memcpy instead of
     * reinterpreting a packed struct pointer (alignment 1) as uint16_t*
     * (alignment 2) — that cast both risks an unaligned access and trips
     * -Werror=address-of-packed-member. */
    const uint8_t* pb = (const uint8_t*)pseudo;
    for (uint32_t i = 0u; i < 6u; ++i) {
        uint16_t word;
        __builtin_memcpy(&word, pb + (i * 2u), sizeof(word));
        sum += word;
    }

    /* Sum TCP segment. Same memcpy approach — tcp_seg was never
     * misaligned itself, but this keeps both loops consistent and drops
     * the may_alias typedef in favor of the more standard memcpy idiom. */
    uint32_t words = (uint32_t)((seg_len + 1u) / 2u);  /* round up to half-words */
    for (uint32_t i = 0u; i < words; ++i) {
        uint16_t word;
        __builtin_memcpy(&word, tcp_seg + (i * 2u), sizeof(word));
        sum += word;
    }

    /* Fold 32-bit sum to 16 bits */
    while (sum >> 16u) {
        sum = (sum & 0xFFFFu) + (sum >> 16u);
    }

    return (uint16_t)(~sum & 0xFFFFu);
}

static void tcp_send_segment(tcp_conn_t* conn, uint8_t flags,
                             const uint8_t* payload, uint16_t payload_len) {
    uint16_t seg_len = (uint16_t)(TCP_HLEN + payload_len);
    if (seg_len > (uint16_t)(TCP_HLEN + TCP_MSS)) return;

    uint8_t buf[TCP_HLEN + TCP_MSS];
    tcp_header_t* tcp = (tcp_header_t*)buf;

    tcp->src_port    = __builtin_bswap16(conn->local_port);
    tcp->dst_port    = __builtin_bswap16(conn->remote_port);
    tcp->seq_num     = __builtin_bswap32(conn->seq_num);
    tcp->ack_num     = __builtin_bswap32(conn->ack_num);
    tcp->data_offset = (uint8_t)(TCP_HLEN_WORDS << 4);  /* top 4 bits */
    tcp->flags       = flags;
    tcp->window      = __builtin_bswap16(TCP_DEFAULT_WINDOW);
    tcp->checksum    = 0u;
    tcp->urgent_ptr  = 0u;

    /* Copy payload after header */
    if (payload_len > 0u && payload) {
        typedef uint32_t __attribute__((__may_alias__)) u32_alias;
        const uint32_t* s = (const uint32_t*)payload;
        uint32_t* d = (uint32_t*)(buf + TCP_HLEN);
        uint32_t words = (uint32_t)((payload_len + 3u) / 4u);
        for (uint32_t i = 0u; i < words; ++i) {
            d[i] = ((const u32_alias*)s)[i];
        }
    }

    /* Build pseudo-header for checksum */
    tcp_pseudo_t pseudo;
    pseudo.src_ip    = __builtin_bswap32(conn->local_ip);
    pseudo.dst_ip    = __builtin_bswap32(conn->remote_ip);
    pseudo.zero      = 0u;
    pseudo.protocol  = IP_PROTO_TCP;
    pseudo.tcp_length = __builtin_bswap16(seg_len);

    tcp->checksum = tcp_checksum(&pseudo, buf, seg_len);

    /* Send as IP payload */
    ip_send(conn->remote_ip, IP_PROTO_TCP, buf, seg_len);

    /* Update sequence number for SYN/FIN/data */
    if (flags & TCP_SYN) {
        conn->seq_num++;
    }
    if (payload_len > 0u) {
        conn->seq_num += payload_len;
    }
    if (flags & TCP_FIN) {
        conn->seq_num++;
    }
}

int tcp_connect(uint32_t remote_ip, uint16_t remote_port) {
    if (g_tcp_conn.state != TCP_CLOSED) return -1;

    g_tcp_conn.local_ip    = OUR_IP_ADDR;
    g_tcp_conn.remote_ip   = remote_ip;
    g_tcp_conn.local_port  = 12345u;
    g_tcp_conn.remote_port = remote_port;
    g_tcp_conn.state       = TCP_SYN_SENT;
    g_tcp_conn.retries     = 0u;
    g_tcp_conn.waiting_for_ack = 1;
    g_tcp_conn.ack_received    = 0;

    serial_puts("[TCP] connect ");
    serial_put_u32((remote_ip >> 24) & 0xFFu);
    serial_puts(".");
    serial_put_u32((remote_ip >> 16) & 0xFFu);
    serial_puts(".");
    serial_put_u32((remote_ip >> 8) & 0xFFu);
    serial_puts(".");
    serial_put_u32(remote_ip & 0xFFu);
    serial_puts(":");
    serial_put_u32(remote_port);
    serial_puts("\n");

    while (g_tcp_conn.state != TCP_ESTABLISHED && g_tcp_conn.retries < TCP_MAX_RETRIES) {
        tcp_send_segment(&g_tcp_conn, TCP_SYN, (const uint8_t*)0, 0u);

        /* Poll for SYN-ACK */
        uint32_t start = timer_ticks();
        while ((timer_ticks() - start) < TCP_RTO_TICKS) {
            net_poll_once();
            if (g_tcp_conn.state == TCP_ESTABLISHED) {
                serial_puts("[TCP] connected\n");
                g_tcp_conn.connected = 1;
                return 0;
            }
            if (g_tcp_conn.state == TCP_CLOSED) {
                serial_puts("[TCP] connection refused (RST)\n");
                return -1;
            }
        }

        g_tcp_conn.retries++;
        serial_puts("[TCP] SYN timeout, retry ");
        serial_put_u32(g_tcp_conn.retries);
        serial_puts("\n");
    }

    serial_puts("[TCP] connect failed\n");
    g_tcp_conn.state = TCP_CLOSED;
    return -1;
}

int tcp_send(tcp_conn_t* conn, const uint8_t* data, uint16_t len) {
    if (!conn || conn->state != TCP_ESTABLISHED) {
        serial_puts("[TCP] send: not connected\n");
        return -1;
    }

    uint16_t sent = 0u;
    while (sent < len) {
        uint16_t chunk = (uint16_t)(len - sent);
        if (chunk > TCP_MSS) chunk = TCP_MSS;

        uint32_t expected_ack = conn->seq_num + (uint32_t)chunk;
        int acked = 0;

        for (uint16_t retry = 0u; retry < TCP_MAX_RETRIES && !acked; ++retry) {
            tcp_send_segment(conn, TCP_PSH | TCP_ACK, data + sent, chunk);

            uint32_t start = timer_ticks();
            while ((timer_ticks() - start) < TCP_RTO_TICKS) {
                net_poll_once();
                if (conn->state != TCP_ESTABLISHED) {
                    serial_puts("[TCP] connection lost during send\n");
                    return -1;
                }
                /* Check if our data has been acknowledged */
                /* (The ack_num in ACK packets is set by tcp_handle_packet) */
                if (conn->ack_num >= expected_ack) {
                    acked = 1;
                    break;
                }
            }
        }

        if (!acked) {
            serial_puts("[TCP] send timeout\n");
            return -1;
        }

        sent = (uint16_t)(sent + chunk);
        if (chunk < TCP_MSS) break;
    }

    return (int)sent;
}

int tcp_recv(tcp_conn_t* conn, uint8_t* buf, uint16_t len) {
    if (!conn || !buf || len == 0u) return 0;

    /* Wait for data with polling */
    uint32_t start = timer_ticks();
    while (conn->recv_len == 0u) {
        net_poll_once();
        if (conn->state != TCP_ESTABLISHED && conn->state != TCP_FIN_WAIT_1) {
            if (conn->recv_len == 0u) return -1;
            break;
        }
        /* Timeout after ~2s */
        if ((timer_ticks() - start) > 200u) {
            return 0;  /* no data within timeout */
        }
    }

    uint16_t copy_len = conn->recv_len;
    if (copy_len > len) copy_len = (uint16_t)len;

    typedef uint32_t __attribute__((__may_alias__)) u32_alias;
    uint32_t* s = (uint32_t*)conn->recv_buf;
    uint32_t* d = (uint32_t*)buf;
    uint32_t words = (uint32_t)((copy_len + 3u) / 4u);
    for (uint32_t i = 0u; i < words; ++i) {
        d[i] = ((u32_alias*)s)[i];
    }

    conn->recv_len = 0u;
    return (int)copy_len;
}

void tcp_close(tcp_conn_t* conn) {
    if (!conn || conn->state != TCP_ESTABLISHED) return;

    conn->state = TCP_FIN_WAIT_1;
    tcp_send_segment(conn, TCP_FIN | TCP_ACK, (const uint8_t*)0, 0u);

    /* Wait for FIN-ACK */
    uint32_t start = timer_ticks();
    while ((timer_ticks() - start) < TCP_RTO_TICKS) {
        net_poll_once();
        if (conn->state == TCP_TIME_WAIT || conn->state == TCP_CLOSED) {
            break;
        }
    }

    conn->state = TCP_CLOSED;
    conn->connected = 0;
    serial_puts("[TCP] connection closed\n");
}

void tcp_handle_packet(const uint8_t* data, uint16_t len, uint32_t src_ip) {
    if (!data || len < TCP_HLEN) return;

    const tcp_header_t* tcp = (const tcp_header_t*)data;
    uint16_t src_port = __builtin_bswap16(tcp->src_port);
    uint16_t dst_port = __builtin_bswap16(tcp->dst_port);

    /* Only handle packets for our connection */
    if (src_ip != g_tcp_conn.remote_ip ||
        src_port != g_tcp_conn.remote_port ||
        dst_port != g_tcp_conn.local_port) {
        return;
    }

    uint8_t flags  = tcp->flags;
    uint32_t seq   = __builtin_bswap32(tcp->seq_num);
    uint32_t ack   = __builtin_bswap32(tcp->ack_num);
    uint16_t hdr_words = (uint16_t)((tcp->data_offset >> 4) & 0x0Fu);
    uint16_t hdr_len   = (uint16_t)(hdr_words * 4u);
    if (hdr_len > len) return;
    const uint8_t* payload = data + hdr_len;
    uint16_t payload_len = (uint16_t)(len - hdr_len);

    switch (g_tcp_conn.state) {
    case TCP_SYN_SENT:
        if ((flags & TCP_SYN_ACK) == TCP_SYN_ACK) {
            /* Valid SYN-ACK received */
            g_tcp_conn.ack_num = seq + 1u;
            g_tcp_conn.seq_num = ack;

            /* Send ACK to complete handshake */
            tcp_send_segment(&g_tcp_conn, TCP_ACK, (const uint8_t*)0, 0u);

            g_tcp_conn.state = TCP_ESTABLISHED;
            serial_puts("[TCP] SYN-ACK received, established\n");
        } else if (flags & TCP_RST) {
            g_tcp_conn.state = TCP_CLOSED;
            serial_puts("[TCP] RST received\n");
        }
        break;

    case TCP_ESTABLISHED:
        if (flags & TCP_ACK) {
            g_tcp_conn.ack_num = ack;
        }

        if (payload_len > 0u) {
            /* Buffer received data */
            uint16_t copy_len = payload_len;
            if (copy_len > TCP_RECV_BUF_SIZE) {
                copy_len = TCP_RECV_BUF_SIZE;
            }
            typedef uint32_t __attribute__((__may_alias__)) u32_alias;
            const uint32_t* s = (const uint32_t*)payload;
            uint32_t* d = (uint32_t*)g_tcp_conn.recv_buf;
            uint32_t words = (uint32_t)((copy_len + 3u) / 4u);
            for (uint32_t i = 0u; i < words; ++i) {
                d[i] = ((const u32_alias*)s)[i];
            }
            g_tcp_conn.recv_len = copy_len;
            g_tcp_conn.ack_num = seq + payload_len;

            /* Send ACK for received data */
            tcp_send_segment(&g_tcp_conn, TCP_ACK, (const uint8_t*)0, 0u);
        }

        if (flags & TCP_FIN) {
            g_tcp_conn.ack_num = seq + 1u;
            tcp_send_segment(&g_tcp_conn, TCP_ACK | TCP_FIN, (const uint8_t*)0, 0u);
            g_tcp_conn.state = TCP_TIME_WAIT;
            serial_puts("[TCP] FIN received, sent FIN-ACK\n");
        }
        break;

    case TCP_FIN_WAIT_1:
        if (flags & TCP_ACK) {
            g_tcp_conn.ack_num = ack;
        }
        if (flags & TCP_FIN) {
            g_tcp_conn.ack_num = seq + 1u;
            tcp_send_segment(&g_tcp_conn, TCP_ACK, (const uint8_t*)0, 0u);
            g_tcp_conn.state = TCP_TIME_WAIT;
        } else {
            g_tcp_conn.state = TCP_CLOSED;
        }
        break;

    case TCP_TIME_WAIT:
        g_tcp_conn.state = TCP_CLOSED;
        g_tcp_conn.connected = 0;
        break;

    default:
        break;
    }
}
