// ATA PIO block driver implementation
#include "ata.h"
#include "io.h"
#include "serial.h"

// Primary ATA bus registers (all on the primary controller).
#define ATA_REG_DATA      0x1F0u
#define ATA_REG_ERROR     0x1F1u  // features on write
#define ATA_REG_SECT_COUNT 0x1F2u
#define ATA_REG_LBA_LO    0x1F3u
#define ATA_REG_LBA_MID   0x1F4u
#define ATA_REG_LBA_HI    0x1F5u
#define ATA_REG_DRIVE     0x1F6u  // drive/head select
#define ATA_REG_COMMAND   0x1F7u  // status on read

#define ATA_CMD_READ_SECTORS  0x20u
#define ATA_CMD_WRITE_SECTORS 0x30u

// Drive select byte: 0xE0 = LBA mode, master, head 0.
#define ATA_DRIVE_MASTER      0xE0u

// Status register flags.
#define ATA_SR_BSY  0x80u  // busy; ignore everything else while set
#define ATA_SR_ERR  0x01u  // error
#define ATA_SR_DF   0x20u  // drive fault
#define ATA_SR_DRQ  0x08u  // data ready (PIO data transfer in progress)

typedef struct {
    uint32_t cur_lba;       // next sector that read()/write() will transfer
} ata_device_t;

static ata_device_t g_ata_dev;
static int g_ata_ready;

// ---------------------------------------------------------------------------
// Low-level port helpers
// ---------------------------------------------------------------------------

static void ata_poll_bsy(void) {
    // Wait until BSY clears. Bound the loop so a dead drive can't hang us.
    for (uint32_t spins = 0u; spins < 1000000u; ++spins) {
        if (!(inb(ATA_REG_COMMAND) & ATA_SR_BSY)) return;
    }
}

static int ata_poll_drq(void) {
    // Wait for DRQ (PIO data ready). Returns 0 on success, -1 on error.
    for (uint32_t spins = 0u; spins < 1000000u; ++spins) {
        uint8_t status = inb(ATA_REG_COMMAND);
        if (status & ATA_SR_BSY) continue;
        if (status & ATA_SR_ERR) return -1;
        if (status & ATA_SR_DRQ) return 0;
    }
    return -1;
}

// Read 256 16-bit words = one 512-byte sector of data.
static void ata_read_256_words(void* buf) {
    uint16_t* dst = (uint16_t*)buf;
    for (uint32_t i = 0u; i < 256u; ++i) {
        dst[i] = (uint16_t)inw(ATA_REG_DATA);
    }
}

// Write 256 16-bit words = one 512-byte sector of data.
static void ata_write_256_words(const void* buf) {
    const uint16_t* src = (const uint16_t*)buf;
    for (uint32_t i = 0u; i < 256u; ++i) {
        outw(ATA_REG_DATA, src[i]);
    }
}

// ---------------------------------------------------------------------------
// Core sector operations (28-bit LBA, polling, no IRQ)
// ---------------------------------------------------------------------------

static int ata_start(uint32_t lba, uint8_t command) {
    ata_poll_bsy();
    outb(ATA_REG_DRIVE, ATA_DRIVE_MASTER | ((lba >> 24) & 0x0Fu));
    outb(ATA_REG_SECT_COUNT, 1u);      // one sector
    outb(ATA_REG_LBA_LO, (uint8_t)(lba & 0xFFu));
    outb(ATA_REG_LBA_MID, (uint8_t)((lba >> 8) & 0xFFu));
    outb(ATA_REG_LBA_HI, (uint8_t)((lba >> 16) & 0xFFu));
    outb(ATA_REG_COMMAND, command);
    return 0;
}

static int ata_read_sector(uint32_t lba, void* buf) {
    if (ata_start(lba, ATA_CMD_READ_SECTORS) != 0) return -1;
    if (ata_poll_drq() != 0) return -1;
    ata_read_256_words(buf);
    ata_poll_bsy();
    return 0;
}

