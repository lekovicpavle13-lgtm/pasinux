#include "paging.h"
#include "mm_fs.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

#ifdef FREESTANDING

static uint32_t paging_current_cr3(void)
{
    uint32_t cr3;

    __asm__ volatile (
        "mov %%cr3, %0"
        : "=r"(cr3)
    );

    return cr3 & 0xFFFFF000u;
}

static uint32_t *paging_current_pd(void)
{
    uint32_t cr3 = paging_current_cr3();

    if (cr3 == 0u) {
        return NULL;
    }

    return (uint32_t *)paging_virt_addr(cr3);
}

static int paging_user_page_accessible(uint32_t *pd,
                                       uint32_t vaddr,
                                       int writable)
{
    if (!pd) {
        return 0;
    }

    uint32_t pde_idx = vaddr >> 22u;
    uint32_t pte_idx =
        (vaddr >> 12u) & 0x3FFu;

    uint32_t pde = pd[pde_idx];

    if (!(pde & PAGING_FLAG_PRESENT) ||
        !(pde & PAGING_FLAG_USER)) {
        return 0;
    }

    /*
     * A user-accessible 4 MiB page would bypass the normal PTE checks.
     * The kernel currently has no user-accessible large pages, so reject
     * them here.
     */
    if (pde & PAGING_FLAG_PS) {
        return 0;
    }

    if (writable && !(pde & PAGING_FLAG_WRITABLE)) {
        return 0;
    }

    uint32_t pt_phys =
        pde & 0xFFFFF000u;

    if (pt_phys == 0u) {
        return 0;
    }

    uint32_t *pt =
        (uint32_t *)paging_virt_addr(pt_phys);

    uint32_t pte = pt[pte_idx];

    if (!(pte & PAGING_FLAG_PRESENT) ||
        !(pte & PAGING_FLAG_USER)) {
        return 0;
    }

    if (writable && !(pte & PAGING_FLAG_WRITABLE)) {
        return 0;
    }

    return 1;
}

void paging_init_higher_half(void)
{
    uint32_t pde_val =
        page_directory[PAGING_PDE_HIGHER];

    if (pde_val & PAGING_FLAG_PRESENT) {
        serial_puts(
            "[PAGING] higher-half active: "
            "kernel at 0xC0000000+\n"
        );
    } else {
        serial_puts(
            "[PAGING] WARNING: PDE[768] not present!\n"
        );
    }
}

uint32_t *paging_create_pd(void)
{
    /*
     * Allocate enough space for alignment because kmalloc() does not
     * guarantee a 4 KiB-aligned result.
     */
    void *raw =
        kmalloc(PAGING_PAGE_SIZE * 2u);

    if (!raw) {
        serial_puts(
            "[PAGING] kmalloc failed for process PD\n"
        );
        return NULL;
    }

    uintptr_t raw_addr =
        (uintptr_t)raw;

    uintptr_t aligned =
        (raw_addr + PAGING_PAGE_SIZE - 1u) &
        ~(uintptr_t)(PAGING_PAGE_SIZE - 1u);

    uint32_t *pd =
        (uint32_t *)aligned;

    for (uint32_t i = 0u;
         i < PAGING_PDE_COUNT;
         ++i) {
        pd[i] = 0u;
    }

    /*
     * Copy the kernel's higher-half mappings but explicitly clear USER.
     */
    for (uint32_t i = PAGING_PDE_HIGHER;
         i < PAGING_PDE_COUNT;
         ++i) {
        pd[i] =
            page_directory[i] &
            ~PAGING_FLAG_USER;
    }

    return pd;
}

