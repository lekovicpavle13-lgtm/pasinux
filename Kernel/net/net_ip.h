#ifndef NET_IP_H
#define NET_IP_H

#include <stdint.h>

#define IP_PROTO_TCP 6u
#define IP_PROTO_UDP 17u

#define IP_HLEN 20u

/* Our IP in QEMU user-mode networking */
#define OUR_IP_ADDR  0x0A00020Fu  /* 10.0.2.15 */
#define GW_IP_ADDR   0x0A000202u  /* 10.0.2.2 (QEMU host gateway) */

typedef struct {
    uint8_t  ver_ihl;       /* version (4) | header length (5) */
    uint8_t  dscp_ecn;
    uint16_t total_length;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t header_cksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} __attribute__((packed)) ip_header_t;

uint16_t ip_checksum(const uint16_t* data, uint32_t word_count);

void ip_send(uint32_t dst_ip, uint8_t protocol,
             const uint8_t* payload, uint16_t len);

void ip_handle_packet(const uint8_t* data, uint16_t len);

#endif /* NET_IP_H */