#ifndef RTL8139_H
#define RTL8139_H

#include <stdint.h>

#define RTL8139_VENDOR_ID  0x10ECu
#define RTL8139_DEVICE_ID  0x8139u

/* Register offsets (I/O port based) */
#define RTL_MAC0     0x00u   /* MAC address bytes 0-3 (R) */
#define RTL_MAC4     0x04u   /* MAC address bytes 4-5 (R) */
#define RTL_MAR0     0x08u   /* multicast (R/W) */
#define RTL_MAR4     0x0Cu
#define RTL_TSD0     0x10u   /* TX status/command descriptor 0 */
#define RTL_TSD1     0x14u   /* TX status/command descriptor 1 */
#define RTL_TSD2     0x18u   /* (not used in standard RTL8139) */
#define RTL_TSD3     0x1Cu   /* (not used in standard RTL8139) */
#define RTL_TSAD0    0x20u   /* TX buffer address 0 */
#define RTL_TSAD1    0x24u   /* TX buffer address 1 */
#define RTL_TSAD2    0x28u   /* TX buffer address 2 */
#define RTL_TSAD3    0x2Cu   /* TX buffer address 3 */
#define RTL_RBSTART  0x30u   /* RX buffer physical address */
#define RTL_CRDA     0x34u   /* current RX descriptor addr (R) */
#define RTL_CAPR     0x38u   /* current addr of packet read (R/W) */
#define RTL_IMR      0x3Cu   /* interrupt mask */
#define RTL_ISR      0x3Eu   /* interrupt status (write 1 to clear) */
#define RTL_TCR      0x40u   /* TX config */
#define RTL_RCR      0x44u   /* RX config */
#define RTL_9346CR   0x50u   /* EEPROM control */
#define RTL_CONFIG1  0x52u   /* config 1 */
#define RTL_CR       0x37u   /* command register */
#define RTL_MSR      0x58u   /* media status */
#define RTL_BMCR     0x5Au   /* basic mode control (PHY) */

/* CR bits */
#define RTL_CR_RST   0x10u   /* reset */
#define RTL_CR_RE    0x08u   /* receiver enable */
#define RTL_CR_TE    0x04u   /* transmitter enable */

/* RCR bits */
#define RTL_RCR_AB   0x08u   /* accept broadcast */
#define RTL_RCR_AM   0x04u   /* accept multicast */
#define RTL_RCR_APM  0x02u   /* accept physical match */
#define RTL_RCR_AAP  0x01u   /* accept all (promiscuous) */

/* ISR bits */
#define RTL_ISR_ROK  0x01u   /* receive OK */
#define RTL_ISR_TOK  0x04u   /* transmit OK */
#define RTL_ISR_RER  0x10u   /* receive error */
#define RTL_ISR_TER  0x20u   /* transmit error */
#define RTL_ISR_RX_OVW 0x40u /* RX overflow */

/* TX status bits */
#define RTL_TSD_TOK  0x8000u /* transmit OK */
#define RTL_TSD_TUN  0x4000u /* transmit underrun */
#define RTL_TSD_OWN  0x2000u /* DMA owns buffer */
#define RTL_TSD_SIZE_MASK 0x1FFFu  /* packet size mask */

/* RX packet header */
#define RTL_RX_ROK   0x0001u /* receive OK */
#define RTL_RX_FOFS  0x0004u /* frame alignment error */
#define RTL_RX_CRC   0x0008u /* CRC error */

#define RTL_NUM_TX_DESC 4u
#define RTL_TX_BUF_SIZE 1536u   /* enough for a full Ethernet frame */
#define RTL_RX_BUF_SIZE (8192u + 16u)  /* RTL8139 needs 8KB + 16 bytes */

typedef struct {
    uint16_t io_base;          /* I/O port base from PCI BAR0 */
    uint8_t  mac[6];           /* MAC address */
    uint8_t  irq;              /* IRQ line from PCI config */
    uint8_t* rx_buffer;        /* kmalloc'd RX ring (virtual addr) */
    uint32_t rx_buffer_phys;   /* physical addr programmed into chip */
    uint8_t* tx_buffers[RTL_NUM_TX_DESC]; /* kmalloc'd TX buffers (virtual) */
    uint32_t tx_buffer_phys[RTL_NUM_TX_DESC]; /* physical for each */
    uint8_t  tx_cur;           /* current TX descriptor index */
    uint8_t  tx_busy[RTL_NUM_TX_DESC]; /* descriptor awaiting completion */
    uint32_t rx_offset;        /* current read offset in RX ring */
    uint32_t packet_count;     /* statistics */
    uint32_t byte_count;
} rtl8139_t;

/* Initialize the RTL8139 from PCI address, returns 0 on success */
int rtl8139_init(uint16_t pci_addr, rtl8139_t* nic);

/* Send a packet, returns 0 on success */
int rtl8139_send(rtl8139_t* nic, const uint8_t* data, uint16_t len);

/* Poll for received data — returns 0 if nothing, otherwise packet length */
uint16_t rtl8139_poll(rtl8139_t* nic, uint8_t* out_buf, uint16_t buf_size);

/* IRQ handler — must be registered with irq_register */
void rtl8139_irq_handler(void);

/* Access the global nic instance (set by rtl8139_init) */
rtl8139_t* rtl8139_get(void);

#endif /* RTL8139_H */