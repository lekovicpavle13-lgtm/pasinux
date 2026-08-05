#ifndef GDT_H
#define GDT_H

#include <stdint.h>

/* GDT entries (indices into the table) */
#define GDT_NULL        0u
#define GDT_R0_CODE    1u   /* sel 0x08 */
#define GDT_R0_DATA    2u   /* sel 0x10 */
#define GDT_R3_CODE    3u   /* sel 0x18 | 3 (RPL=3) */
#define GDT_R3_DATA    4u   /* sel 0x20 | 3 (RPL=3) */
#define GDT_TSS        5u   /* sel 0x28 */

/* Access byte helpers */
#define GDT_ACCESS_P       0x80u  /* Present */
#define GDT_ACCESS_DPL0    0x00u  /* DPL 0 */
#define GDT_ACCESS_DPL3    0x60u  /* DPL 3 */
#define GDT_ACCESS_S       0x10u  /* Code/data segment */
#define GDT_ACCESS_EX      0x08u  /* Executable */
#define GDT_ACCESS_DC      0x04u  /* Direction/conforming */
#define GDT_ACCESS_RW      0x02u  /* Readable (code) or writable (data) */

/* Code segment kernel: P|DPL0|S|EX|RW = 0x9A */
#define GDT_ACC_R0_CODE    (GDT_ACCESS_P | GDT_ACCESS_DPL0 | GDT_ACCESS_S | \
                            GDT_ACCESS_EX | GDT_ACCESS_RW)
/* Data segment kernel: P|DPL0|S|RW = 0x92 */
#define GDT_ACC_R0_DATA    (GDT_ACCESS_P | GDT_ACCESS_DPL0 | GDT_ACCESS_S | \
                            GDT_ACCESS_RW)
/* Code segment user: P|DPL3|S|EX|RW = 0xFA */
#define GDT_ACC_R3_CODE    (GDT_ACCESS_P | GDT_ACCESS_DPL3 | GDT_ACCESS_S | \
                            GDT_ACCESS_EX | GDT_ACCESS_RW)
/* Data segment user: P|DPL3|S|RW = 0xF2 */
#define GDT_ACC_R3_DATA    (GDT_ACCESS_P | GDT_ACCESS_DPL3 | GDT_ACCESS_S | \
                            GDT_ACCESS_RW)
/* TSS: P|DPL0|type=0x9 = 0x89 */
#define GDT_ACC_TSS        (GDT_ACCESS_P | GDT_ACCESS_DPL0 | 0x09u)

/* Flags nibble (top 4 bits of byte 6) */
#define GDT_FLAG_GR       0x80u  /* 4 KB granularity */
#define GDT_FLAG_DB       0x40u  /* 32-bit protected mode */
#define GDT_FLAG_LONG     0x20u  /* 64-bit (unused here) */

/* Combined: 4 GB flat 32-bit segments */
#define GDT_FLAGS_4GB     (GDT_FLAG_GR | GDT_FLAG_DB)  /* 0xC0 */

typedef struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  flags_limit_high;  /* top nibble = flags, bottom nibble = limit[19:16] */
    uint8_t  base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) gdt_ptr_t;

/* Encode a flag granular/D-bit segment descriptor (GDT index, base, limit, access, flags). */
void gdt_set_entry(gdt_entry_t *entry, uint32_t base, uint32_t limit,
                   uint8_t access, uint8_t flags);

/* Load the GDT: calls lgdt then reloads all segment registers + far jump. */
void gdt_flush(gdt_ptr_t *gdt_ptr, gdt_entry_t *entries, uint16_t num_entries);

/* Build and install the full 6-entry GDT. */
void gdt_install(void);

#endif /* GDT_H */