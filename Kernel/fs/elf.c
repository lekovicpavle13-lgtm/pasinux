// Minimal ELF32 loader: FAT12 -> validated ELF -> ring-3 execution.
//
// Address-space layout for a loaded program:
//   0x00100000..0xBFBFBFFF user program
//   0xBFBFC000..0xBFBFFFFF user stack, 16 KiB
//   0xC0000000..          kernel higher half
//
// Frames come from the PMM (phys 2..4 MiB). Kernel code accesses those
// frames through the higher-half physical alias.
//
// Execution model: launch_ring3 irets into ring 3; the kernel resumes when
// the program raises int 0x80 with eax=SYS_EXIT. Preemption is suspended
// for the duration.

#include "elf.h"
#include "mm_fs.h"
#include "pmm.h"
#include "paging.h"
#include "sched_fs.h"
#include "serial.h"
#include "tss.h"

#include <stddef.h>
#include <stdint.h>

extern void launch_ring3(void *entry, void *user_stack_top);

#define ELFMAG0 0x7Fu
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

#define ELFCLASS32 1u
#define ELFDATA2LSB 1u
#define EM_386 3u
#define ET_EXEC 2u
#define PT_LOAD 1u

#define PF_X 0x1u
#define PF_W 0x2u
#define PF_R 0x4u

/*
 * User virtual address policy.
 *
 * Keep page zero unmapped and keep the entire higher half reserved for
 * the kernel. The user stack occupies the final 16 KiB immediately below
 * USER_STACK_TOP.
 */
#define USER_VADDR_MIN      0x00001000u
#define USER_VADDR_MAX      0xC0000000u

#define USER_STACK_TOP      0xBFC00000u
#define USER_STACK_PAGES    4u
#define USER_STACK_SIZE     (USER_STACK_PAGES * PAGING_PAGE_SIZE)
#define USER_STACK_BASE     (USER_STACK_TOP - USER_STACK_SIZE)

/*
 * ELF32 executable header.
 */
typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf32_ehdr_t;

/*
 * ELF32 program header.
 */
typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} elf32_phdr_t;

static void *elf_memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    while (n--) {
        *d++ = *s++;
    }

    return dst;
}

static void *elf_memset(void *dst, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dst;

    while (n--) {
        *d++ = (unsigned char)c;
    }

    return dst;
}

/*
 * Return a program header only after validating the complete table entry.
 */
static const elf32_phdr_t *elf_phdr(const uint8_t *file,
                                    uint32_t filesize,
                                    uint16_t idx)
{
    const elf32_ehdr_t *eh = (const elf32_ehdr_t *)file;

    if (idx >= eh->e_phnum) {
        return NULL;
    }

    const uint32_t idx_size =
        (uint32_t)idx * (uint32_t)eh->e_phentsize;

    if ((uint32_t)idx != 0u &&
        idx_size / (uint32_t)eh->e_phentsize != (uint32_t)idx) {
        return NULL;
    }

    if (eh->e_phoff > filesize) {
        return NULL;
    }

    if (idx_size > filesize - eh->e_phoff) {
        return NULL;
    }

    const uint32_t off = eh->e_phoff + idx_size;

    if (eh->e_phentsize < sizeof(elf32_phdr_t)) {
        return NULL;
    }

    if (sizeof(elf32_phdr_t) > filesize - off) {
        return NULL;
    }

    return (const elf32_phdr_t *)(file + off);
}

/*
 * Validate a PT_LOAD segment before allocating anything.
 */
