// ATA PIO block driver header
#ifndef ATA_H
#define ATA_H

#include <stddef.h>
#include <stdint.h>
#include "driver_fs.h"

#define ATA_SECTOR_SIZE 512u

// ioctl request: set the current LBA (28-bit). arg points to a uint32_t.
#define ATA_IOCTL_SEEK 0x41544531u  // 'ATE1'

// Register the ATA block driver into the framework. Drive 0 is primary master.
// Returns 0 on success, -1 if no drive responds to the probe.
int ata_init(void);

// The registered driver instance is "ata" (DRIVER_TYPE_BLOCK). Look it up via
// driver_lookup("ata") after ata_init() returns 0.

#endif // ATA_H