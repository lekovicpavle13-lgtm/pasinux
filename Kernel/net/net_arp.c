#include "net_arp.h"
#include "net_eth.h"
#include "net_ip.h"

#include "rtl8139.h"
#include "serial.h"
#include "timer.h"

#include <stdint.h>

static arp_cache_t g_arp_cache[ARP_CACHE_SIZE];
static uint32_t g_arp_age[ARP_CACHE_SIZE];
static uint32_t g_arp_age_counter;

void arp_init(void) {
    for (uint32_t i = 0u; i < ARP_CACHE_SIZE; ++i) {
        g_arp_cache[i].valid = 0;
        g_arp_age[i] = 0u;
    }

    g_arp_age_counter = 0u;
}

static int arp_cache_lookup(uint32_t ip, uint8_t* out_mac) {
    if (!out_mac) {
        return -1;
    }

    for (uint32_t i = 0u; i < ARP_CACHE_SIZE; ++i) {
        if (g_arp_cache[i].valid &&
            g_arp_cache[i].ip == ip) {

            for (uint32_t j = 0u; j < ETH_ALEN; ++j) {
                out_mac[j] = g_arp_cache[i].mac[j];
            }

            /*
             * Refresh the entry's age so the cache behaves as LRU.
             */
            g_arp_age[i] = ++g_arp_age_counter;

            return 0;
        }
    }

    return -1;
}

