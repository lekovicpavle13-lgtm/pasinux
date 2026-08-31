#ifndef NET_TCP_H
#define NET_TCP_H

#include <stdint.h>

#define TCP_DEFAULT_WINDOW  65535u
#define TCP_MSS             1460u
#define TCP_RTO_TICKS       50u    /* 500ms at 100Hz */
#define TCP_MAX_RETRIES     3u
#define TCP_RECV_BUF_SIZE   2048u

/* TCP header length in 32-bit words */
#define TCP_HLEN_WORDS 5u
#define TCP_HLEN       (TCP_HLEN_WORDS * 4u)

/* TCP flags */
#define TCP_FIN  0x01u
#define TCP_SYN  0x02u
#define TCP_RST  0x04u
#define TCP_PSH  0x08u
#define TCP_ACK  0x10u
#define TCP_SYN_ACK (TCP_SYN | TCP_ACK)

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t  data_offset;   /* top 4 bits = header length in 4-byte words */
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_ptr;
} __attribute__((packed)) tcp_header_t;

/* TCP pseudo-header for checksum calculation */
typedef struct {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint8_t  zero;
    uint8_t  protocol;
    uint16_t tcp_length;
} __attribute__((packed)) tcp_pseudo_t;

typedef enum {
    TCP_CLOSED,
    TCP_SYN_SENT,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,
    TCP_TIME_WAIT
} tcp_state_t;

typedef struct {
    tcp_state_t state;
    uint32_t    local_ip;
    uint32_t    remote_ip;
    uint16_t    local_port;
    uint16_t    remote_port;
    uint32_t    seq_num;
    uint32_t    ack_num;
    uint8_t     recv_buf[TCP_RECV_BUF_SIZE];
    uint16_t    recv_len;
    uint32_t    rto_start_tick;    /* timer start for retransmit */
    uint16_t    retries;
    int         connected;
    /* For blocking connect/send */
    volatile int waiting_for_ack;
    volatile int ack_received;
} tcp_conn_t;

void tcp_init(void);
int  tcp_connect(uint32_t remote_ip, uint16_t remote_port);
int  tcp_send(tcp_conn_t* conn, const uint8_t* data, uint16_t len);
int  tcp_recv(tcp_conn_t* conn, uint8_t* buf, uint16_t len);
void tcp_close(tcp_conn_t* conn);
void tcp_handle_packet(const uint8_t* data, uint16_t len, uint32_t src_ip);
tcp_conn_t* tcp_get_conn(void);

#endif /* NET_TCP_H */