static int ata_write_sector(uint32_t lba, const void* buf) {
    if (ata_start(lba, ATA_CMD_WRITE_SECTORS) != 0) return -1;
    if (ata_poll_drq() != 0) return -1;
    ata_write_256_words(buf);
    ata_poll_bsy();
    return 0;
}

// ---------------------------------------------------------------------------
// driver_ops_t implementation
// ---------------------------------------------------------------------------

static int ata_drv_init(void* device_data) {
    (void)device_data;
    return g_ata_ready ? 0 : -1;
}

static int ata_drv_open(void* device_data, int flags) {
    (void)device_data; (void)flags;
    return g_ata_ready ? 0 : -1;
}

static int ata_drv_close(void* device_data) {
    (void)device_data;
    return 0;
}

static kssize_t ata_drv_read(void* device_data, void* buf, size_t count) {
    ata_device_t* dev = (ata_device_t*)device_data;
    if (!dev || !buf) return -1;
    // Only whole-sector transfers are supported by this driver.
    size_t sector_bytes = ATA_SECTOR_SIZE;
    size_t n_sectors = count / sector_bytes;
    if (n_sectors == 0u) return 0;
    uint8_t* p = (uint8_t*)buf;
    uint32_t lba = dev->cur_lba;
    for (size_t i = 0u; i < n_sectors; ++i) {
        if (ata_read_sector(lba + (uint32_t)i, p) != 0) {
            return (kssize_t)(i * sector_bytes);
        }
        p += sector_bytes;
    }
    dev->cur_lba = lba + (uint32_t)n_sectors;
    return (kssize_t)(n_sectors * sector_bytes);
}

static kssize_t ata_drv_write(void* device_data, const void* buf, size_t count) {
    ata_device_t* dev = (ata_device_t*)device_data;
    if (!dev || !buf) return -1;
    size_t sector_bytes = ATA_SECTOR_SIZE;
    size_t n_sectors = count / sector_bytes;
    if (n_sectors == 0u) return 0;
    const uint8_t* p = (const uint8_t*)buf;
    uint32_t lba = dev->cur_lba;
    for (size_t i = 0u; i < n_sectors; ++i) {
        if (ata_write_sector(lba + (uint32_t)i, p) != 0) {
            return (kssize_t)(i * sector_bytes);
        }
        p += sector_bytes;
    }
    dev->cur_lba = lba + (uint32_t)n_sectors;
    return (kssize_t)(n_sectors * sector_bytes);
}

static int ata_drv_ioctl(void* device_data, unsigned long request, void* arg) {
    ata_device_t* dev = (ata_device_t*)device_data;
    if (!dev || !arg) return -1;
    switch (request) {
    case ATA_IOCTL_SEEK:
        dev->cur_lba = *(const uint32_t*)arg;
        return 0;
    default:
        return -1;
    }
}

static const driver_ops_t ata_ops = {
    ata_drv_init, ata_drv_open, ata_drv_close,
    ata_drv_read, ata_drv_write, ata_drv_ioctl
};

static driver_t ata_driver = {
    "ata", DRIVER_TYPE_BLOCK, &g_ata_dev, &ata_ops, NULL
};

// ---------------------------------------------------------------------------
// Probe + registration
// ---------------------------------------------------------------------------

// Software reset + identify probe. If the drive reports OK, we take it as
// present. A floppy-as-IDE image in QEMU responds to READ SECTORS; probe by
// attempting an actual single-sector read at LBA 0.
static int ata_probe(void) {
    g_ata_dev.cur_lba = 0u;
    uint8_t scratch[ATA_SECTOR_SIZE];
    return (ata_read_sector(0u, scratch) == 0) ? 0 : -1;
}

int ata_init(void) {
    if (ata_probe() != 0) {
        serial_puts("[ATA] no drive responded on primary bus\n");
        return -1;
    }
    driver_register(&ata_driver);
    g_ata_ready = 1;
    serial_puts("[ATA] registered ata as DRIVER_TYPE_BLOCK\n");
    (void)serial_puts;
    return 0;
}