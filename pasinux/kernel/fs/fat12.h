// FAT12 filesystem driver header
#ifndef FAT12_H
#define FAT12_H

#include <stddef.h>
#include <stdint.h>
#include "driver_fs.h"

// File info structure (8.3 filename, attributes, first cluster, size, sector offset)
typedef struct {
    char name[11];          // 8.3 name padded with spaces
    uint8_t attributes;    // attribute byte
    uint16_t first_cluster;// first logical cluster (0 for empty)
    uint32_t size;         // file size in bytes
    uint32_t sector;       // starting sector of first cluster (cached for speed)
} file_info_t;

// FAT12 filesystem context
typedef struct {
    driver_t* block_driver; // underlying block driver
    uint16_t bytes_per_sector; // usually 512
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t num_fats;
    uint16_t sectors_per_fat;
    uint16_t root_entry_count;
    uint32_t root_dir_sectors;
    uint32_t first_data_sector;
    uint32_t total_sectors;
    uint8_t* fat;       // loaded FAT table (size sectors_per_fat * bytes_per_sector)
    uint8_t* root_dir; // loaded root directory (root_entry_count * 32 bytes)
} fat12_fs_t;

// Mount the FAT12 volume using a registered block driver name.
// Returns a pointer to the filesystem context (allocated with kmalloc) or NULL on failure.
fat12_fs_t* fat12_mount(const char* block_driver_name);

// Find a file in the root directory by name (8.3 format, case‑insensitive).
// Returns 0 on success and fills *out_info, -1 if not found.
int fat12_find_file(fat12_fs_t* fs, const char* name, file_info_t* out_info);

// Read a file into a newly allocated buffer. Caller must kfree() the buffer.
// Returns pointer to buffer on success, NULL on failure.
void* fat12_read_file(fat12_fs_t* fs, const file_info_t* info, uint32_t* out_size);

// Write data to a file (overwrites existing size). Returns 0 on success, -1 on error.
int fat12_write_file(fat12_fs_t* fs, const char* name, const void* data, uint32_t size);

// Create a new empty file in the root directory. Returns 0 on success.
int fat12_create_file(fat12_fs_t* fs, const char* name);

// Delete a file/dir from the root directory. Returns 0 on success.
int fat12_delete_file(fat12_fs_t* fs, const char* name);

// Create a new subdirectory in the root directory (real FAT12 dir with ./../).
// Returns 0 on success, -1 on failure. Files-under-folders is out of scope.
int fat12_create_dir(fat12_fs_t* fs, const char* name);

// Unmount and free resources.
void fat12_unmount(fat12_fs_t* fs);

#endif // FAT12_H
