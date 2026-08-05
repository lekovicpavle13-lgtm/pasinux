#ifndef PCI_H
#define PCI_H

#include <stdint.h>

/* PCI config space access ports */
#define PCI_CONFIG_ADDR  0xCF8u
#define PCI_CONFIG_DATA  0xCFCu

/* PCI vendor/device IDs */
#define PCI_VENDOR_REALTEK   0x10ECu
#define PCI_DEVICE_RTL8139   0x8139u

/* PCI class codes */
#define PCI_CLASS_NETWORK    0x02u
#define PCI_CLASS_NET_ETHERNET 0x00u

/* PCI header fields */
#define PCI_VENDOR_ID    0x00u
#define PCI_DEVICE_ID    0x02u
#define PCI_COMMAND      0x04u
#define PCI_STATUS       0x06u
#define PCI_REVISION     0x08u
#define PCI_CLASS_CODE   0x0Au
#define PCI_BAR0         0x10u
#define PCI_BAR1         0x14u
#define PCI_BAR2         0x18u
#define PCI_BAR3         0x1Cu
#define PCI_BAR4         0x20u
#define PCI_BAR5         0x24u
#define PCI_INTERRUPT_LINE 0x3Cu

/* PCI command register bits */
#define PCI_CMD_IO_SPACE     0x0001u
#define PCI_CMD_MEM_SPACE    0x0002u
#define PCI_CMD_BUS_MASTER   0x0004u

/* PCI BAR types */
#define PCI_BAR_IO           0x00000001u
#define PCI_BAR_MEM          0x00000000u
#define PCI_BAR_MEM_64       0x00000004u

/* Returned by pci_find_device when not found */
#define PCI_NOT_FOUND 0xFFFFu

/* Packed bus:slot:func identifier (16-bit) */
#define PCI_ADDR(bus, slot, func)  ((uint16_t)(((bus) & 0xFFu) << 8) | \
                                             (((slot) & 0x1Fu) << 3) | \
                                             ((func) & 0x07u))
#define PCI_BUS(addr)   (((addr) >> 8) & 0xFFu)
#define PCI_SLOT(addr)  (((addr) >> 3) & 0x1Fu)
#define PCI_FUNC(addr)  ((addr) & 0x07u)

/* --- Functions --- */

/* Read/write PCI config space */
uint32_t pci_read_config(uint16_t pci_addr, uint8_t offset);
void     pci_write_config(uint16_t pci_addr, uint8_t offset, uint32_t value);

/* Find a device by vendor/device ID, returns PCI_ADDR or PCI_NOT_FOUND */
uint16_t pci_find_device(uint16_t vendor_id, uint16_t device_id);

/* Read a BAR register — returns raw value (caller must decode I/O vs MEM) */
uint32_t pci_read_bar(uint16_t pci_addr, uint8_t bar_index);

/* Enable bus mastering for DMA */
void pci_enable_bus_mastering(uint16_t pci_addr);

/* Read IRQ line assigned by BIOS */
uint8_t pci_read_irq(uint16_t pci_addr);

/* Full bus scan — enumerates and prints all devices */
void pci_scan_all(void);

#endif /* PCI_H */