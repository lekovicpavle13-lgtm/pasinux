// Physical frame allocator (bump allocator) for user-mode page frames.
//
// Region: [PMM_BASE, PMM_LIMIT) = physical 2 MiB .. 4 MiB.
//   - Below 2 MiB lives the kernel image, its BSS (incl. the 1 MiB heap
//     arena) and legacy low memory (VGA 0xB8000, EBDA), so it is off-limits.
//   - At 4 MiB the higher-half window ends: entry.asm maps phys 0..4 MiB as
//     one 4 MiB page at 0xC0000000 (PDE[768]) and copies that PDE into every
//     process page directory. Frames below PMM_LIMIT are therefore writable
//     from ring 0 via paging_virt_addr(phys) in ANY address space -- no
//     temporary kernel alias mapping required.
#ifndef PMM_H
#define PMM_H

#include <stddef.h>
#include <stdint.h>

#define PMM_FRAME_SIZE 4096u
#define PMM_BASE       0x00200000u /* 2 MiB */
#define PMM_LIMIT      0x00400000u /* 4 MiB (exclusive) */

/* Reset the allocator. Idempotent; safe to call again. */
void pmm_init(void);

/* Allocate one 4 KiB physical frame. Returns its physical address,
 * or 0 if the region is exhausted. */
uint32_t pmm_alloc_frame(void);

/* Allocate n physically contiguous frames (trivially true for a bump
 * allocator). Returns the physical address of the first frame, or 0. */
uint32_t pmm_alloc_frames(size_t count);

/* Bytes handed out so far / bytes remaining. Diagnostics only. */
uint32_t pmm_bytes_used(void);
uint32_t pmm_bytes_free(void);

#endif // PMM_H
