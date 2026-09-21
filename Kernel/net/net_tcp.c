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

/* Poll NIC for one incoming packet and dispatch it. */
static void net_poll_once(void) {
    rtl8139_t* nic = rtl8139_get();

    if (!nic || nic->io_base == 0u) {
        return;
    }

    uint8_t buf[ETH_HLEN + ETH_MTU];

    uint16_t len =
        rtl8139_poll(nic, buf, sizeof(buf));

    if (len > 0u) {
        eth_dispatch(buf, len);
    }
}

void tcp_init(void) {
    g_tcp_conn.state = TCP_CLOSED;
    g_tcp_conn.seq_num = 1000u;
    g_tcp_conn.ack_num = 0u;
    g_tcp_conn.recv_len = 0u;
    g_tcp_conn.rto_start_tick = 0u;
    g_tcp_conn.retries = 0u;
    g_tcp_conn.connected = 0;
    g_tcp_conn.waiting_for_ack = 0;
    g_tcp_conn.ack_received = 0;
}

/*
 * TCP checksum over the pseudo-header and TCP segment.
 *
 * The checksum is calculated from the actual network-order bytes so
 * odd-length segments are padded correctly without reading past the
 * supplied buffer.
 */
static uint16_t tcp_checksum(const tcp_pseudo_t* pseudo,
                             const uint8_t* tcp_seg,
                             uint16_t seg_len) {
    if (!pseudo || (!tcp_seg && seg_len != 0u)) {
        return 0u;
    }

    uint32_t sum = 0u;

    const uint8_t* pb =
        (const uint8_t*)pseudo;

    for (uint32_t i = 0u; i < sizeof(tcp_pseudo_t); i += 2u) {
        uint16_t word =
            ((uint16_t)pb[i] << 8u);

        if (i + 1u < sizeof(tcp_pseudo_t)) {
            word |= pb[i + 1u];
        }

        sum += word;
    }

    for (uint32_t i = 0u; i < (uint32_t)seg_len; i += 2u) {
        uint16_t word =
            ((uint16_t)tcp_seg[i] << 8u);

        if (i + 1u < (uint32_t)seg_len) {
            word |= tcp_seg[i + 1u];
        }

        sum += word;
    }

    while (sum >> 16u) {
        sum = (sum & 0xFFFFu) + (sum >> 16u);
    }

    return __builtin_bswap16(
        (uint16_t)(~sum & 0xFFFFu)
    );
}

/*
 * Send one TCP segment.
 *
 * This function deliberately does NOT advance seq_num.
 * Sequence advancement belongs to the caller so retransmitting the same
 * segment does not consume sequence space a second time.
 */
static int tcp_send_segment(tcp_conn_t* conn,
                            uint8_t flags,
                            const uint8_t* payload,
                            uint16_t payload_len) {
    if (!conn) {
        return -1;
    }

    if (payload_len > 0u && !payload) {
        return -1;
    }

    if (payload_len > TCP_MSS) {
        return -1;
    }

    uint16_t seg_len =
        (uint16_t)(TCP_HLEN + payload_len);

    uint8_t buf[TCP_HLEN + TCP_MSS];

    tcp_header_t* tcp =
        (tcp_header_t*)buf;

    tcp->src_port =
        __builtin_bswap16(conn->local_port);

    tcp->dst_port =
        __builtin_bswap16(conn->remote_port);

    tcp->seq_num =
        __builtin_bswap32(conn->seq_num);

    tcp->ack_num =
        __builtin_bswap32(conn->ack_num);

    tcp->data_offset =
        (uint8_t)(TCP_HLEN_WORDS << 4u);

    tcp->flags = flags;

    tcp->window =
        __builtin_bswap16(TCP_DEFAULT_WINDOW);

    tcp->checksum = 0u;
    tcp->urgent_ptr = 0u;

    /*
     * Copy exactly payload_len bytes.
     * Never round the copy up to a machine word.
     */
    for (uint32_t i = 0u;
         i < (uint32_t)payload_len;
         ++i) {
        buf[TCP_HLEN + i] = payload[i];
    }

    tcp_pseudo_t pseudo;

    pseudo.src_ip =
        __builtin_bswap32(conn->local_ip);

    pseudo.dst_ip =
        __builtin_bswap32(conn->remote_ip);

    pseudo.zero = 0u;
    pseudo.protocol = IP_PROTO_TCP;

    pseudo.tcp_length =
        __builtin_bswap16(seg_len);

    tcp->checksum =
        tcp_checksum(
            &pseudo,
            buf,
            seg_len
        );

    ip_send(
        conn->remote_ip,
        IP_PROTO_TCP,
        buf,
        seg_len
    );

    return 0;
}

