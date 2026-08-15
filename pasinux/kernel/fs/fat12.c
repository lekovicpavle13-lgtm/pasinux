// FAT12 filesystem driver implementation
#include "fat12.h"
#include "ata.h"
#include "io.h"
#include "mm.h"

// Freestanding string/ctype helpers (no libc in the kernel).
static void* fs_memset(void* dst, int c, size_t n) {
    unsigned char* p = (unsigned char*)dst;
    while (n--) *p++ = (unsigned char)c;
    return dst;
}

static void* fs_memcpy(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    while (n--) *d++ = *s++;
    return dst;
}

static size_t fs_strlen(const char* s) {
    size_t n = 0u;
    while (s[n]) ++n;
    return n;
}

static const char* fs_strchr(const char* s, int c) {
    while (*s) {
        if (*s == (char)c) return s;
        ++s;
    }
    return (c == '\0') ? s : NULL;
}

static int fs_toupper(int c) {
    if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
    return c;
}

static void fat12_format_name(const char* name, char out[11]) {
    fs_memset(out, ' ', 11);
    const char* dot = fs_strchr(name, '.');
    size_t name_len = dot ? (size_t)(dot - name) : fs_strlen(name);
    size_t ext_len = dot ? fs_strlen(dot + 1) : 0;
    if (name_len > 8) name_len = 8;
    if (ext_len > 3) ext_len = 3;
    for (size_t i = 0; i < name_len; ++i) out[i] = (char)fs_toupper((unsigned char)name[i]);
    for (size_t i = 0; i < ext_len; ++i) out[8 + i] = (char)fs_toupper((unsigned char)dot[1 + i]);
}

// The underlying block driver (ATA) is cursor-based: position it at `sector`
// with ATA_IOCTL_SEEK, then transfer whole sectors. Returns 0 on success.
//
// NOTE: sector transfer size is always ATA_SECTOR_SIZE (512), never the
// BPB-parsed bytes_per_sector -- fat12_mount() reads the boot sector BEFORE
// the BPB is parsed (bytes_per_sector is still 0 there), so using it as the
// transfer length would read 0 bytes and leave the boot buffer uninitialized.
static int read_sector(fat12_fs_t* fs, uint32_t sector, uint8_t* buf) {
    if (!fs || !buf) return -1;
    driver_t* drv = fs->block_driver;
    if (drv && drv->ops && drv->ops->ioctl) {
        uint32_t lba = sector;
        if (drv->ops->ioctl(drv->device_data, ATA_IOCTL_SEEK, &lba) != 0) return -1;
    }
    kssize_t res = drv->ops->read(drv->device_data, buf, ATA_SECTOR_SIZE);
    return (res == (kssize_t)ATA_SECTOR_SIZE) ? 0 : -1;
}

// The write/create/delete path is currently stubbed; suppress unused warnings.
static int write_sector(fat12_fs_t* fs, uint32_t sector, const uint8_t* buf) {
    if (!fs || !buf) return -1;
    driver_t* drv = fs->block_driver;
    if (drv && drv->ops && drv->ops->ioctl) {
        uint32_t lba = sector;
        if (drv->ops->ioctl(drv->device_data, ATA_IOCTL_SEEK, &lba) != 0) return -1;
    }
    kssize_t res = drv->ops->write(drv->device_data, buf, ATA_SECTOR_SIZE);
    return (res == (kssize_t)ATA_SECTOR_SIZE) ? 0 : -1;
}

static uint16_t get_fat_entry(fat12_fs_t* fs, uint16_t cluster) {
    // FAT12 entries are 12 bits packed little endian.
    uint32_t offset = (cluster * 3) / 2; // three bytes hold two entries
    uint16_t val = 0;
    if (offset + 1 >= fs->sectors_per_fat * fs->bytes_per_sector) return 0xFFF;
    uint8_t b0 = fs->fat[offset];
    uint8_t b1 = fs->fat[offset + 1];
    if (cluster & 1) {
        // odd cluster: high 12 bits
        val = (b0 >> 4) | (b1 << 4);
    } else {
        // even cluster: low 12 bits
        val = b0 | ((b1 & 0x0F) << 8);
    }
    return val & 0x0FFF;
}

