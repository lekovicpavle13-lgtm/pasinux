// Physical frame allocator -- see pmm.h for the region rationale.
//
// A pure bump allocator: frames are never individually reclaimed. User
// programs in pasinux are short-lived foreground apps; their page
// directories and frames stay parked until reboot, which is fine for a
// 2 MiB region serving a notepad-class workload.

#include "pmm.h"

static uint32_t g_next;
static int g_ready;

void pmm_init(void)
{
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
    const uint32_t bytes = (uint32_t)count * PMM_FRAME_SIZE;
    if (bytes > PMM_LIMIT - g_next) {
        return 0u;
    }
    const uint32_t addr = g_next;
    g_next += bytes;
    return addr;
}

uint32_t pmm_bytes_used(void)
{
    return g_next - PMM_BASE;
}

uint32_t pmm_bytes_free(void)
{
    return PMM_LIMIT - g_next;
}
