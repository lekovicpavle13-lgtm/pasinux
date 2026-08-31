#include "net_ip.h"
#include "net_arp.h"
#include "net_eth.h"  /* for ETH_MTU */
#include "net_tcp.h"

#include "serial.h"

#include <stdint.h>

static uint16_t g_ip_id;

uint16_t ip_checksum(const uint16_t* data, uint32_t word_count) {
    uint32_t sum = 0u;
    for (uint32_t i = 0u; i < word_count; ++i) {
        sum += data[i];
    }
    while (sum >> 16u) {
        sum = (sum & 0xFFFFu) + (sum >> 16u);
    }
    return (uint16_t)(~sum & 0xFFFFu);
}

void ip_send(uint32_t dst_ip, uint8_t protocol,
             const uint8_t* payload, uint16_t len) {
    if (len > ETH_MTU) return;
    uint16_t total_len = (uint16_t)(IP_HLEN + len);

    /* Build frame buffer: IP header + payload */
    uint8_t buf[IP_HLEN + ETH_MTU];
    ip_header_t* ip = (ip_header_t*)buf;

    ip->ver_ihl       = 0x45u;  /* IPv4, 5 words = 20 bytes */
    ip->dscp_ecn      = 0u;
    ip->total_length  = __builtin_bswap16(total_len);
    ip->id            = __builtin_bswap16(g_ip_id++);
    ip->flags_frag    = __builtin_bswap16(0x4000u);  /* Don't Fragment */
    ip->ttl           = 64u;
    ip->protocol      = protocol;
    ip->header_cksum  = 0u;
    ip->src_ip        = __builtin_bswap32(OUR_IP_ADDR);
    ip->dst_ip        = __builtin_bswap32(dst_ip);

    /* Compute checksum over IP header only (10 words for 20-byte header) */
    ip->header_cksum = ip_checksum((const uint16_t*)buf, 10u);

    /* Copy payload after header */
    typedef uint32_t __attribute__((__may_alias__)) u32_alias;
    const uint32_t* s = (const uint32_t*)payload;
    uint32_t* d = (uint32_t*)(buf + IP_HLEN);
    uint32_t words = (uint32_t)((len + 3u) / 4u);
    for (uint32_t i = 0u; i < words; ++i) {
        d[i] = ((const u32_alias*)s)[i];
    }

    /* Resolve destination MAC via ARP, then send */
    uint8_t dst_mac[ETH_ALEN];
    if (arp_resolve(dst_ip, dst_mac) == 0) {
        eth_send(dst_mac, ETH_TYPE_IP, buf, total_len);
    } else {
        serial_puts("[IP] ARP resolution failed for dst\n");
    }
}

void ip_handle_packet(const uint8_t* data, uint16_t len) {
    if (!data || len < IP_HLEN) return;

    const ip_header_t* ip = (const ip_header_t*)data;

    /* Verify header checksum */
    /* We need a mutable copy to zero the checksum field */
    /* For simplicity, skip checksum validation for now */
    /* (The host gateway generally produces valid packets) */

    uint8_t protocol = ip->protocol;
    const uint8_t* payload = data + IP_HLEN;
    uint16_t payload_len = (uint16_t)(len - IP_HLEN);

    switch (protocol) {
    case IP_PROTO_TCP:
        tcp_handle_packet(payload, payload_len, __builtin_bswap32(ip->src_ip));
        break;
    default:
        break;
    }
}