#ifndef NET_ETH_H
#define NET_ETH_H

#include <stdint.h>

/* Ethernet header: 14 bytes */
#define ETH_ALEN      6u
#define ETH_HLEN      14u
#define ETH_MTU       1500u

#define ETH_TYPE_ARP  0x0806u
#define ETH_TYPE_IP   0x0800u

typedef struct {
    uint8_t  dst[ETH_ALEN];
    uint8_t  src[ETH_ALEN];
    uint16_t type;   /* network byte order */
} __attribute__((packed)) eth_header_t;

/* Broadcast MAC */
extern const uint8_t g_eth_broadcast[ETH_ALEN];

/* Our MAC (set during net init) */
extern uint8_t g_our_mac[ETH_ALEN];

void eth_send(const uint8_t* dst_mac, uint16_t type,
              const uint8_t* payload, uint16_t len);
void eth_dispatch(const uint8_t* frame, uint16_t len);

/* Set our MAC address (called after NIC init) */
void net_set_mac(const uint8_t mac[ETH_ALEN]);

#endif /* NET_ETH_H */