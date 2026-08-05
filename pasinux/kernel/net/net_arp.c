#include "net_arp.h"
#include "net_eth.h"

#include "rtl8139.h"
#include "serial.h"
#include "timer.h"

#include <stdint.h>

static arp_cache_t g_arp_cache[ARP_CACHE_SIZE];

void arp_init(void) {
    for (uint32_t i = 0u; i < ARP_CACHE_SIZE; ++i) {
        g_arp_cache[i].valid = 0;
    }
}

static int arp_cache_lookup(uint32_t ip, uint8_t* out_mac) {
    for (uint32_t i = 0u; i < ARP_CACHE_SIZE; ++i) {
        if (g_arp_cache[i].valid && g_arp_cache[i].ip == ip) {
            for (uint32_t j = 0u; j < ETH_ALEN; ++j) {
                out_mac[j] = g_arp_cache[i].mac[j];
            }
            return 0;
        }
    }
    return -1;
}

static void arp_cache_add(uint32_t ip, const uint8_t* mac) {
    /* Find existing or oldest slot */
    uint32_t oldest = 0u;
    for (uint32_t i = 0u; i < ARP_CACHE_SIZE; ++i) {
        if (!g_arp_cache[i].valid) {
            oldest = i;
            break;
        }
    }
    /* If no free slot, use slot 0 (simple LRU approximation) */
    if (g_arp_cache[oldest].valid && oldest > 0u) oldest = 0u;

    g_arp_cache[oldest].ip = ip;
    for (uint32_t j = 0u; j < ETH_ALEN; ++j) {
        g_arp_cache[oldest].mac[j] = mac[j];
    }
    g_arp_cache[oldest].valid = 1;
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

int arp_resolve(uint32_t ip, uint8_t* out_mac) {
    if (arp_cache_lookup(ip, out_mac) == 0) {
        return 0;  /* cached */
    }

    /* Build ARP request */
    arp_packet_t arp;
    arp.htype = __builtin_bswap16(1u);           /* Ethernet */
    arp.ptype = __builtin_bswap16(0x0800u);       /* IPv4 */
    arp.hlen  = 6u;
    arp.plen  = 4u;
    arp.op    = __builtin_bswap16(1u);            /* request */

    for (uint32_t i = 0u; i < ETH_ALEN; ++i) {
        arp.sha[i] = g_our_mac[i];
        arp.tha[i] = 0u;
    }
    /* Our IP = 10.0.2.15 (QEMU guest default) */
    arp.spa[0] = 10u; arp.spa[1] = 0u; arp.spa[2] = 2u; arp.spa[3] = 15u;
    arp.tpa[0] = (uint8_t)((ip >> 24) & 0xFFu);
    arp.tpa[1] = (uint8_t)((ip >> 16) & 0xFFu);
    arp.tpa[2] = (uint8_t)((ip >> 8) & 0xFFu);
    arp.tpa[3] = (uint8_t)(ip & 0xFFu);

    for (uint32_t retry = 0u; retry < ARP_RETRIES; ++retry) {
        serial_puts("[ARP] request for ");
        serial_put_u32((ip >> 24) & 0xFFu);
        serial_puts(".");
        serial_put_u32((ip >> 16) & 0xFFu);
        serial_puts(".");
        serial_put_u32((ip >> 8) & 0xFFu);
        serial_puts(".");
        serial_put_u32(ip & 0xFFu);
        serial_puts(" (try ");
        serial_put_u32(retry + 1u);
        serial_puts(")\n");

        eth_send(g_eth_broadcast, ETH_TYPE_ARP, (const uint8_t*)&arp, sizeof(arp_packet_t));

        /* Poll for up to ARP_TIMEOUT_MS milliseconds */
        uint32_t start = timer_ticks();  /* 100 Hz = 10ms per tick */
        uint32_t timeout_ticks = 5u;     /* ~50ms per retry */
        while ((timer_ticks() - start) < timeout_ticks) {
            net_poll_once();
            if (arp_cache_lookup(ip, out_mac) == 0) {
                serial_puts("[ARP] resolved\n");
                return 0;
            }
        }
    }

    serial_puts("[ARP] failed to resolve\n");
    return -1;
}

void arp_handle_packet(const uint8_t* data, uint16_t len) {
    if (!data || len < sizeof(arp_packet_t)) return;

    const arp_packet_t* arp = (const arp_packet_t*)data;

    /* Only handle Ethernet/IPv4 */
    if (arp->htype != __builtin_bswap16(1u)) return;
    if (arp->ptype != __builtin_bswap16(0x0800u)) return;

    /* Extract sender IP */
    uint32_t sender_ip = ((uint32_t)arp->spa[0] << 24)
                       | ((uint32_t)arp->spa[1] << 16)
                       | ((uint32_t)arp->spa[2] << 8)
                       | (uint32_t)arp->spa[3];

    /* Add to cache */
    arp_cache_add(sender_ip, arp->sha);

    /* Handle ARP request for our IP */
    if (arp->op == __builtin_bswap16(1u)) {
        /* Check if target IP is ours (10.0.2.15) */
        if (arp->tpa[0] == 10u && arp->tpa[1] == 0u &&
            arp->tpa[2] == 2u && arp->tpa[3] == 15u) {

            arp_packet_t reply;
            reply.htype = arp->htype;
            reply.ptype = arp->ptype;
            reply.hlen  = 6u;
            reply.plen  = 4u;
            reply.op    = __builtin_bswap16(2u);  /* reply */

            for (uint32_t i = 0u; i < ETH_ALEN; ++i) {
                reply.sha[i] = g_our_mac[i];
                reply.tha[i] = arp->sha[i];
            }
            reply.spa[0] = 10u; reply.spa[1] = 0u;
            reply.spa[2] = 2u;  reply.spa[3] = 15u;
            for (uint32_t i = 0u; i < 4u; ++i) {
                reply.tpa[i] = arp->spa[i];
            }

            eth_send(arp->sha, ETH_TYPE_ARP, (const uint8_t*)&reply, sizeof(arp_packet_t));
        }
    }
}

void arp_print_cache(void) {
    serial_puts("[ARP] cache:\n");
    for (uint32_t i = 0u; i < ARP_CACHE_SIZE; ++i) {
        if (g_arp_cache[i].valid) {
            uint32_t ip = g_arp_cache[i].ip;
            serial_puts("  ");
            serial_put_u32((ip >> 24) & 0xFFu);
            serial_puts(".");
            serial_put_u32((ip >> 16) & 0xFFu);
            serial_puts(".");
            serial_put_u32((ip >> 8) & 0xFFu);
            serial_puts(".");
            serial_put_u32(ip & 0xFFu);
            serial_puts(" -> ");
            for (uint32_t j = 0u; j < ETH_ALEN; ++j) {
                uint8_t d = g_arp_cache[i].mac[j];
                const char* hex = "0123456789ABCDEF";
                serial_putc(hex[(d >> 4) & 0x0Fu]);
                serial_putc(hex[d & 0x0Fu]);
                if (j < 5u) serial_putc(':');
            }
            serial_puts("\n");
        }
    }
}