int paging_map_page(uint32_t *pd,
                    uint32_t vaddr,
                    uint32_t phys,
                    uint32_t flags)
{
    if (!pd) {
        return -1;
    }

    if ((vaddr & (PAGING_PAGE_SIZE - 1u)) != 0u ||
        (phys  & (PAGING_PAGE_SIZE - 1u)) != 0u) {
        return -1;
    }

    if (flags & ~(PAGING_FLAG_PRESENT |
                  PAGING_FLAG_WRITABLE |
                  PAGING_FLAG_USER)) {
        return -1;
    }

    flags &= ~PAGING_FLAG_PRESENT;

    uint32_t pde_idx =
        vaddr >> 22u;

    uint32_t pte_idx =
        (vaddr >> 12u) & 0x3FFu;

    uint32_t pde =
        pd[pde_idx];

    /*
     * A PS PDE is a 4 MiB mapping, not a page-table pointer.
     */
    if (pde & PAGING_FLAG_PS) {
        return -1;
    }

    if (!(pde & PAGING_FLAG_PRESENT)) {
        void *raw =
            kmalloc(PAGING_PAGE_SIZE * 2u);

        if (!raw) {
            return -1;
        }

        uintptr_t raw_addr =
            (uintptr_t)raw;

        uintptr_t aligned =
            (raw_addr + PAGING_PAGE_SIZE - 1u) &
            ~(uintptr_t)(PAGING_PAGE_SIZE - 1u);

        uint32_t *pt =
            (uint32_t *)aligned;

        for (uint32_t i = 0u;
             i < PAGING_PTE_COUNT;
             ++i) {
            pt[i] = 0u;
        }

        uint32_t pt_phys =
            paging_phys_addr(pt);

        pd[pde_idx] =
            pt_phys |
            PAGING_FLAG_PRESENT |
            PAGING_FLAG_WRITABLE |
            (flags & PAGING_FLAG_USER);

        pde =
            pd[pde_idx];
    } else {
        /*
         * If an existing page table is already present, a user mapping
         * requires the PDE itself to be user-accessible.
         */
        if ((flags & PAGING_FLAG_USER) &&
            !(pde & PAGING_FLAG_USER)) {
            return -1;
        }
    }

    if (pde & PAGING_FLAG_PS) {
        return -1;
    }

    uint32_t pt_phys =
        pde & 0xFFFFF000u;

    if (pt_phys == 0u) {
        return -1;
    }

    uint32_t *pt =
        (uint32_t *)paging_virt_addr(pt_phys);

    pt[pte_idx] =
        (phys & 0xFFFFF000u) |
        flags |
        PAGING_FLAG_PRESENT;

    __asm__ volatile (
        "invlpg (%0)"
        :
        : "r"(vaddr)
        : "memory"
    );

    return 0;
}

int paging_validate_user_range(uint32_t vaddr,
                               uint32_t len,
                               int writable)
{
    if (len == 0u) {
        return 0;
    }

    /*
     * Prevent wraparound when calculating the exclusive end address.
     */
    if (vaddr < PAGING_USER_VADDR_MIN ||
        vaddr >= PAGING_USER_VADDR_MAX ||
        len > PAGING_USER_VADDR_MAX - vaddr) {
        return -1;
    }

    uint32_t end =
        vaddr + len;

    uint32_t page =
        vaddr & ~(PAGING_PAGE_SIZE - 1u);

    uint32_t last =
        (end - 1u) &
        ~(PAGING_PAGE_SIZE - 1u);

    uint32_t *pd =
        paging_current_pd();

    if (!pd) {
        return -1;
    }

    for (;;) {
        if (!paging_user_page_accessible(
                pd,
                page,
                writable)) {
            return -1;
        }

        if (page == last) {
            break;
        }

        page += PAGING_PAGE_SIZE;
    }

    return 0;
}

int paging_copy_from_user(void *dst,
                          uint32_t src,
                          uint32_t len)
{
    if (!dst && len != 0u) {
        return -1;
    }

    if (paging_validate_user_range(
            src,
            len,
            0) != 0) {
        return -1;
    }

    uint8_t *d =
        (uint8_t *)dst;

    const uint8_t *s =
        (const uint8_t *)(uintptr_t)src;

    for (uint32_t i = 0u;
         i < len;
         ++i) {
        d[i] = s[i];
    }

    return 0;
}

int paging_copy_to_user(uint32_t dst,
                        const void *src,
                        uint32_t len)
{
    if (!src && len != 0u) {
        return -1;
    }

    if (paging_validate_user_range(
            dst,
            len,
            1) != 0) {
        return -1;
    }

    uint8_t *d =
        (uint8_t *)(uintptr_t)dst;

    const uint8_t *s =
        (const uint8_t *)src;

    for (uint32_t i = 0u;
         i < len;
         ++i) {
        d[i] = s[i];
    }

    return 0;
}

int paging_copy_user_string(char *dst,
                            uint32_t src,
                            uint32_t max_len)
{
    if (!dst ||
        max_len == 0u) {
        return -1;
    }

    for (uint32_t i = 0u;
         i < max_len;
         ++i) {

        if (paging_validate_user_range(
                src + i,
                1u,
                0) != 0) {
            return -1;
        }

        char c =
            *(const char *)(uintptr_t)(src + i);

        dst[i] = c;

        if (c == '\0') {
            return 0;
        }
    }

    /*
     * No terminating NUL within the allowed length.
     */
    dst[max_len - 1u] = '\0';

    return -1;
}

#endif