// Physical frame allocator -- see pmm.h for the region rationale.
//
// A pure bump allocator: frames are never individually reclaimed. User
// programs in pasinux are short-lived foreground apps; their page
// directories and frames stay parked until reboot.

#include "pmm.h"

#include <stdint.h>

static uint32_t g_next;
static int g_ready;

void pmm_init(void)
{
    /*
     * Initialization is intentionally one-shot.
     *
     * Resetting g_next after frames have already been allocated would
     * cause subsequent allocations to reuse physical frames belonging
     * to existing address spaces.
     */
    if (g_ready) {
        return;
    }

    g_next = PMM_BASE;
    g_ready = 1;
}

uint32_t pmm_alloc_frame(void)
{
    return pmm_alloc_frames(1u);
}

uint32_t pmm_alloc_frames(size_t count)
{
    if (!g_ready || count == 0u) {
        return 0u;
    }

    /*
     * Prevent size_t -> uint32_t truncation from producing an incorrect
     * allocation size on platforms where size_t is wider than 32 bits.
     */
    if (count > (size_t)0xFFFFFFFFu / PMM_FRAME_SIZE) {
        return 0u;
    }

    const uint32_t bytes =
        (uint32_t)count * PMM_FRAME_SIZE;

    /*
     * PMM_LIMIT is exclusive. This subtraction is safe because the
     * allocator invariant keeps g_next inside [PMM_BASE, PMM_LIMIT].
     */
    if (g_next < PMM_BASE || g_next > PMM_LIMIT) {
        return 0u;
    }

    if (bytes > PMM_LIMIT - g_next) {
        return 0u;
    }

    const uint32_t addr = g_next;

    g_next += bytes;

    return addr;
}

uint32_t pmm_bytes_used(void)
{
    if (!g_ready || g_next < PMM_BASE) {
        return 0u;
    }

    return g_next - PMM_BASE;
}

uint32_t pmm_bytes_free(void)
{
    if (!g_ready || g_next > PMM_LIMIT) {
        return 0u;
    }

    return PMM_LIMIT - g_next;
}