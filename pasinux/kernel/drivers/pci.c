#include "pci.h"

#include "io.h"
#include "serial.h"
#include "vga.h"

#include <stdint.h>

#define PCI_MAX_BUS   256u
#define PCI_MAX_SLOT  32u
#define PCI_MAX_FUNC  8u

uint32_t pci_read_config(uint16_t pci_addr, uint8_t offset) {
    uint32_t addr = 0x80000000u
                  | ((uint32_t)PCI_BUS(pci_addr)  << 16)
                  | ((uint32_t)PCI_SLOT(pci_addr) << 11)
                  | ((uint32_t)PCI_FUNC(pci_addr) << 8)
                  | ((uint32_t)(offset & 0xFCu));
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

void pci_write_config(uint16_t pci_addr, uint8_t offset, uint32_t value) {
    uint32_t addr = 0x80000000u
                  | ((uint32_t)PCI_BUS(pci_addr)  << 16)
                  | ((uint32_t)PCI_SLOT(pci_addr) << 11)
                  | ((uint32_t)PCI_FUNC(pci_addr) << 8)
                  | ((uint32_t)(offset & 0xFCu));
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, value);
}

static uint16_t pci_read_word(uint16_t pci_addr, uint8_t offset) {
    uint32_t val = pci_read_config(pci_addr, offset);
    if (offset & 2u) {
        return (uint16_t)((val >> 16) & 0xFFFFu);
    }
    return (uint16_t)(val & 0xFFFFu);
}

uint8_t pci_read_irq(uint16_t pci_addr) {
    uint32_t val = pci_read_config(pci_addr, PCI_INTERRUPT_LINE);
    return (uint8_t)(val & 0xFFu);
}

uint16_t pci_find_device(uint16_t vendor_id, uint16_t device_id) {
    for (uint32_t bus = 0u; bus < PCI_MAX_BUS; ++bus) {
        for (uint32_t slot = 0u; slot < PCI_MAX_SLOT; ++slot) {
            for (uint32_t func = 0u; func < PCI_MAX_FUNC; ++func) {
                uint16_t addr = PCI_ADDR(bus, slot, func);
                uint16_t vendor = pci_read_word(addr, PCI_VENDOR_ID);
                if (vendor == 0xFFFFu) {
                    if (func == 0u) break;
                    continue;
                }
                uint16_t device = pci_read_word(addr, PCI_DEVICE_ID);
                if (vendor == vendor_id && device == device_id) {
                    return addr;
                }
            }
        }
    }
    return PCI_NOT_FOUND;
}

uint32_t pci_read_bar(uint16_t pci_addr, uint8_t bar_index) {
    return pci_read_config(pci_addr, (uint8_t)(PCI_BAR0 + bar_index * 4u));
}

void pci_enable_bus_mastering(uint16_t pci_addr) {
    uint32_t cmd = pci_read_config(pci_addr, PCI_COMMAND);
    cmd |= PCI_CMD_IO_SPACE | PCI_CMD_MEM_SPACE | PCI_CMD_BUS_MASTER;
    pci_write_config(pci_addr, PCI_COMMAND, cmd);
}

static const char* pci_class_name(uint8_t class_code, uint8_t subclass) {
    switch (class_code) {
    case 0x00: return "Unclassified";
    case 0x01: return "Mass storage";
    case 0x02:
        switch (subclass) {
        case 0x00: return "Ethernet";
        case 0x01: return "Token ring";
        default:   return "Network";
        }
    case 0x03: return "Display";
    case 0x04: return "Multimedia";
    case 0x06: return "Bridge";
    case 0x07: return "Comm";
    case 0x08: return "Generic";
    case 0x0C:
        switch (subclass) {
        case 0x03: return "USB";
        case 0x05: return "SMBus";
        default:   return "Serial bus";
        }
    default:   return "Other";
    }
}

void pci_scan_all(void) {
    vga_puts("\n--- PCI devices ---\n");
    serial_puts("[PCI] scanning bus 0...\n");

    uint32_t count = 0u;
    for (uint32_t slot = 0u; slot < PCI_MAX_SLOT; ++slot) {
        uint16_t addr = PCI_ADDR(0, slot, 0);
        uint16_t vendor = pci_read_word(addr, PCI_VENDOR_ID);
        if (vendor == 0xFFFFu) continue;

        uint16_t device = pci_read_word(addr, PCI_DEVICE_ID);
        uint32_t class_raw = pci_read_config(addr, PCI_CLASS_CODE);
        uint8_t class_code = (uint8_t)((class_raw >> 24) & 0xFFu);
        uint8_t subclass   = (uint8_t)((class_raw >> 16) & 0xFFu);
        uint8_t irq = pci_read_irq(addr);

        /* Print to serial */
        serial_puts("  slot ");
        serial_put_u32(slot);
        serial_puts(": vendor=0x");
        serial_put_u32(vendor);
        serial_puts(" device=0x");
        serial_put_u32(device);
        serial_puts(" class=");
        serial_puts(pci_class_name(class_code, subclass));
        serial_puts(" IRQ=");
        serial_put_u32(irq);
        serial_puts("\n");

        /* Print to VGA */
        vga_puts("  ");
        vga_puts(pci_class_name(class_code, subclass));
        vga_puts(" (");
        {
            char hex[10];
            /* Simple hex conversion for vendor */
            const char* digits = "0123456789ABCDEF";
            hex[0] = digits[(vendor >> 12) & 0x0Fu];
            hex[1] = digits[(vendor >> 8) & 0x0Fu];
            hex[2] = digits[(vendor >> 4) & 0x0Fu];
            hex[3] = digits[vendor & 0x0Fu];
            hex[4] = ':';
            hex[5] = digits[(device >> 12) & 0x0Fu];
            hex[6] = digits[(device >> 8) & 0x0Fu];
            hex[7] = digits[(device >> 4) & 0x0Fu];
            hex[8] = digits[device & 0x0Fu];
            hex[9] = '\0';
            vga_puts(hex);
        }
        vga_puts(")\n");
        ++count;
    }

    serial_puts("[PCI] total: ");
    serial_put_u32(count);
    serial_puts(" devices\n");
    vga_puts("--- ");
    {
        char buf[12];
        size_t n = 0;
        uint32_t tmp = count;
        if (tmp == 0u) { buf[n++] = '0'; }
        else {
            char rev[12]; size_t r = 0;
            while (tmp > 0u && r < 11u) { rev[r++] = (char)('0' + (tmp % 10u)); tmp /= 10u; }
            while (r > 0u) { buf[n++] = rev[--r]; }
        }
        buf[n] = '\0';
        vga_puts(buf);
    }
    vga_puts(" devices ---\n");
}