static int elf_validate_segment(const elf32_phdr_t *ph,
                                uint32_t filesize)
{
    if (ph->p_memsz == 0u) {
        return 0;
    }

    /*
     * ELF requires the initialized file data to fit inside the
     * in-memory segment.
     */
    if (ph->p_filesz > ph->p_memsz) {
        serial_puts("[ELF] p_filesz exceeds p_memsz\n");
        return -1;
    }

    /*
     * Validate file range without allowing 32-bit addition overflow.
     */
    if (ph->p_offset > filesize ||
        ph->p_filesz > filesize - ph->p_offset) {
        serial_puts("[ELF] segment extends past end of file\n");
        return -1;
    }

    /*
     * Validate p_vaddr + p_memsz without overflow.
     */
    if (ph->p_vaddr < USER_VADDR_MIN ||
        ph->p_vaddr >= USER_VADDR_MAX ||
        ph->p_memsz > USER_VADDR_MAX - ph->p_vaddr) {
        serial_puts("[ELF] segment outside user address space\n");
        return -1;
    }

    const uint32_t end = ph->p_vaddr + ph->p_memsz;

    /*
     * Do not allow a program segment to overlap the user stack.
     */
    if (ph->p_vaddr < USER_STACK_TOP &&
        end > USER_STACK_BASE) {
        serial_puts("[ELF] segment overlaps user stack\n");
        return -1;
    }

    /*
     * A loadable segment must have a sensible alignment.
     *
     * The loader works at page granularity, so require the standard
     * page-relative ELF relationship.
     */
    if (ph->p_align != 0u && ph->p_align != 1u) {
        if ((ph->p_align & (ph->p_align - 1u)) != 0u) {
            serial_puts("[ELF] invalid segment alignment\n");
            return -1;
        }

        if ((ph->p_offset & (ph->p_align - 1u)) !=
            (ph->p_vaddr & (ph->p_align - 1u))) {
            serial_puts("[ELF] segment offset/address alignment mismatch\n");
            return -1;
        }
    }

    /*
     * This loader cannot represent arbitrary ELF page-sharing semantics
     * safely, so reject a zero-address load and otherwise rely on the
     * overlap validation performed by the caller.
     */
    return 0;
}

/*
 * Return the page-aligned end of a segment.
 */
static int elf_segment_page_range(const elf32_phdr_t *ph,
                                  uint32_t *start,
                                  uint32_t *end)
{
    if (!start || !end || ph->p_memsz == 0u) {
        return -1;
    }

    const uint32_t page_start =
        ph->p_vaddr & ~(PAGING_PAGE_SIZE - 1u);

    const uint32_t segment_end =
        ph->p_vaddr + ph->p_memsz;

    if (segment_end < ph->p_vaddr) {
        return -1;
    }

    uint32_t page_end =
        (segment_end + PAGING_PAGE_SIZE - 1u) &
        ~(PAGING_PAGE_SIZE - 1u);

    if (page_end < segment_end) {
        return -1;
    }

    *start = page_start;
    *end = page_end;

    return 0;
}

/*
 * The current loader allocates one independent contiguous frame range
 * for each PT_LOAD. Therefore two PT_LOADs may not share a virtual page.
 */
static int elf_segments_overlap(const elf32_phdr_t *a,
                                 const elf32_phdr_t *b)
{
    uint32_t a_start, a_end;
    uint32_t b_start, b_end;

    if (elf_segment_page_range(a, &a_start, &a_end) != 0 ||
        elf_segment_page_range(b, &b_start, &b_end) != 0) {
        return 1;
    }

    return a_start < b_end && b_start < a_end;
}

/*
 * Map one PT_LOAD segment into the process address space.
 *
 * The segment permissions are derived from p_flags. The current paging
 * implementation has no NX bit support, so PF_X is validated but cannot
 * yet be enforced at the hardware page level.
 */
static int elf_load_segment(uint32_t *pd,
                            const uint8_t *file,
                            uint32_t filesize,
                            const elf32_phdr_t *ph)
{
    if (!pd || !file || !ph) {
        return -1;
    }

    if (elf_validate_segment(ph, filesize) != 0) {
        return -1;
    }

    if (ph->p_memsz == 0u) {
        return 0;
    }

    uint32_t page_start;
    uint32_t page_end;

    if (elf_segment_page_range(ph, &page_start, &page_end) != 0) {
        serial_puts("[ELF] invalid segment page range\n");
        return -1;
    }

    const uint32_t pages =
        (page_end - page_start) / PAGING_PAGE_SIZE;

    if (pages == 0u) {
        return -1;
    }

    const uint32_t frames = pmm_alloc_frames(pages);

    if (frames == 0u) {
        serial_puts("[ELF] out of physical frames\n");
        return -1;
    }

    uint32_t paging_flags = PAGING_FLAG_USER;

    /*
     * Only PF_W grants user write access.
     *
     * PF_X is currently informational because the paging layer does not
     * expose an NX bit.
     */
    if (ph->p_flags & PF_W) {
        paging_flags |= PAGING_FLAG_WRITABLE;
    }

    for (uint32_t i = 0u; i < pages; ++i) {
        const uint32_t vpage =
            page_start + i * PAGING_PAGE_SIZE;

        const uint32_t phys =
            frames + i * PAGING_PAGE_SIZE;

        if (paging_map_page(pd, vpage, phys, paging_flags) != 0) {
            serial_puts("[ELF] paging_map_page failed\n");
            return -1;
        }
    }

    /*
     * Kernel-side alias of the allocated physical frames.
     */
    uint8_t *dest =
        (uint8_t *)paging_virt_addr(frames);

    const uint32_t off_in_first =
        ph->p_vaddr & (PAGING_PAGE_SIZE - 1u);

    /*
     * Copy initialized data.
     */
    if (ph->p_filesz != 0u) {
        elf_memcpy(dest + off_in_first,
                   file + ph->p_offset,
                   ph->p_filesz);
    }

    /*
     * Zero the BSS/tail of the segment.
     */
    if (ph->p_memsz > ph->p_filesz) {
        elf_memset(dest + off_in_first + ph->p_filesz,
                   0,
                   ph->p_memsz - ph->p_filesz);
    }

    return 0;
}