static void arp_cache_add(uint32_t ip, const uint8_t* mac) {
    if (!mac) {
        return;
    }

    uint32_t slot = ARP_CACHE_SIZE;

    /*
     * Update an existing entry instead of creating a duplicate.
     */
    for (uint32_t i = 0u; i < ARP_CACHE_SIZE; ++i) {
        if (g_arp_cache[i].valid &&
            g_arp_cache[i].ip == ip) {
            slot = i;
            break;
        }
    }

    /*
     * Prefer a free slot.
     */
    if (slot == ARP_CACHE_SIZE) {
        for (uint32_t i = 0u; i < ARP_CACHE_SIZE; ++i) {
            if (!g_arp_cache[i].valid) {
                slot = i;
                break;
            }
        }
    }

    /*
     * Cache is full: evict the least recently used entry.
     */
    if (slot == ARP_CACHE_SIZE) {
        slot = 0u;

        for (uint32_t i = 1u; i < ARP_CACHE_SIZE; ++i) {
            if (g_arp_age[i] < g_arp_age[slot]) {
                slot = i;
            }
        }
    }

    g_arp_cache[slot].ip = ip;

    for (uint32_t j = 0u; j < ETH_ALEN; ++j) {
        g_arp_cache[slot].mac[j] = mac[j];
    }

    g_arp_cache[slot].valid = 1;
    g_arp_age[slot] = ++g_arp_age_counter;
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

static void arp_fill_ip(uint8_t out[4], uint32_t ip) {
    out[0] = (uint8_t)((ip >> 24u) & 0xFFu);
    out[1] = (uint8_t)((ip >> 16u) & 0xFFu);
    out[2] = (uint8_t)((ip >> 8u) & 0xFFu);
    out[3] = (uint8_t)(ip & 0xFFu);
}

static uint32_t arp_read_ip(const uint8_t ip[4]) {
    return ((uint32_t)ip[0] << 24u) |
           ((uint32_t)ip[1] << 16u) |
           ((uint32_t)ip[2] << 8u) |
           (uint32_t)ip[3];
}

int arp_resolve(uint32_t ip, uint8_t* out_mac) {
    if (!out_mac) {
        return -1;
    }

    if (arp_cache_lookup(ip, out_mac) == 0) {
        return 0;
    }

    arp_packet_t arp;

    arp.htype = __builtin_bswap16(1u);
    arp.ptype = __builtin_bswap16(0x0800u);
    arp.hlen  = 6u;
    arp.plen  = 4u;
    arp.op    = __builtin_bswap16(1u);

    for (uint32_t i = 0u; i < ETH_ALEN; ++i) {
        arp.sha[i] = g_our_mac[i];
        arp.tha[i] = 0u;
    }

    arp_fill_ip(arp.spa, OUR_IP_ADDR);
    arp_fill_ip(arp.tpa, ip);

    /*
     * ARP_TIMEOUT_MS is the timeout for each request attempt.
     *
     * The timer runs at 100 Hz, so each tick is approximately 10 ms.
     */
    uint32_t timeout_ticks =
        (ARP_TIMEOUT_MS + 9u) / 10u;

    if (timeout_ticks == 0u) {
        timeout_ticks = 1u;
    }

    for (uint32_t retry = 0u;
         retry < ARP_RETRIES;
         ++retry) {

        serial_puts("[ARP] request for ");
        serial_put_u32((ip >> 24u) & 0xFFu);
        serial_puts(".");
        serial_put_u32((ip >> 16u) & 0xFFu);
        serial_puts(".");
        serial_put_u32((ip >> 8u) & 0xFFu);
        serial_puts(".");
        serial_put_u32(ip & 0xFFu);
        serial_puts(" (try ");
        serial_put_u32(retry + 1u);
        serial_puts(")\n");

        eth_send(
            g_eth_broadcast,
            ETH_TYPE_ARP,
            (const uint8_t*)&arp,
            sizeof(arp_packet_t)
        );

        uint32_t start = timer_ticks();

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
    if (!data || len < sizeof(arp_packet_t)) {
        return;
    }

    const arp_packet_t* arp =
        (const arp_packet_t*)data;

    /*
     * Only Ethernet/IPv4 ARP is supported.
     */
    if (arp->htype != __builtin_bswap16(1u) ||
        arp->ptype != __builtin_bswap16(0x0800u) ||
        arp->hlen != 6u ||
        arp->plen != 4u) {
        return;
    }

    uint16_t op =
        __builtin_bswap16(arp->op);

    if (op != 1u && op != 2u) {
        return;
    }

    uint32_t sender_ip =
        arp_read_ip(arp->spa);

    /*
     * Learn the sender's mapping from both requests and replies.
     */
    arp_cache_add(sender_ip, arp->sha);

    /*
     * Only respond to ARP requests targeting our configured IP.
     */
    if (op != 1u) {
        return;
    }

    uint32_t target_ip =
        arp_read_ip(arp->tpa);

    if (target_ip != OUR_IP_ADDR) {
        return;
    }

    arp_packet_t reply;

    reply.htype = arp->htype;
    reply.ptype = arp->ptype;
    reply.hlen  = 6u;
    reply.plen  = 4u;
    reply.op    = __builtin_bswap16(2u);

    for (uint32_t i = 0u; i < ETH_ALEN; ++i) {
        reply.sha[i] = g_our_mac[i];
        reply.tha[i] = arp->sha[i];
    }

    arp_fill_ip(reply.spa, OUR_IP_ADDR);

    for (uint32_t i = 0u; i < 4u; ++i) {
        reply.tpa[i] = arp->spa[i];
    }

    eth_send(
        arp->sha,
        ETH_TYPE_ARP,
        (const uint8_t*)&reply,
        sizeof(arp_packet_t)
    );
}

void arp_print_cache(void) {
    serial_puts("[ARP] cache:\n");

    for (uint32_t i = 0u; i < ARP_CACHE_SIZE; ++i) {
        if (!g_arp_cache[i].valid) {
            continue;
        }

        uint32_t ip =
            g_arp_cache[i].ip;

        serial_puts("  ");

        serial_put_u32((ip >> 24u) & 0xFFu);
        serial_puts(".");
        serial_put_u32((ip >> 16u) & 0xFFu);
        serial_puts(".");
        serial_put_u32((ip >> 8u) & 0xFFu);
        serial_puts(".");
        serial_put_u32(ip & 0xFFu);

        serial_puts(" -> ");

        for (uint32_t j = 0u; j < ETH_ALEN; ++j) {
            uint8_t d =
                g_arp_cache[i].mac[j];

            const char* hex =
                "0123456789ABCDEF";

            serial_putc(
                hex[(d >> 4u) & 0x0Fu]
            );

            serial_putc(
                hex[d & 0x0Fu]
            );

            if (j < 5u) {
                serial_putc(':');
            }
        }

        serial_puts("\n");
    }
}