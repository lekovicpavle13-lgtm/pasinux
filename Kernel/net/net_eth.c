#include "net_eth.h"
#include "net_arp.h"
#include "net_ip.h"

#include "rtl8139.h"
#include "serial.h"

#include <stdint.h>

const uint8_t g_eth_broadcast[ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t g_our_mac[ETH_ALEN];

void eth_send(const uint8_t* dst_mac, uint16_t type,
              const uint8_t* payload, uint16_t len) {
    rtl8139_t* nic = rtl8139_get();
    if (!nic || nic->io_base == 0u) return;
    if (len > ETH_MTU) return;

    uint8_t frame[ETH_HLEN + ETH_MTU];
    eth_header_t* hdr = (eth_header_t*)frame;

    for (uint32_t i = 0u; i < ETH_ALEN; ++i) {
        hdr->dst[i] = dst_mac[i];
        hdr->src[i] = g_our_mac[i];
    }
    hdr->type = __builtin_bswap16(type);

    /* Copy payload after header */
    typedef uint32_t __attribute__((__may_alias__)) u32_alias;
    const uint32_t* s = (const uint32_t*)payload;
    uint32_t* d = (uint32_t*)(frame + ETH_HLEN);
    uint32_t words = (uint32_t)((len + 3u) / 4u);
    for (uint32_t i = 0u; i < words; ++i) {
        d[i] = ((const u32_alias*)s)[i];
    }

    rtl8139_send(nic, frame, (uint16_t)(ETH_HLEN + len));
}

void eth_dispatch(const uint8_t* frame, uint16_t len) {
    if (!frame || len < ETH_HLEN) return;

    const eth_header_t* hdr = (const eth_header_t*)frame;
    uint16_t type = __builtin_bswap16(hdr->type);
    const uint8_t* payload = frame + ETH_HLEN;
    uint16_t payload_len = (uint16_t)(len - ETH_HLEN);

    switch (type) {
    case ETH_TYPE_ARP:
        arp_handle_packet(payload, payload_len);
        break;
    case ETH_TYPE_IP:
        ip_handle_packet(payload, payload_len);
        break;
    default:
        break;
    }
}

void net_set_mac(const uint8_t mac[ETH_ALEN]) {
    for (uint32_t i = 0u; i < ETH_ALEN; ++i) {
        g_our_mac[i] = mac[i];
    }
}