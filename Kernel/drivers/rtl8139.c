#include "rtl8139.h"

#include "io.h"
#include "interrupt.h"
#include "mm_fs.h"
#include "paging.h"
#include "pci.h"
#include "serial.h"
#include "vga.h"

#include <stdint.h>

static rtl8139_t g_nic;

rtl8139_t* rtl8139_get(void) {
    return &g_nic;
}

static inline uint8_t rtl_inb(rtl8139_t* nic, uint16_t reg) {
    return inb((uint16_t)(nic->io_base + reg));
}

static inline uint32_t rtl_inl(rtl8139_t* nic, uint16_t reg) {
    return inl((uint16_t)(nic->io_base + reg));
}

static inline void rtl_outb(rtl8139_t* nic, uint16_t reg, uint8_t value) {
    outb((uint16_t)(nic->io_base + reg), value);
}

static inline void rtl_outl(rtl8139_t* nic, uint16_t reg, uint32_t value) {
    outl((uint16_t)(nic->io_base + reg), value);
}

static void rtl_read_mac(rtl8139_t* nic) {
    uint32_t low = rtl_inl(nic, RTL_MAC0);
    uint32_t high = rtl_inl(nic, RTL_MAC4);
    nic->mac[0] = (uint8_t)(low & 0xFFu);
    nic->mac[1] = (uint8_t)((low >> 8) & 0xFFu);
    nic->mac[2] = (uint8_t)((low >> 16) & 0xFFu);
    nic->mac[3] = (uint8_t)((low >> 24) & 0xFFu);
    nic->mac[4] = (uint8_t)(high & 0xFFu);
    nic->mac[5] = (uint8_t)((high >> 8) & 0xFFu);
}

static void rtl_print_mac(rtl8139_t* nic) {
    char buf[20];
    const char* digits = "0123456789ABCDEF";
    buf[0] = digits[(nic->mac[0] >> 4) & 0x0Fu];
    buf[1] = digits[nic->mac[0] & 0x0Fu];
    buf[2] = ':';
    buf[3] = digits[(nic->mac[1] >> 4) & 0x0Fu];
    buf[4] = digits[nic->mac[1] & 0x0Fu];
    buf[5] = ':';
    buf[6] = digits[(nic->mac[2] >> 4) & 0x0Fu];
    buf[7] = digits[nic->mac[2] & 0x0Fu];
    buf[8] = ':';
    buf[9] = digits[(nic->mac[3] >> 4) & 0x0Fu];
    buf[10] = digits[nic->mac[3] & 0x0Fu];
    buf[11] = ':';
    buf[12] = digits[(nic->mac[4] >> 4) & 0x0Fu];
    buf[13] = digits[nic->mac[4] & 0x0Fu];
    buf[14] = ':';
    buf[15] = digits[(nic->mac[5] >> 4) & 0x0Fu];
    buf[16] = '\0';
    serial_puts(" MAC ");
    serial_puts(buf);
    vga_puts(" MAC ");
    vga_puts(buf);
}

static void rtl_reset(rtl8139_t* nic) {
    rtl_outb(nic, RTL_CR, RTL_CR_RST);
    uint32_t timeout = 0u;
    while (rtl_inb(nic, RTL_CR) & RTL_CR_RST) {
        ++timeout;
        if (timeout > 10000u) break;
    }
    serial_puts("[RTL] reset complete (timeout=");
    serial_put_u32(timeout);
    serial_puts(")\n");
}

void rtl8139_irq_handler(void) {
    rtl8139_t* nic = &g_nic;
    if (nic->io_base == 0u) return;

    uint16_t isr = (uint16_t)(rtl_inl(nic, RTL_ISR) & 0xFFFFu);
    if (isr == 0u) return;

    /* Clear ISR by writing back */
    rtl_outl(nic, RTL_ISR, isr);

    if (isr & RTL_ISR_ROK) {
        ++nic->packet_count;
    }
    if (isr & RTL_ISR_TOK) {
        /* TX completed */
    }
    if (isr & (RTL_ISR_RER | RTL_ISR_TER | RTL_ISR_RX_OVW)) {
        serial_puts("[RTL] IRQ error flags=0x");
        serial_put_u32(isr);
        serial_puts("\n");
    }
}