int tcp_connect(uint32_t remote_ip,
                uint16_t remote_port) {
    if (g_tcp_conn.state != TCP_CLOSED) {
        return -1;
    }

    g_tcp_conn.local_ip =
        OUR_IP_ADDR;

    g_tcp_conn.remote_ip =
        remote_ip;

    g_tcp_conn.local_port =
        12345u;

    g_tcp_conn.remote_port =
        remote_port;

    g_tcp_conn.state =
        TCP_SYN_SENT;

    g_tcp_conn.retries = 0u;
    g_tcp_conn.waiting_for_ack = 1;
    g_tcp_conn.ack_received = 0;

    serial_puts("[TCP] connect ");

    serial_put_u32(
        (remote_ip >> 24u) & 0xFFu
    );
    serial_puts(".");

    serial_put_u32(
        (remote_ip >> 16u) & 0xFFu
    );
    serial_puts(".");

    serial_put_u32(
        (remote_ip >> 8u) & 0xFFu
    );
    serial_puts(".");

    serial_put_u32(
        remote_ip & 0xFFu
    );

    serial_puts(":");
    serial_put_u32(remote_port);
    serial_puts("\n");

    /*
     * The SYN consumes one sequence number.
     * Keep the same sequence number for every retransmission.
     */
    uint32_t syn_seq =
        g_tcp_conn.seq_num;

    while (g_tcp_conn.state == TCP_SYN_SENT &&
           g_tcp_conn.retries < TCP_MAX_RETRIES) {

        g_tcp_conn.seq_num = syn_seq;

        if (tcp_send_segment(
                &g_tcp_conn,
                TCP_SYN,
                (const uint8_t*)0,
                0u) != 0) {
            g_tcp_conn.state = TCP_CLOSED;
            break;
        }

        uint32_t start =
            timer_ticks();

        while ((timer_ticks() - start) <
               TCP_RTO_TICKS) {

            net_poll_once();

            if (g_tcp_conn.state ==
                TCP_ESTABLISHED) {

                serial_puts(
                    "[TCP] connected\n"
                );

                g_tcp_conn.connected = 1;
                g_tcp_conn.waiting_for_ack = 0;

                return 0;
            }

            if (g_tcp_conn.state ==
                TCP_CLOSED) {

                serial_puts(
                    "[TCP] connection refused (RST)\n"
                );

                g_tcp_conn.waiting_for_ack = 0;
                return -1;
            }
        }

        g_tcp_conn.retries++;

        serial_puts(
            "[TCP] SYN timeout, retry "
        );

        serial_put_u32(
            g_tcp_conn.retries
        );

        serial_puts("\n");
    }

    g_tcp_conn.state = TCP_CLOSED;
    g_tcp_conn.waiting_for_ack = 0;

    serial_puts(
        "[TCP] connect failed\n"
    );

    return -1;
}

int tcp_send(tcp_conn_t* conn,
             const uint8_t* data,
             uint16_t len) {
    if (!conn ||
        conn->state != TCP_ESTABLISHED) {
        serial_puts(
            "[TCP] send: not connected\n"
        );
        return -1;
    }

    if (!data && len != 0u) {
        return -1;
    }

    uint16_t sent = 0u;

    while (sent < len) {
        uint16_t chunk =
            (uint16_t)(len - sent);

        if (chunk > TCP_MSS) {
            chunk = TCP_MSS;
        }

        /*
         * Keep the sequence number unchanged while retransmitting.
         */
        uint32_t segment_seq =
            conn->seq_num;

        uint32_t expected_ack =
            segment_seq + (uint32_t)chunk;

        int acked = 0;

        for (uint16_t retry = 0u;
             retry < TCP_MAX_RETRIES && !acked;
             ++retry) {

            conn->seq_num = segment_seq;

            if (tcp_send_segment(
                    conn,
                    TCP_PSH | TCP_ACK,
                    data + sent,
                    chunk) != 0) {
                return -1;
            }

            uint32_t start =
                timer_ticks();

            while ((timer_ticks() - start) <
                   TCP_RTO_TICKS) {

                net_poll_once();

                if (conn->state !=
                    TCP_ESTABLISHED) {

                    serial_puts(
                        "[TCP] connection lost during send\n"
                    );

                    return -1;
                }

                if (conn->ack_num >= expected_ack) {
                    acked = 1;
                    break;
                }
            }
        }

        if (!acked) {
            serial_puts(
                "[TCP] send timeout\n"
            );
            return -1;
        }

        /*
         * Advance sequence space exactly once,
         * after successful acknowledgement.
         */
        conn->seq_num =
            expected_ack;

        sent =
            (uint16_t)(sent + chunk);
    }

    return (int)sent;
}

