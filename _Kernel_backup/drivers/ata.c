/*
 * ATA PIO driver (primary master only) for QEMU IDE floppy image.
 * Implements minimal block read/write using 28‑bit LBA.
 *
 * The driver is intentionally simple – it uses busy‑wait polling on the
 * status register (0x1F7) and does not rely on IRQs. All operations are
 * performed in the kernel’s early boot stage before multitasking is
 * active.
 */

#include "ata.h"
#include "io.h"   // inb/outb definitions provided elsewhere in the kernel
#include "driver.h" // driver registration utilities (if needed later)

#define ATA_PRIMARY_IO_BASE   0x1F0U
#define ATA_PRIMARY_CTRL_BASE 0x3F6U

#define ATA_REG_DATA      (ATA_PRIMARY_IO_BASE + 0)   // 16‑bit data port
#define ATA_REG_ERROR     (ATA_PRIMARY_IO_BASE + 1)   // read: error, write: features
#define ATA_REG_FEATURES  (ATA_PRIMARY_IO_BASE + 1)
#define ATA_REG_SECCOUNT0 (ATA_PRIMARY_IO_BASE + 2)
#define ATA_REG_LBA0      (ATA_PRIMARY_IO_BASE + 3)
#define ATA_REG_LBA1      (ATA_PRIMARY_IO_BASE + 4)
#define ATA_REG_LBA2      (ATA_PRIMARY_IO_BASE + 5)
#define ATA_REG_HDDEVSEL  (ATA_PRIMARY_IO_BASE + 6)
#define ATA_REG_COMMAND   (ATA_PRIMARY_IO_BASE + 7)
#define ATA_REG_STATUS    (ATA_PRIMARY_IO_BASE + 7)

/* Status flags */
#define ATA_SR_BSY  0x80U   // Busy
#define ATA_SR_DRDY 0x40U   // Device ready
#define ATA_SR_DF   0x20U   // Device fault
#define ATA_SR_DSC  0x10U   // Device seek complete
#define ATA_SR_DRQ  0x08U   // Data request ready
#define ATA_SR_CORR 0x04U   // Corrected data
#define ATA_SR_IDX  0x02U   // Index (obsolete)
#define ATA_SR_ERR  0x01U   // Error

static bool initialized = false;

int ata_init(void) {
    // For the QEMU IDE floppy there is no actual reset required.
    // We simply mark the driver as ready.
    initialized = true;
    return 0;
}

static int ata_wait_busy(void) {
    // Wait until BSY clears or an error occurs.
    for (int i = 0; i < 1000000; ++i) {
        uint8_t status = inb(ATA_REG_STATUS);
        if ((status & ATA_SR_BSY) == 0) {
            return (status & ATA_SR_ERR) ? -1 : 0;
        }
    }
    return -1; // timeout
}

static int ata_wait_drq(void) {
    for (int i = 0; i < 1000000; ++i) {
        uint8_t status = inb(ATA_REG_STATUS);
        if (status & ATA_SR_ERR) return -1;
        if (status & ATA_SR_DRQ) return 0;
    }
    return -1; // timeout
}

int ata_read_sectors(uint8_t drive, uint32_t lba, uint32_t count, void *buf) {
    if (!initialized) return -1;
    if (drive != 0) return -2; // only primary master supported
    if (count == 0) return -3;

    uint16_t *ptr = (uint16_t *)buf;
    for (uint32_t sector = 0; sector < count; ++sector) {
        if (ata_wait_busy() < 0) return -4;

        // Select drive and LBA (28‑bit mode)
        outb(ATA_REG_HDDEVSEL, 0xE0 | ((lba >> 24) & 0x0F)); // 0xE0 = master
        outb(ATA_REG_SECCOUNT0, 1);
        outb(ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
        outb(ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
        outb(ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
        outb(ATA_REG_COMMAND, 0x20); // READ SECTORS

        if (ata_wait_busy() < 0) return -5;
        if (ata_wait_drq() < 0) return -6;

        // Read 256 WORDs (512 bytes)
        for (int i = 0; i < 256; ++i) {
            *ptr++ = inw(ATA_REG_DATA);
        }
        lba++;
    }
    return 0;
}

int ata_write_sectors(uint8_t drive, uint32_t lba, uint32_t count, const void *buf) {
    if (!initialized) return -1;
    if (drive != 0) return -2;
    if (count == 0) return -3;

    const uint16_t *ptr = (const uint16_t *)buf;
    for (uint32_t sector = 0; sector < count; ++sector) {
        if (ata_wait_busy() < 0) return -4;

        outb(ATA_REG_HDDEVSEL, 0xE0 | ((lba >> 24) & 0x0F));
        outb(ATA_REG_SECCOUNT0, 1);
        outb(ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
        outb(ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
        outb(ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
        outb(ATA_REG_COMMAND, 0x30); // WRITE SECTORS

        if (ata_wait_busy() < 0) return -5;
        if (ata_wait_drq() < 0) return -6;

        // Write 256 WORDs (512 bytes)
        for (int i = 0; i < 256; ++i) {
            outw(ATA_REG_DATA, *ptr++);
        }
        // Flush cache (optional but safe)
        outb(ATA_REG_COMMAND, 0xE7); // CACHE FLUSH
        if (ata_wait_busy() < 0) return -7;
        lba++;
    }
    return 0;
}

/* Optional: register the driver so other subsystems can discover it.
 * This file does not currently call driver_register; the kernel’s
 * initialization code will invoke ata_init() directly.
 */