__attribute__((unused)) static void set_fat_entry(fat12_fs_t* fs, uint16_t cluster, uint16_t value) {
    uint32_t offset = (cluster * 3) / 2;
    if (cluster & 1) {
        // odd: high 12 bits
        fs->fat[offset] = (fs->fat[offset] & 0x0F) | ((value & 0x0F) << 4);
        fs->fat[offset + 1] = (value >> 4) & 0xFF;
    } else {
        // even: low 12 bits
        fs->fat[offset] = value & 0xFF;
        fs->fat[offset + 1] = (fs->fat[offset + 1] & 0xF0) | ((value >> 8) & 0x0F);
    }
}

static uint32_t cluster_to_sector(fat12_fs_t* fs, uint16_t cluster) {
    return fs->first_data_sector + (cluster - 2) * fs->sectors_per_cluster;
}

static int find_free_dir_slot(fat12_fs_t* fs) {
    for (uint32_t i = 0; i < fs->root_entry_count; ++i) {
        uint8_t* e = fs->root_dir + i * 32u;
        if (e[0] == 0x00 || e[0] == 0xE5) return (int)(i * 32u);
    }
    return -1;
}

static int flush_root(fat12_fs_t* fs) {
    if (!fs || !fs->root_dir) return -1;
    uint32_t root_start = fs->reserved_sectors + fs->num_fats * fs->sectors_per_fat;
    for (uint32_t i = 0; i < fs->root_dir_sectors; ++i) {
        if (write_sector(fs, root_start + i, fs->root_dir + i * fs->bytes_per_sector) != 0)
            return -1;
    }
    return 0;
}

// Highest legal cluster number on this volume, derived from geometry.
static uint16_t fat12_max_cluster(fat12_fs_t* fs) {
    uint32_t data_sectors = fs->total_sectors - fs->first_data_sector;
    uint32_t data_clusters = data_sectors / (uint32_t)fs->sectors_per_cluster;
    uint32_t mc = 2u + data_clusters;
    return (mc >= 0xFF0u) ? 0xFEFu : (uint16_t)mc; // stay clear of the 0xFF0+ sentinels
}

// Scan the loaded FAT for a free (0x000) cluster at or above `start`.
static uint16_t fat12_find_free_cluster(fat12_fs_t* fs, uint16_t start) {
    uint16_t max = fat12_max_cluster(fs);
    if (start < 2) start = 2;
    for (uint16_t c = start; c < max; ++c) {
        if (get_fat_entry(fs, c) == 0x000) return c;
    }
    return 0; // volume full
}

// Forward declaration (free_chain is defined below but used in the rollback
// path of alloc_chain).
static void fat12_free_chain(fat12_fs_t* fs, uint16_t first);

// Allocate a chain of `n` free clusters, linking them in the FAT.
// Returns the first cluster of the chain, or 0 on failure.
// The on-disk FAT is NOT flushed here; callers must fat12_flush_fat().
static uint16_t fat12_alloc_chain(fat12_fs_t* fs, uint32_t n) {
    if (n == 0) return 0;
    uint16_t first = 0, prev = 0;
    for (uint32_t i = 0; i < n; ++i) {
        uint16_t start = (prev != 0) ? (uint16_t)(prev + 1) : 2;
        uint16_t cl = fat12_find_free_cluster(fs, start);
        if (cl == 0) {
            // roll back anything we already took
            if (first != 0) fat12_free_chain(fs, first);
            return 0;
        }
        if (prev != 0) set_fat_entry(fs, prev, cl);      // link prev -> cl
        else           first = cl;
        set_fat_entry(fs, cl, 0xFFF);                      // EOF for now
        prev = cl;
    }
    return first;
}

