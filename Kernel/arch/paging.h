#ifndef PASINUX_PAGING_H
#define PASINUX_PAGING_H

#include <stdint.h>

#define PAGING_PAGE_SIZE  4096u
#define PAGING_PDE_COUNT  1024u
#define PAGING_PTE_COUNT  1024u

#define PAGING_FLAG_PRESENT  0x001u
#define PAGING_FLAG_WRITABLE 0x002u
#define PAGING_FLAG_USER     0x004u
#define PAGING_FLAG_PS       0x080u

#define PAGING_HIGHER_HALF_OFFSET 0xC0000000u
#define PAGING_PDE_HIGHER 768u

/*
 * User virtual address policy.
 *
 * Keep page zero unmapped and reserve the higher half for the kernel.
 */
#define PAGING_USER_VADDR_MIN 0x00001000u
#define PAGING_USER_VADDR_MAX 0xC0000000u

extern uint32_t page_directory[PAGING_PDE_COUNT];
extern uint32_t page_table[PAGING_PTE_COUNT];

void paging_init_higher_half(void);

static inline uint32_t paging_phys_addr(void *vaddr)
{
    return (uint32_t)(uintptr_t)vaddr -
           PAGING_HIGHER_HALF_OFFSET;
}

static inline void *paging_virt_addr(uint32_t phys)
{
    return (void *)(uintptr_t)(
        phys + PAGING_HIGHER_HALF_OFFSET
    );
}

uint32_t *paging_create_pd(void);

int paging_map_page(uint32_t *pd,
                    uint32_t vaddr,
                    uint32_t phys,
                    uint32_t flags);

/*
 * Validate that a virtual range belongs to the current ring-3 address
 * space and that every page in it is mapped as a user page.
 *
 * writable != 0 additionally requires the pages to be writable.
 */
int paging_validate_user_range(uint32_t vaddr,
                               uint32_t len,
                               int writable);

/*
 * Safe kernel/user copies.
 *
 * These validate the complete range before touching it.
 */
int paging_copy_from_user(void *dst,
                          uint32_t src,
                          uint32_t len);

int paging_copy_to_user(uint32_t dst,
                        const void *src,
                        uint32_t len);

/*
 * Copy a NUL-terminated user string into a kernel buffer.
 *
 * max_len includes the terminating NUL. Returns 0 on success.
 */
int paging_copy_user_string(char *dst,
                            uint32_t src,
                            uint32_t max_len);

#endif