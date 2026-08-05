#include "gdt.h"

#include <stdint.h>
#include <stddef.h>

/* The actual GDT table and its pointer. */
static gdt_entry_t g_gdt_entries[6];
static gdt_ptr_t   g_gdt_ptr;

void gdt_set_entry(gdt_entry_t *entry, uint32_t base, uint32_t limit,
                   uint8_t access, uint8_t flags)
{
    entry->limit_low       = (uint16_t)(limit & 0xFFFFu);
    entry->base_low         = (uint16_t)(base & 0xFFFFu);
    entry->base_mid         = (uint8_t)((base >> 16) & 0xFFu);
    entry->access          = access;
    entry->flags_limit_high = (uint8_t)((flags & 0xF0u) | ((limit >> 16) & 0x0Fu));
    entry->base_high        = (uint8_t)((base >> 24) & 0xFFu);
}

void gdt_flush(gdt_ptr_t *gdt_ptr, gdt_entry_t *entries, uint16_t num_entries)
{
    (void)entries;

    gdt_ptr->limit = (uint16_t)(num_entries * sizeof(gdt_entry_t) - 1u);
    gdt_ptr->base  = (uint32_t)(uintptr_t)g_gdt_entries;

    __asm__ volatile (
        "lgdt %0\n"
        /* reload data segments */
        "mov  $0x10, %%ax\n"
        "mov  %%ax, %%ds\n"
        "mov  %%ax, %%es\n"
        "mov  %%ax, %%fs\n"
        "mov  %%ax, %%gs\n"
        "mov  %%ax, %%ss\n"
        /* far jump to reload CS */
        "ljmp $0x08, $1f\n"
        "1:\n"
        : : "m"(*gdt_ptr) : "eax", "memory"
    );
}

void gdt_install(void)
{
    /* Build 6-entry GDT.
     * GDT_TSS (slot 5) is filled later by tss_flush(). */

    /* zero the whole table */
    for (uint32_t i = 0u; i < 6u; ++i) {
        g_gdt_entries[i].limit_low       = 0u;
        g_gdt_entries[i].base_low         = 0u;
        g_gdt_entries[i].base_mid         = 0u;
        g_gdt_entries[i].access          = 0u;
        g_gdt_entries[i].flags_limit_high = 0u;
        g_gdt_entries[i].base_high        = 0u;
    }

    /* ring-0 code */
    gdt_set_entry(&g_gdt_entries[GDT_R0_CODE], 0u, 0xFFFFFu,
                  GDT_ACC_R0_CODE, GDT_FLAGS_4GB);
    /* ring-0 data */
    gdt_set_entry(&g_gdt_entries[GDT_R0_DATA], 0u, 0xFFFFFu,
                  GDT_ACC_R0_DATA, GDT_FLAGS_4GB);
    /* ring-3 code */
    gdt_set_entry(&g_gdt_entries[GDT_R3_CODE], 0u, 0xFFFFFu,
                  GDT_ACC_R3_CODE, GDT_FLAGS_4GB);
    /* ring-3 data */
    gdt_set_entry(&g_gdt_entries[GDT_R3_DATA], 0u, 0xFFFFFu,
                  GDT_ACC_R3_DATA, GDT_FLAGS_4GB);

    /* TSS entry is zeroed for now; tss_flush() will set it up */

    gdt_flush(&g_gdt_ptr, g_gdt_entries, 6);

    /* Logging is done by the caller (kmain.c) */
}