// Walk the chain starting at `first`, freeing every cluster (entry -> 0x000).
// The on-disk FAT is NOT flushed here; callers must fat12_flush_fat().
static void fat12_free_chain(fat12_fs_t* fs, uint16_t first) {
    uint16_t c = first;
    while (c >= 2 && c < 0xFF0) {
        uint16_t next = get_fat_entry(fs, c);
        set_fat_entry(fs, c, 0x000);
        c = next;
    }
}

// Write the in-memory FAT to BOTH copies on disk so the mirror stays in sync.
static int fat12_flush_fat(fat12_fs_t* fs) {
    if (!fs || !fs->fat) return -1;
    for (uint32_t fat = 0; fat < fs->num_fats; ++fat) {
        uint32_t base = fs->reserved_sectors + fat * fs->sectors_per_fat;
        for (uint32_t s = 0; s < fs->sectors_per_fat; ++s) {
            if (write_sector(fs, base + s, fs->fat + s * fs->bytes_per_sector) != 0)
                return -1;
        }
    }
    return 0;
}

fat12_fs_t* fat12_mount(const char* block_driver_name) {
    driver_t* drv = driver_lookup(block_driver_name);
    if (!drv || drv->type != DRIVER_TYPE_BLOCK) return NULL;
    // Allocate filesystem context
    fat12_fs_t* fs = (fat12_fs_t*)kmalloc(sizeof(fat12_fs_t));
    if (!fs) return NULL;
    fs_memset(fs, 0, sizeof(*fs));
    fs->block_driver = drv;
    // Read boot sector (sector 0)
    uint8_t* boot = (uint8_t*)kmalloc(512);
    if (!boot) { kfree(fs); return NULL; }
    if (read_sector(fs, 0, boot) != 0) { kfree(boot); kfree(fs); return NULL; }
    // Parse BPB (offsets based on standard FAT12 BPB)
    fs->bytes_per_sector = boot[11] | (boot[12] << 8);
    fs->sectors_per_cluster = boot[13];
    fs->reserved_sectors = boot[14] | (boot[15] << 8);
    fs->num_fats = boot[16];
    fs->root_entry_count = boot[17] | (boot[18] << 8);
    fs->total_sectors = boot[19] | (boot[20] << 8);
    if (fs->total_sectors == 0) {
        // later 32‑bit field
        fs->total_sectors = boot[32] | (boot[33] << 8) | (boot[34] << 16) | (boot[35] << 24);
    }
    fs->sectors_per_fat = boot[22] | (boot[23] << 8);
    // Compute derived values
    fs->root_dir_sectors = ((fs->root_entry_count * 32) + (fs->bytes_per_sector - 1)) / fs->bytes_per_sector;
    fs->first_data_sector = fs->reserved_sectors + (fs->num_fats * fs->sectors_per_fat) + fs->root_dir_sectors;
    // Load FAT table (first FAT)
    uint32_t fat_size = fs->sectors_per_fat * fs->bytes_per_sector;
    fs->fat = (uint8_t*)kmalloc(fat_size);
    if (!fs->fat) { kfree(boot); kfree(fs); return NULL; }
    for (uint32_t i = 0; i < fs->sectors_per_fat; ++i) {
        if (read_sector(fs, fs->reserved_sectors + i, fs->fat + i * fs->bytes_per_sector) != 0) {
            kfree(fs->fat); kfree(boot); kfree(fs); return NULL;
        }
    }
    // Load root directory
    uint32_t root_size = fs->root_dir_sectors * fs->bytes_per_sector;
    fs->root_dir = (uint8_t*)kmalloc(root_size);
    if (!fs->root_dir) { kfree(fs->fat); kfree(boot); kfree(fs); return NULL; }
    uint32_t root_start = fs->reserved_sectors + fs->num_fats * fs->sectors_per_fat;
    for (uint32_t i = 0; i < fs->root_dir_sectors; ++i) {
        if (read_sector(fs, root_start + i, fs->root_dir + i * fs->bytes_per_sector) != 0) {
            kfree(fs->root_dir); kfree(fs->fat); kfree(boot); kfree(fs); return NULL;
        }
    }
    kfree(boot);
    return fs;
}

