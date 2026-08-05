#include "tss.h"
#include "gdt.h"
#include "io.h"



static struct tss_entry g_tss;
static gdt_entry_t g_tss_desc;

void tss_set_kernel_stack(uint32_t esp0)
{
    g_tss.esp0 = esp0;
    g_tss.ss0  = 0x10u; 
}

void tss_flush(void)
{
    uint32_t tss_addr = (uint32_t)(uintptr_t)&g_tss;
    uint32_t tss_limit = (uint32_t)(sizeof(struct tss_entry) - 1u);
    uint32_t i;
    uint8_t *p = (uint8_t*)&g_tss;
    for (i = 0u; i < sizeof(struct tss_entry); ++i) {
        p[i] = 0u;
    }
    g_tss.ss0  = 0x10u;
    g_tss.esp0 = 0u;
    g_tss.iomap_offset = (uint16_t)sizeof(struct tss_entry);
    gdt_set_entry(&g_tss_desc, tss_addr, tss_limit,
                  GDT_ACC_TSS, 0u);

    {
        struct {
            uint16_t limit;
            uint32_t base;
        } __attribute__((packed)) gdtptr;

        __asm__ volatile ("sgdt %0" : "=m"(gdtptr));
        gdt_entry_t *gdt_table = (gdt_entry_t*)(uintptr_t)gdtptr.base;
        gdt_table[GDT_TSS] = g_tss_desc;
    }

    __asm__ volatile ("ltr %%ax" : : "a"((uint16_t)(GDT_TSS * 8u)));

    (void)tss_addr;
    (void)tss_limit;
}