int tcp_recv(tcp_conn_t* conn,
             uint8_t* buf,
             uint16_t len) {
    if (!conn || !buf || len == 0u) {
        return 0;
    }

    uint32_t start =
        timer_ticks();

    while (conn->recv_len == 0u) {
        net_poll_once();

        if (conn->state != TCP_ESTABLISHED &&
            conn->state != TCP_FIN_WAIT_1) {

            if (conn->recv_len == 0u) {
                return -1;
            }

            break;
        }

        if ((timer_ticks() - start) > 200u) {
            return 0;
        }
    }

    uint16_t copy_len =
        conn->recv_len;

    if (copy_len > len) {
        copy_len = len;
    }

    /*
     * Exact byte copy.
     */
    for (uint32_t i = 0u;
         i < (uint32_t)copy_len;
         ++i) {
        buf[i] = conn->recv_buf[i];
    }

    /*
     * If the caller's buffer was smaller than the queued data,
     * preserve the remainder instead of silently discarding it.
     */
    if (copy_len < conn->recv_len) {
        uint16_t remaining =
            (uint16_t)(conn->recv_len - copy_len);

        for (uint32_t i = 0u;
             i < (uint32_t)remaining;
             ++i) {
            conn->recv_buf[i] =
                conn->recv_buf[copy_len + i];
        }

        conn->recv_len = remaining;
    } else {
        conn->recv_len = 0u;
    }

    return (int)copy_len;
}

void tcp_close(tcp_conn_t* conn) {
    if (!conn ||
        conn->state != TCP_ESTABLISHED) {
        return;
    }

    uint32_t fin_seq =
        conn->seq_num;

    conn->state =
        TCP_FIN_WAIT_1;

    if (tcp_send_segment(
            conn,
            TCP_FIN | TCP_ACK,
            (const uint8_t*)0,
            0u) != 0) {

        conn->state = TCP_CLOSED;
        conn->connected = 0;
        return;
    }

    /*
     * FIN consumes one sequence number.
     */
    conn->seq_num =
        fin_seq + 1u;

    uint32_t start =
        timer_ticks();

    while ((timer_ticks() - start) <
           TCP_RTO_TICKS) {

        net_poll_once();

        if (conn->state ==
                TCP_TIME_WAIT ||
            conn->state ==
                TCP_CLOSED) {
            break;
        }
    }

    conn->state =
        TCP_CLOSED;

    conn->connected = 0;
    conn->waiting_for_ack = 0;

    serial_puts(
        "[TCP] connection closed\n"
    );
}