static int name_match(const char* entry_name, const char* target) {
    // entry_name is 11 bytes (no null). target should be 8.3 formatted (uppercase, spaces).
    for (int i = 0; i < 11; ++i) {
        char a = entry_name[i];
        char b = (i < (int)fs_strlen(target)) ? target[i] : ' ';
        if (fs_toupper((unsigned char)a) != fs_toupper((unsigned char)b)) return 0;
    }
    return 1;
}

int fat12_find_file(fat12_fs_t* fs, const char* name, file_info_t* out_info) {
    if (!fs || !name || !out_info) return -1;
    // Ensure name is in 8.3 padded format (upper case, space‑filled)
    char formatted[11];
    fs_memset(formatted, ' ', sizeof(formatted));
    const char* dot = fs_strchr(name, '.');
    size_t name_len = dot ? (size_t)(dot - name) : fs_strlen(name);
    size_t ext_len = dot ? fs_strlen(dot + 1) : 0;
    if (name_len > 8) name_len = 8;
    if (ext_len > 3) ext_len = 3;
    for (size_t i = 0; i < name_len; ++i) formatted[i] = fs_toupper((unsigned char)name[i]);
    for (size_t i = 0; i < ext_len; ++i) formatted[8 + i] = fs_toupper((unsigned char)dot[1 + i]);

    uint32_t entries = fs->root_entry_count;
    for (uint32_t i = 0; i < entries; ++i) {
        uint8_t* entry = fs->root_dir + i * 32;
        if (entry[0] == 0x00) break; // no more files
        if (entry[0] == 0xE5) continue; // deleted
        if (entry[11] == 0x0F) continue; // long name entry
        if (name_match((char*)entry, formatted)) {
            fs_memcpy(out_info->name, entry, 11);
            out_info->attributes = entry[11];
            out_info->first_cluster = entry[26] | (entry[27] << 8);
            out_info->size = entry[28] | (entry[29] << 8) | (entry[30] << 16) | (entry[31] << 24);
            // compute starting sector for caching (optional)
            out_info->sector = 0; // not used directly
            return 0;
        }
    }
    return -1; // not found
}

void* fat12_read_file(fat12_fs_t* fs, const file_info_t* info, uint32_t* out_size) {
    if (!fs || !info) return NULL;
    uint32_t size = info->size;
    if (out_size) *out_size = size;
    if (size == 0) return NULL;
    uint8_t* buffer = (uint8_t*)kmalloc(size);
    if (!buffer) return NULL;
    uint16_t cluster = info->first_cluster;
    uint32_t bytes_per_cluster = fs->bytes_per_sector * fs->sectors_per_cluster;
    uint32_t offset = 0;
    while (cluster < 0xFF8 && offset < size) {
        uint32_t sector = cluster_to_sector(fs, cluster);
        for (uint8_t s = 0; s < fs->sectors_per_cluster && offset < size; ++s) {
            uint8_t sector_buf[512]; // maximum sector size is 512 for floppy
            if (read_sector(fs, sector + s, sector_buf) != 0) {
                kfree(buffer);
                return NULL;
            }
            uint32_t copy = bytes_per_cluster - (s * fs->bytes_per_sector);
            if (copy > size - offset) copy = size - offset;
            fs_memcpy(buffer + offset, sector_buf, copy);
            offset += copy;
        }
        cluster = get_fat_entry(fs, cluster);
    }
    return buffer;
}

// Locate the root-dir entry offset (in bytes) for a formatted 8.3 name, or -1.
static int find_dir_entry(fat12_fs_t* fs, const char* formatted) {
    for (uint32_t i = 0; i < fs->root_entry_count; ++i) {
        uint8_t* e = fs->root_dir + i * 32;
        if (e[0] == 0x00) break;        // end of active entries
        if (e[0] == 0xE5) continue;     // deleted slot
        if (e[11] == 0x0F) continue;    // LFN entry
        if (name_match((char*)e, formatted)) return (int)(i * 32);
    }
    return -1;
}

