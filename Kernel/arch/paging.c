#include "paging.h"
#include "mm_fs.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

#ifdef FREESTANDING



void paging_init_higher_half(void)
{
   
    uint32_t pde_val = page_directory[PAGING_PDE_HIGHER];
    if (pde_val & PAGING_FLAG_PRESENT) {
        serial_puts("[PAGING] higher-half active: kernel at 0xC0000000+\n");
    } else {
        serial_puts("[PAGING] WARNING: PDE[768] not present!\n");
    }
}

uint32_t *paging_create_pd(void)
{
    void *raw = kmalloc(PAGING_PAGE_SIZE * 2u);
    if (!raw) {
        serial_puts("[PAGING] kmalloc failed for process PD\n");
        return NULL;
    }

    uintptr_t raw_addr = (uintptr_t)(raw);
    uintptr_t aligned  = (raw_addr + PAGING_PAGE_SIZE - 1u)
                         & ~(uintptr_t)(PAGING_PAGE_SIZE - 1u);
    uint32_t *pd = (uint32_t *)aligned;


    for (uint32_t i = 0u; i < PAGING_PDE_COUNT; ++i) {
        pd[i] = 0u;
    }

    for (uint32_t i = PAGING_PDE_HIGHER; i < PAGING_PDE_COUNT; ++i) {
        pd[i] = page_directory[i];
    }

    return pd;
}

int paging_map_page(uint32_t *pd, uint32_t vaddr, uint32_t phys,
                     uint32_t flags)
{
    if (!pd) return -1;

    uint32_t pde_idx = vaddr >> 22u;    
    uint32_t pte_idx = (vaddr >> 12u) & 0x3FFu; 

    if (!(pd[pde_idx] & PAGING_FLAG_PRESENT)) {
        void *raw = kmalloc(PAGING_PAGE_SIZE * 2u);
        if (!raw) return -1;

        uintptr_t raw_addr = (uintptr_t)raw;
        uintptr_t aligned  = (raw_addr + PAGING_PAGE_SIZE - 1u)
                             & ~(uintptr_t)(PAGING_PAGE_SIZE - 1u);
        uint32_t *pt = (uint32_t *)aligned;

        for (uint32_t i = 0u; i < PAGING_PTE_COUNT; ++i) {
            pt[i] = 0u;
        }

        uint32_t pt_phys = paging_phys_addr(pt);
        pd[pde_idx] = pt_phys | PAGING_FLAG_PRESENT | PAGING_FLAG_WRITABLE
                               | (flags & PAGING_FLAG_USER);
    }

    uint32_t pt_phys = pd[pde_idx] & 0xFFFFF000u;
    uint32_t *pt     = (uint32_t *)paging_virt_addr(pt_phys);

    pt[pte_idx] = (phys & 0xFFFFF000u) | flags | PAGING_FLAG_PRESENT;

    __asm__ volatile ("invlpg (%0)" : : "r"(vaddr));

    return 0;
}

#endif 