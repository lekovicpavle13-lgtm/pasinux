#include "net_ip.h"
#include "net_arp.h"
#include "net_eth.h"
#include "net_tcp.h"

#include "serial.h"

#include <stdint.h>

static uint16_t g_ip_id;

uint16_t ip_checksum(const uint16_t* data, uint32_t word_count) {
    if (!data) {
        return 0u;
    }

    uint32_t sum = 0u;

    for (uint32_t i = 0u; i < word_count; ++i) {
        /*
         * IPv4 fields are stored in network byte order. Convert each
         * 16-bit word before adding it so the checksum is independent
         * of the CPU's native endianness.
         */
        sum += (uint32_t)__builtin_bswap16(data[i]);
    }

    while (sum >> 16u) {
        sum = (sum & 0xFFFFu) + (sum >> 16u);
    }

    return __builtin_bswap16((uint16_t)(~sum & 0xFFFFu));
}

void ip_send(uint32_t dst_ip, uint8_t protocol,
             const uint8_t* payload, uint16_t len) {
    if (len > (uint16_t)(ETH_MTU - IP_HLEN)) {
        return;
    }

    if (!payload && len != 0u) {
        return;
    }

    uint16_t total_len = (uint16_t)(IP_HLEN + len);

    uint8_t buf[IP_HLEN + ETH_MTU];
    ip_header_t* ip = (ip_header_t*)buf;

    ip->ver_ihl      = 0x45u;
    ip->dscp_ecn     = 0u;
    ip->total_length = __builtin_bswap16(total_len);
    ip->id           = __builtin_bswap16(g_ip_id++);
    ip->flags_frag   = __builtin_bswap16(0x4000u);
    ip->ttl          = 64u;
    ip->protocol     = protocol;
    ip->header_cksum = 0u;
    ip->src_ip       = __builtin_bswap32(OUR_IP_ADDR);
    ip->dst_ip       = __builtin_bswap32(dst_ip);

    ip->header_cksum = ip_checksum(
        (const uint16_t*)buf,
        IP_HLEN / 2u
    );

    /*
     * Copy exactly len bytes. The old implementation rounded the
     * length up to 32-bit words and could read past the payload.
     */
    uint8_t* dst = buf + IP_HLEN;

    for (uint32_t i = 0u; i < (uint32_t)len; ++i) {
        dst[i] = payload[i];
    }

    uint8_t dst_mac[ETH_ALEN];

    if (arp_resolve(dst_ip, dst_mac) == 0) {
        eth_send(
            dst_mac,
            ETH_TYPE_IP,
            buf,
            total_len
        );
    } else {
        serial_puts("[IP] ARP resolution failed for dst\n");
    }
}

void ip_handle_packet(const uint8_t* data, uint16_t len) {
    if (!data || len < IP_HLEN) {
        return;
    }

    const ip_header_t* ip = (const ip_header_t*)data;

    uint8_t version = (uint8_t)(ip->ver_ihl >> 4u);
    uint8_t ihl_words = (uint8_t)(ip->ver_ihl & 0x0Fu);

    if (version != 4u || ihl_words < 5u) {
        return;
    }

    uint16_t header_len =
        (uint16_t)ihl_words * 4u;

    if (header_len > len) {
        return;
    }

    uint16_t total_len =
        __builtin_bswap16(ip->total_length);

    /*
     * IPv4 total_length includes the IP header itself.
     */
    if (total_len < header_len || total_len > len) {
        return;
    }

    /*
     * Validate the complete IPv4 header checksum.
     *
     * The checksum covers the entire header, including any options.
     * A correct header produces a zero checksum result.
     */
    if (ip_checksum(
            (const uint16_t*)data,
            (uint32_t)header_len / 2u
        ) != 0u) {
        return;
    }

    /*
     * This stack has no IP fragment reassembly implementation.
     * Reject fragmented packets instead of passing incomplete data
     * to the transport layer.
     */
    uint16_t flags_frag =
        __builtin_bswap16(ip->flags_frag);

    if ((flags_frag & 0x2000u) != 0u ||
        (flags_frag & 0x1FFFu) != 0u) {
        return;
    }

    uint8_t protocol = ip->protocol;

    const uint8_t* payload =
        data + header_len;

    uint16_t payload_len =
        (uint16_t)(total_len - header_len);

    uint32_t src_ip =
        __builtin_bswap32(ip->src_ip);

    switch (protocol) {
    case IP_PROTO_TCP:
        tcp_handle_packet(
            payload,
            payload_len,
            src_ip
        );
        break;

    default:
        break;
    }
}