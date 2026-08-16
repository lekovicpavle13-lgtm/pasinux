#ifndef ATA_H
#define ATA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Minimal ATA PIO driver for the IDE interface used by QEMU.
 * Supports only the primary master drive (drive 0).
 *
 * API:
 *   int ata_init(void);
 *   int ata_read_sectors(uint8_t drive, uint32_t lba, uint32_t count, void *buf);
 *   int ata_write_sectors(uint8_t drive, uint32_t lba, uint32_t count, const void *buf);
 *
 * Returns 0 on success, negative error code on failure.
 */

int ata_init(void);
int ata_read_sectors(uint8_t drive, uint32_t lba, uint32_t count, void *buf);
int ata_write_sectors(uint8_t drive, uint32_t lba, uint32_t count, const void *buf);

#endif // ATA_H