// Write `size` bytes from `data` into the clusters starting at `first`,
// zero-padding the final sector. Returns 0 on success.
static int write_data_to_clusters(fat12_fs_t* fs, uint16_t first, const void* data, uint32_t size) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t bpc = fs->bytes_per_sector * fs->sectors_per_cluster;
    uint32_t remaining = size;
    uint16_t cluster = first;
    while (cluster >= 2 && cluster < 0xFF0 && remaining > 0) {
        uint32_t sector = cluster_to_sector(fs, cluster);
        uint32_t chunk = (remaining < bpc) ? remaining : bpc;
        uint32_t written = 0;   // bytes consumed from `p` within this cluster
        for (uint8_t s = 0; s < fs->sectors_per_cluster; ++s) {
            uint8_t sector_buf[512];
            fs_memset(sector_buf, 0, sizeof(sector_buf));
            uint32_t copy = chunk - written;
            if (copy > fs->bytes_per_sector) copy = fs->bytes_per_sector;
            if (copy == 0) break;
            fs_memcpy(sector_buf, p + written, copy);
            if (write_sector(fs, sector + s, sector_buf) != 0) return -1;
            written += copy;
        }
        p += written;
        remaining -= written;
        cluster = get_fat_entry(fs, cluster);
    }
    return 0;
}

int fat12_write_file(fat12_fs_t* fs, const char* name, const void* data, uint32_t size) {
    if (!fs || !name) return -1;
    if (!data && size > 0) return -1;
    char fname[11];
    fat12_format_name(name, fname);

    int off = find_dir_entry(fs, fname);
    int created = 0;
    if (off < 0) {
        off = find_free_dir_slot(fs);
        if (off < 0) return -1;
        created = 1;
    }

    uint8_t* e = fs->root_dir + off;
    uint16_t old_first = e[26] | (e[27] << 8);
    uint16_t first = 0;

    /* Allocate the new chain BEFORE freeing the old one, so a full volume
       leaves the existing file untouched (no partial overwrite). */
    if (size > 0) {
        uint32_t bpc = fs->bytes_per_sector * fs->sectors_per_cluster;
        uint32_t nclusters = (size + bpc - 1u) / bpc;
        first = fat12_alloc_chain(fs, nclusters);
        if (first == 0) return -1; /* no space: old file/data intact in FAT */
        if (write_data_to_clusters(fs, first, data, size) != 0) {
            fat12_free_chain(fs, first);   /* discard failed new chain */
            fat12_flush_fat(fs);
            return -1;
        }
    }

    /* Commit the directory entry's cluster/size fields. If we are replacing an
       existing file, free its old clusters now that the new data is in place. */
    e[26] = (uint8_t)(first & 0xFF);
    e[27] = (uint8_t)((first >> 8) & 0xFF);
    e[28] = (uint8_t)(size & 0xFF);
    e[29] = (uint8_t)((size >> 8) & 0xFF);
    e[30] = (uint8_t)((size >> 16) & 0xFF);
    e[31] = (uint8_t)((size >> 24) & 0xFF);
    if (old_first >= 2 && old_first != first) fat12_free_chain(fs, old_first);

    if (created) {
        fs_memcpy(e, fname, 11);
        e[11] = 0x20;               /* archive (normal file) */
        fs_memset(e + 12, 0, 6);    /* reserved + create time/date */
        fs_memset(e + 18, 0, 8);    /* last-access/off + last write time/date */
        /* e[26..31] (first_cluster + size) already set above. */
    }

    /* Update the directory entry and persist. */
    if (created) {
        fs_memcpy(e, fname, 11);
        e[11] = 0x20;
        fs_memset(e + 12, 0, 14);     /* reserved + time + date */
        fs_memset(e + 26, 0, 6);      /* first_cluster + size */
    }
    e[26] = (uint8_t)(first & 0xFF);
    e[27] = (uint8_t)((first >> 8) & 0xFF);
    e[28] = (uint8_t)(size & 0xFF);
    e[29] = (uint8_t)((size >> 8) & 0xFF);
    e[30] = (uint8_t)((size >> 16) & 0xFF);
    e[31] = (uint8_t)((size >> 24) & 0xFF);

    if (fat12_flush_fat(fs) != 0) return -1;
    if (flush_root(fs) != 0) return -1;
    return 0;
}