int user_prog_exec(fat12_fs_t *fs, const char *name)
{
    if (!fs || !name) {
        return -1;
    }

    /*
     * PMM initialization must happen once during kernel startup.
     * Do NOT call pmm_init() here: it resets the bump allocator and can
     * cause a newly launched process to reuse frames belonging to an
     * existing address space.
     */

    file_info_t info;

    if (fat12_find_file(fs, name, &info) != 0) {
        serial_puts("[ELF] program not found: ");
        serial_puts(name);
        serial_puts("\n");
        return -1;
    }

    uint32_t filesize = 0u;

    uint8_t *file =
        (uint8_t *)fat12_read_file(fs, &info, &filesize);

    if (!file) {
        serial_puts("[ELF] read failed\n");
        return -1;
    }

    if (filesize < sizeof(elf32_ehdr_t)) {
        serial_puts("[ELF] file too small\n");
        kfree(file);
        return -1;
    }

    const elf32_ehdr_t *eh =
        (const elf32_ehdr_t *)file;

    if (eh->e_ident[0] != ELFMAG0 ||
        eh->e_ident[1] != ELFMAG1 ||
        eh->e_ident[2] != ELFMAG2 ||
        eh->e_ident[3] != ELFMAG3 ||
        eh->e_ident[4] != ELFCLASS32 ||
        eh->e_ident[5] != ELFDATA2LSB ||
        eh->e_machine != EM_386 ||
        eh->e_type != ET_EXEC ||
        eh->e_version != 1u) {
        serial_puts("[ELF] not a 32-bit x86 ELF executable\n");
        kfree(file);
        return -1;
    }

    if (eh->e_ehsize < sizeof(elf32_ehdr_t) ||
        eh->e_phentsize < sizeof(elf32_phdr_t) ||
        eh->e_phnum == 0u) {
        serial_puts("[ELF] bad ELF header sizes\n");
        kfree(file);
        return -1;
    }

    /*
     * Validate the complete program-header table without allowing
     * arithmetic overflow.
     */
    if (eh->e_phoff > filesize) {
        serial_puts("[ELF] program-header table outside file\n");
        kfree(file);
        return -1;
    }

    const uint32_t ph_table_size =
        (uint32_t)eh->e_phnum * (uint32_t)eh->e_phentsize;

    if (eh->e_phnum != 0u &&
        ph_table_size / eh->e_phnum != eh->e_phentsize) {
        serial_puts("[ELF] program-header table overflow\n");
        kfree(file);
        return -1;
    }

    if (ph_table_size > filesize - eh->e_phoff) {
        serial_puts("[ELF] program-header table truncated\n");
        kfree(file);
        return -1;
    }

    /*
     * First pass:
     * - validate every PT_LOAD
     * - reject overlapping load pages
     * - determine whether the entry point belongs to an executable
     *   PT_LOAD segment.
     */
    int entry_in_executable_segment = 0;
    uint32_t load_count = 0u;

    for (uint16_t i = 0u; i < eh->e_phnum; ++i) {
        const elf32_phdr_t *ph =
            elf_phdr(file, filesize, i);

        if (!ph) {
            serial_puts("[ELF] invalid program header\n");
            kfree(file);
            return -1;
        }

        if (ph->p_type != PT_LOAD) {
            continue;
        }

        if (elf_validate_segment(ph, filesize) != 0) {
            kfree(file);
            return -1;
        }

        if (ph->p_memsz == 0u) {
            continue;
        }

        ++load_count;

        /*
         * Entry must be inside an executable PT_LOAD, not merely inside
         * some arbitrary user address.
         */
        if (eh->e_entry >= ph->p_vaddr &&
            eh->e_entry - ph->p_vaddr < ph->p_memsz &&
            (ph->p_flags & PF_X)) {
            entry_in_executable_segment = 1;
        }

        for (uint16_t j = 0u; j < i; ++j) {
            const elf32_phdr_t *prev =
                elf_phdr(file, filesize, j);

            if (!prev || prev->p_type != PT_LOAD ||
                prev->p_memsz == 0u) {
                continue;
            }

            if (elf_segments_overlap(ph, prev)) {
                serial_puts("[ELF] overlapping PT_LOAD pages unsupported\n");
                kfree(file);
                return -1;
            }
        }
    }

    if (load_count == 0u) {
        serial_puts("[ELF] ELF contains no loadable segments\n");
        kfree(file);
        return -1;
    }

    /*
     * Reject an entry point outside the loaded executable image.
     */
    if (eh->e_entry < USER_VADDR_MIN ||
        eh->e_entry >= USER_STACK_BASE ||
        !entry_in_executable_segment) {
        serial_puts("[ELF] entry point is not in an executable segment\n");
        kfree(file);
        return -1;
    }

    const uint32_t entry_vaddr = eh->e_entry;

    uint32_t *pd = paging_create_pd();

    if (!pd) {
        kfree(file);
        return -1;
    }

    /*
     * Second pass: load PT_LOAD segments.
     */
    for (uint16_t i = 0u; i < eh->e_phnum; ++i) {
        const elf32_phdr_t *ph =
            elf_phdr(file, filesize, i);

        if (!ph || ph->p_type != PT_LOAD) {
            continue;
        }

        if (elf_load_segment(pd, file, filesize, ph) != 0) {
            serial_puts("[ELF] failed to load segment\n");
            kfree(file);
            return -1;
        }
    }

    /*
     * User stack.
     */
    const uint32_t sframes =
        pmm_alloc_frames(USER_STACK_PAGES);

    if (sframes == 0u) {
        serial_puts("[ELF] no frames for user stack\n");
        kfree(file);
        return -1;
    }

    for (uint32_t i = 0u; i < USER_STACK_PAGES; ++i) {
        const uint32_t vaddr =
            USER_STACK_BASE + i * PAGING_PAGE_SIZE;

        const uint32_t phys =
            sframes + i * PAGING_PAGE_SIZE;

        if (paging_map_page(pd,
                            vaddr,
                            phys,
                            PAGING_FLAG_WRITABLE |
                            PAGING_FLAG_USER) != 0) {
            serial_puts("[ELF] stack map failed\n");
            kfree(file);
            return -1;
        }
    }

    /*
     * Ring-0 trap stack used when the CPU enters the kernel from ring 3.
     */
    uint8_t *kstack =
        (uint8_t *)kmalloc(4096u);

    if (!kstack) {
        serial_puts("[ELF] kmalloc kernel stack failed\n");
        kfree(file);
        return -1;
    }

    tss_set_kernel_stack(
        (uint32_t)(uintptr_t)kstack + 4096u
    );

    serial_puts("[ELF] launching ");
    serial_puts(name);
    serial_puts(" entry=0x");
    serial_put_u32(entry_vaddr);
    serial_puts(" cr3=0x");
    serial_put_u32(paging_phys_addr(pd));
    serial_putc('\n');

    kfree(file);

    sched_fs_preempt_enable(0);

    /*
     * Switch into the program's address space before entering ring 3.
     */
    __asm__ volatile (
        "mov %0, %%cr3"
        :
        : "r"(paging_phys_addr(pd))
        : "memory"
    );

    launch_ring3(
        (void *)(uintptr_t)entry_vaddr,
        (void *)(uintptr_t)USER_STACK_TOP
    );

    /*
     * SYS_EXIT returns here in ring 0 while still using the user's CR3.
     */
    sched_fs_restore_kernel_cr3();

    kfree(kstack);

    sched_fs_preempt_enable(1);

    serial_puts("[ELF] user program exited\n");

    return 0;
}