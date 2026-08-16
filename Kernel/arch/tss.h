#ifndef TSS_H
#define TSS_H
#include "io.h"

struct tss_entry {
    uint16_t prev_tss;
    uint16_t reserved0;
    uint32_t esp0;           
    uint16_t ss0;            
    uint16_t reserved1;
    uint32_t esp1;           
    uint16_t ss1;
    uint16_t reserved2;
    uint32_t esp2;           
    uint16_t ss2;
    uint16_t reserved3;
    uint32_t cr3;           
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax, ecx, edx, ebx;
    uint32_t esp, ebp, esi, edi;
    uint16_t es, cs, ss, ds, fs, gs;
    uint16_t ldt;
    uint16_t iomap_offset;   
} __attribute__((packed));

void tss_flush(void);

void tss_set_kernel_stack(uint32_t esp0);

#endif 