int fat12_create_file(fat12_fs_t* fs, const char* name) {
    if (!fs || !name) return -1;
    file_info_t existing;
    if (fat12_find_file(fs, name, &existing) == 0) return -1; /* already exists */
    char fname[11];
    fat12_format_name(name, fname);
    int off = find_free_dir_slot(fs);
    if (off < 0) return -1;
    uint8_t* e = fs->root_dir + off;
    fs_memcpy(e, fname, 11);
    e[11] = 0x20;               /* archive (normal file) */
    fs_memset(e + 12, 0, 20);   /* reserved + time/date + first_cluster=0 + size=0 */
    return flush_root(fs);
}

int fat12_create_dir(fat12_fs_t* fs, const char* name) {
    if (!fs || !name) return -1;
    file_info_t existing;
    if (fat12_find_file(fs, name, &existing) == 0) return -1; /* already exists */
    char fname[11];
    fat12_format_name(name, fname);
    int off = find_free_dir_slot(fs);
    if (off < 0) return -1;

    /* Allocate one cluster for the subdirectory body (`.`/`..`). */
    uint16_t cluster = fat12_alloc_chain(fs, 1);
    if (cluster == 0) return -1;

    /* Build the 32-byte "." and ".." entries. */
    uint8_t body[512];
    fs_memset(body, 0, sizeof(body));
    uint8_t* dot = body;
    fs_memset(dot, ' ', 11); dot[0] = '.'; dot[11] = 0x10; /* directory */
    dot[26] = (uint8_t)(cluster & 0xFF);
    dot[27] = (uint8_t)((cluster >> 8) & 0xFF);
    uint8_t* dotdot = body + 32;
    fs_memset(dotdot, ' ', 11); dotdot[0] = '.'; dotdot[1] = '.'; dotdot[11] = 0x10;
    /* .. points to root (cluster 0). */

    uint32_t sector = cluster_to_sector(fs, cluster);
    if (write_sector(fs, sector, body) != 0) {
        fat12_free_chain(fs, cluster);
        return -1;
    }

    uint8_t* e = fs->root_dir + off;
    fs_memcpy(e, fname, 11);
    e[11] = 0x10;               /* directory attribute */
    fs_memset(e + 12, 0, 14);   /* reserved + time + date */
    fs_memset(e + 26, 0, 6);    /* first_cluster + size (now set below) */
    e[26] = (uint8_t)(cluster & 0xFF);
    e[27] = (uint8_t)((cluster >> 8) & 0xFF);

    if (fat12_flush_fat(fs) != 0) return -1;
    if (flush_root(fs) != 0) return -1;
    return 0;
}

int fat12_delete_file(fat12_fs_t* fs, const char* name) {
    if (!fs || !name) return -1;
    char fname[11];
    fat12_format_name(name, fname);
    int off = find_dir_entry(fs, fname);
    if (off < 0) return -1;
    uint8_t* e = fs->root_dir + off;
    uint16_t first = e[26] | (e[27] << 8);
    if (first >= 2) fat12_free_chain(fs, first);
    e[0] = 0xE5;                /* mark slot deleted */
    if (fat12_flush_fat(fs) != 0) return -1;
    return flush_root(fs);
}

void fat12_unmount(fat12_fs_t* fs) {
    if (!fs) return;
    if (fs->fat) kfree(fs->fat);
    if (fs->root_dir) kfree(fs->root_dir);
    kfree(fs);
}