void tcp_handle_packet(const uint8_t* data,
                       uint16_t len,
                       uint32_t src_ip) {
    if (!data || len < TCP_HLEN) {
        return;
    }

    const tcp_header_t* tcp =
        (const tcp_header_t*)data;

    uint16_t src_port =
        __builtin_bswap16(tcp->src_port);

    uint16_t dst_port =
        __builtin_bswap16(tcp->dst_port);

    if (src_ip != g_tcp_conn.remote_ip ||
        src_port != g_tcp_conn.remote_port ||
        dst_port != g_tcp_conn.local_port) {
        return;
    }

    uint16_t hdr_words =
        (uint16_t)(
            (tcp->data_offset >> 4u) & 0x0Fu
        );

    if (hdr_words < TCP_HLEN_WORDS) {
        return;
    }

    uint16_t hdr_len =
        (uint16_t)(hdr_words * 4u);

    if (hdr_len > len) {
        return;
    }

    /*
     * Validate the TCP checksum before trusting the segment.
     */
    tcp_pseudo_t pseudo;

    pseudo.src_ip =
        __builtin_bswap32(src_ip);

    pseudo.dst_ip =
        __builtin_bswap32(g_tcp_conn.local_ip);

    pseudo.zero = 0u;
    pseudo.protocol = IP_PROTO_TCP;

    pseudo.tcp_length =
        __builtin_bswap16(len);

    if (tcp_checksum(
            &pseudo,
            data,
            len) != 0u) {
        serial_puts(
            "[TCP] invalid checksum\n"
        );
        return;
    }

    uint8_t flags =
        tcp->flags;

    uint32_t seq =
        __builtin_bswap32(tcp->seq_num);

    uint32_t ack =
        __builtin_bswap32(tcp->ack_num);

    const uint8_t* payload =
        data + hdr_len;

    uint16_t payload_len =
        (uint16_t)(len - hdr_len);

    switch (g_tcp_conn.state) {

    case TCP_SYN_SENT:
        if ((flags & TCP_SYN_ACK) ==
            TCP_SYN_ACK) {

            /*
             * The peer must acknowledge our SYN.
             */
            if (ack !=
                g_tcp_conn.seq_num + 1u) {
                return;
            }

            g_tcp_conn.ack_num =
                seq + 1u;

            /*
             * Peer has acknowledged our SYN,
             * so our next sequence number is ack.
             */
            g_tcp_conn.seq_num =
                ack;

            tcp_send_segment(
                &g_tcp_conn,
                TCP_ACK,
                (const uint8_t*)0,
                0u
            );

            g_tcp_conn.state =
                TCP_ESTABLISHED;

            g_tcp_conn.connected = 1;
            g_tcp_conn.waiting_for_ack = 0;

            serial_puts(
                "[TCP] SYN-ACK received, established\n"
            );

        } else if (flags & TCP_RST) {

            g_tcp_conn.state =
                TCP_CLOSED;

            g_tcp_conn.connected = 0;
            g_tcp_conn.waiting_for_ack = 0;

            serial_puts(
                "[TCP] RST received\n"
            );
        }

        break;

    case TCP_ESTABLISHED:

        if (flags & TCP_RST) {
            g_tcp_conn.state =
                TCP_CLOSED;

            g_tcp_conn.connected = 0;

            serial_puts(
                "[TCP] RST received\n"
            );

            break;
        }

        if (flags & TCP_ACK) {
            if (ack >= g_tcp_conn.ack_num) {
                g_tcp_conn.ack_num = ack;
            }
        }

        if (payload_len > 0u) {

            /*
             * Only accept the next expected sequence.
             */
            if (seq != g_tcp_conn.ack_num) {
                tcp_send_segment(
                    &g_tcp_conn,
                    TCP_ACK,
                    (const uint8_t*)0,
                    0u
                );
                break;
            }

            /*
             * Do not accept data that cannot fit into
             * the receive buffer.
             */
            if (payload_len >
                TCP_RECV_BUF_SIZE) {

                tcp_send_segment(
                    &g_tcp_conn,
                    TCP_ACK,
                    (const uint8_t*)0,
                    0u
                );

                break;
            }

            /*
             * This stack exposes one receive buffer.
             * Refuse new data while previous data is pending.
             */
            if (g_tcp_conn.recv_len != 0u) {
                tcp_send_segment(
                    &g_tcp_conn,
                    TCP_ACK,
                    (const uint8_t*)0,
                    0u
                );

                break;
            }

            for (uint32_t i = 0u;
                 i < (uint32_t)payload_len;
                 ++i) {
                g_tcp_conn.recv_buf[i] =
                    payload[i];
            }

            g_tcp_conn.recv_len =
                payload_len;

            g_tcp_conn.ack_num =
                seq + payload_len;

            tcp_send_segment(
                &g_tcp_conn,
                TCP_ACK,
                (const uint8_t*)0,
                0u
            );
        }

        if (flags & TCP_FIN) {

            if (seq != g_tcp_conn.ack_num) {
                tcp_send_segment(
                    &g_tcp_conn,
                    TCP_ACK,
                    (const uint8_t*)0,
                    0u
                );
                break;
            }

            g_tcp_conn.ack_num =
                seq + 1u;

            tcp_send_segment(
                &g_tcp_conn,
                TCP_ACK,
                (const uint8_t*)0,
                0u
            );

            g_tcp_conn.state =
                TCP_TIME_WAIT;

            serial_puts(
                "[TCP] FIN received\n"
            );
        }

        break;

    case TCP_FIN_WAIT_1:

        if (flags & TCP_ACK) {
            if (ack >= g_tcp_conn.ack_num) {
                g_tcp_conn.ack_num = ack;
            }
        }

        if (flags & TCP_FIN) {
            g_tcp_conn.ack_num =
                seq + 1u;

            tcp_send_segment(
                &g_tcp_conn,
                TCP_ACK,
                (const uint8_t*)0,
                0u
            );

            g_tcp_conn.state =
                TCP_TIME_WAIT;

        } else if (flags & TCP_ACK) {
            if (ack >= g_tcp_conn.seq_num) {
                g_tcp_conn.state =
                    TCP_TIME_WAIT;
            }
        }

        break;

    case TCP_TIME_WAIT:

        g_tcp_conn.state =
            TCP_CLOSED;

        g_tcp_conn.connected = 0;

        break;

    default:
        break;
    }
}