#ifndef NET_ARP_H
#define NET_ARP_H

#include <stdint.h>

#define ARP_CACHE_SIZE 8u
#define ARP_TIMEOUT_MS 500u
#define ARP_RETRIES    3u

/* ARP header on wire (after Ethernet header) */
typedef struct {
    uint16_t htype;    /* hardware type (1 = Ethernet) */
    uint16_t ptype;    /* protocol type (0x0800 = IPv4) */
    uint8_t  hlen;     /* hardware addr len (6 for MAC) */
    uint8_t  plen;     /* protocol addr len (4 for IPv4) */
    uint16_t op;       /* 1=request, 2=reply */
    uint8_t  sha[6];   /* sender MAC */
    uint8_t  spa[4];   /* sender IP */
    uint8_t  tha[6];   /* target MAC */
    uint8_t  tpa[4];   /* target IP */
} __attribute__((packed)) arp_packet_t;

/* ARP cache entry */
typedef struct {
    uint32_t ip;
    uint8_t  mac[6];
    int      valid;
} arp_cache_t;

/* Resolve IP to MAC — may block for ARP request/reply */
int arp_resolve(uint32_t ip, uint8_t* out_mac);

/* Handle received ARP packet */
void arp_handle_packet(const uint8_t* data, uint16_t len);

/* Print ARP cache to serial */
void arp_print_cache(void);

/* Initialize ARP cache */
void arp_init(void);

#endif /* NET_ARP_H */