int rtl8139_init(uint16_t pci_addr, rtl8139_t* nic) {
    if (!nic) return -1;

    nic->io_base = 0u;
    nic->irq = 0u;
    nic->rx_buffer = (uint8_t*)0;
    nic->rx_buffer_phys = 0u;
    nic->rx_offset = 0u;
    nic->packet_count = 0u;
    nic->byte_count = 0u;
    nic->tx_cur = 0u;
    for (uint32_t i = 0u; i < RTL_NUM_TX_DESC; ++i) {
        nic->tx_buffers[i] = (uint8_t*)0;
        nic->tx_buffer_phys[i] = 0u;
    }

    /* Read I/O base from BAR0 */
    uint32_t bar0 = pci_read_bar(pci_addr, 0);
    if (!(bar0 & PCI_BAR_IO)) {
        serial_puts("[RTL] BAR0 is not I/O!\n");
        return -1;
    }
    nic->io_base = (uint16_t)(bar0 & 0xFFFCu);

    /* Read IRQ line */
    nic->irq = pci_read_irq(pci_addr);

    /* Enable bus mastering for DMA */
    pci_enable_bus_mastering(pci_addr);

    serial_puts("[RTL] found at IO=0x");
    serial_put_u32(nic->io_base);
    serial_puts(" IRQ=");
    serial_put_u32(nic->irq);

    /* Read MAC address */
    rtl_read_mac(nic);
    rtl_print_mac(nic);
    serial_puts("\n");
    vga_puts("\n");

    /* Reset the chip */
    rtl_reset(nic);

    /* Allocate RX buffer (must be 8KB + 16, need physical addr) */
    nic->rx_buffer = (uint8_t*)kmalloc(RTL_RX_BUF_SIZE);
    if (!nic->rx_buffer) {
        serial_puts("[RTL] kmalloc RX buffer failed\n");
        return -1;
    }
    /* Align to 8-byte boundary — kmalloc already gives 16-byte aligned */
    nic->rx_buffer_phys = paging_phys_addr(nic->rx_buffer);
    nic->rx_offset = 0u;

    /* Allocate TX buffers */
    for (uint32_t i = 0u; i < RTL_NUM_TX_DESC; ++i) {
        nic->tx_buffers[i] = (uint8_t*)kmalloc(RTL_TX_BUF_SIZE);
        if (!nic->tx_buffers[i]) {
            serial_puts("[RTL] kmalloc TX buffer ");
            serial_put_u32(i);
            serial_puts(" failed\n");
            return -1;
        }
        nic->tx_buffer_phys[i] = paging_phys_addr(nic->tx_buffers[i]);
    }

    /* Program RX buffer physical address */
    rtl_outl(nic, RTL_RBSTART, nic->rx_buffer_phys);

    /* Configure RX: accept broadcast + physical match */
    rtl_outl(nic, RTL_RCR, RTL_RCR_AB | RTL_RCR_APM);

    /* Set interrupt mask: RX OK + TX OK + errors */
    rtl_outl(nic, RTL_IMR, RTL_ISR_ROK | RTL_ISR_TOK | RTL_ISR_RER | RTL_ISR_TER | RTL_ISR_RX_OVW);

    /* Enable RX and TX */
    rtl_outb(nic, RTL_CR, RTL_CR_TE | RTL_CR_RE);

    /* Register IRQ handler */
    irq_register(nic->irq, rtl8139_irq_handler);

    serial_puts("[RTL] ready\n");
    return 0;
}

int rtl8139_send(rtl8139_t* nic, const uint8_t* data, uint16_t len) {
    if (!nic || !data || len == 0u || len > RTL_TX_BUF_SIZE) return -1;

    uint8_t desc = nic->tx_cur;
    uint32_t tsd_addr = (uint32_t)(RTL_TSD0 + desc * 4u);

    /* Wait for previous TX on this descriptor to complete */
    uint32_t timeout = 0u;
    while (rtl_inl(nic, (uint16_t)tsd_addr) & RTL_TSD_OWN) {
        ++timeout;
        if (timeout > 100000u) {
            serial_puts("[RTL] TX timeout on desc ");
            serial_put_u32(desc);
            serial_puts("\n");
            return -1;
        }
    }

    /* Copy data into TX buffer */
    typedef uint32_t __attribute__((__may_alias__)) u32_alias;
    uint32_t* src = (uint32_t*)data;
    uint32_t* dst = (uint32_t*)nic->tx_buffers[desc];
    uint32_t words = (uint32_t)((len + 3u) / 4u);
    for (uint32_t i = 0u; i < words; ++i) {
        dst[i] = ((u32_alias*)src)[i];
    }

    /* Set TX buffer address (only on first use or if changed) */
    rtl_outl(nic, (uint16_t)(RTL_TSAD0 + desc * 4u), nic->tx_buffer_phys[desc]);

    /* Write TX command: size | OWN bit (0x3000 = no early TX, clear to start) */
    /* OWN bit = 0x2000 — for RTL8139, clear OWN by writing size */
    rtl_outl(nic, (uint16_t)tsd_addr, (uint32_t)len);

    nic->tx_cur = (uint8_t)((nic->tx_cur + 1u) % RTL_NUM_TX_DESC);
    nic->byte_count += len;
    return 0;
}

uint16_t rtl8139_poll(rtl8139_t* nic, uint8_t* out_buf, uint16_t buf_size) {
    if (!nic || !out_buf || buf_size == 0u) return 0u;

    /* Read the packet header at current offset */
    uint16_t rx_offset = (uint16_t)(nic->rx_offset & 0x1FFFu);  /* wrap within 8KB */
    uint16_t* header = (uint16_t*)(nic->rx_buffer + rx_offset);

    /* RTL8139 sets bit 0 of the first word when a packet is received */
    uint16_t rx_status = header[0];
    uint16_t rx_len = header[1];

    if (!(rx_status & RTL_RX_ROK)) {
        return 0u;  /* no packet */
    }

    /* Validate packet */
    uint16_t pkt_len = (uint16_t)(rx_len & 0x3FFFu);  /* lower 14 bits = length */
    if (pkt_len < 4u || pkt_len > 1514u) {
        /* Bad packet, advance and return */
        nic->rx_offset = (nic->rx_offset + rx_len + 4u + 3u) & ~3u;
        /* Update CAPR */
        rtl_outl(nic, RTL_CAPR, (uint32_t)(nic->rx_offset - 0x10u));
        return 0u;
    }

    /* Skip CRC (last 4 bytes) */
    pkt_len = (uint16_t)(pkt_len - 4u);

    /* Copy packet data */
    uint16_t copy_len = pkt_len;
    if (copy_len > buf_size) copy_len = buf_size;

    typedef uint32_t __attribute__((__may_alias__)) u32_alias;
    uint8_t* src = nic->rx_buffer + rx_offset + 4u;  /* skip status + length */
    uint32_t* s = (uint32_t*)src;
    uint32_t* d = (uint32_t*)out_buf;
    uint32_t words = (uint32_t)((copy_len + 3u) / 4u);
    for (uint32_t i = 0u; i < words; ++i) {
        d[i] = ((u32_alias*)s)[i];
    }

    /* Advance ring offset */
    uint16_t next_offset = (uint16_t)(rx_offset + rx_len + 4u + 3u);

    /* RTL8139 rings wrap at 8KB boundary */
    nic->rx_offset = next_offset;
    if (nic->rx_offset >= RTL_RX_BUF_SIZE) {
        nic->rx_offset = 0u;
    }

    /* Update CAPR register: chip uses (rx_offset - 0x10) for the next packet start */
    rtl_outl(nic, RTL_CAPR, (uint32_t)(nic->rx_offset - 0x10u));

    nic->packet_count++;
    return copy_len;
}