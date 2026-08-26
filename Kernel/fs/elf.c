// Minimal ELF32 loader: FAT12 -> validated ELF -> ring-3 execution.
//
// Address-space layout for a loaded program:
//   0x08048000..          PT_LOAD segments (vaddrs come from the ELF)
//   0xBFBFC000..0xBFC00000 user stack, 16 KiB, grows down
//
// Frames come from the PMM (phys 2..4 MiB). Because every page directory
// maps phys 0..4 MiB at 0xC0000000 (entry.asm PDE[768], copied by
// paging_create_pd), the kernel fills those frames through the higher-half
// alias -- no scratch mapping needed.
//
// Execution model: launch_ring3 irets into ring 3; the kernel resumes when
// the program raises int 0x80 with eax=SYS_EXIT. Preemption is suspended
// for the duration so the scheduler never saves the ring-3 frame into a
// kernel process slot.

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

/* User stack: 16 KiB ending just below the higher-half window. */
#define USER_STACK_TOP    0xBFC00000u
#define USER_STACK_PAGES  4u

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

static const elf32_phdr_t *elf_phdr(const uint8_t *file, uint32_t filesize,
                                    uint16_t idx)
{
    const elf32_ehdr_t *eh = (const elf32_ehdr_t *)file;
    if (idx >= eh->e_phnum) {
        return NULL;
    }
    const uint32_t off = eh->e_phoff + (uint32_t)idx * eh->e_phentsize;
    if (off + sizeof(elf32_phdr_t) > filesize) {
        return NULL;
    }
    return (const elf32_phdr_t *)(file + off);
}

/* Map `pages` frames at vstart and copy one PT_LOAD segment into them.
 * Returns 0 on success, -1 on failure. */
static int elf_load_segment(uint32_t *pd, const uint8_t *file,
                            uint32_t filesize, const elf32_phdr_t *ph)
{
    if (ph->p_memsz == 0u) {
        return 0;
    }
    const uint32_t off_in_first = ph->p_vaddr & (PAGING_PAGE_SIZE - 1u);
    const uint32_t span = off_in_first + ph->p_memsz;
    const uint32_t pages = (span + PAGING_PAGE_SIZE - 1u) / PAGING_PAGE_SIZE;

    if (ph->p_offset + ph->p_filesz > filesize) {
        serial_puts("[ELF] segment extends past end of file\n");
        return -1;
    }

    const uint32_t frames = pmm_alloc_frames(pages);
    if (frames == 0u) {
        serial_puts("[ELF] out of physical frames\n");
        return -1;
    }

    for (uint32_t i = 0u; i < pages; ++i) {
        const uint32_t vpage = (ph->p_vaddr & ~(PAGING_PAGE_SIZE - 1u))
                               + i * PAGING_PAGE_SIZE;
        if (paging_map_page(pd, vpage, frames + i * PAGING_PAGE_SIZE,
                            PAGING_FLAG_WRITABLE | PAGING_FLAG_USER) != 0) {
            serial_puts("[ELF] paging_map_page failed\n");
            return -1;
        }
    }

    /* Kernel-side alias of the frame run (valid in any address space). */
    uint8_t *dest = (uint8_t *)paging_virt_addr(frames);

    if (ph->p_filesz != 0u) {
        elf_memcpy(dest + off_in_first, file + ph->p_offset, ph->p_filesz);
    }
    if (ph->p_memsz > ph->p_filesz) {
        elf_memset(dest + off_in_first + ph->p_filesz, 0,
                   ph->p_memsz - ph->p_filesz); /* BSS */
    }
    return 0;
}

int user_prog_exec(fat12_fs_t* fs, const char* name)
{
    pmm_init(); /* idempotent */

    file_info_t info;
    if (fat12_find_file(fs, name, &info) != 0) {
        serial_puts("[ELF] program not found: ");
        serial_puts(name);
        serial_puts("\n");
        return -1;
    }

    uint32_t filesize = 0u;
    uint8_t *file = (uint8_t *)fat12_read_file(fs, &info, &filesize);
    if (!file) {
        serial_puts("[ELF] read failed\n");
        return -1;
    }

    if (filesize < sizeof(elf32_ehdr_t)) {
        serial_puts("[ELF] file too small\n");
        kfree(file);
        return -1;
    }
    const elf32_ehdr_t *eh = (const elf32_ehdr_t *)file;
    if (eh->e_ident[0] != ELFMAG0 || eh->e_ident[1] != ELFMAG1 ||
        eh->e_ident[2] != ELFMAG2 || eh->e_ident[3] != ELFMAG3 ||
        eh->e_ident[4] != ELFCLASS32 || eh->e_ident[5] != ELFDATA2LSB ||
        eh->e_machine != EM_386 || eh->e_type != ET_EXEC ||
        eh->e_version != 1u) {
        serial_puts("[ELF] not a 32-bit x86 ELF executable\n");
        kfree(file);
        return -1;
    }
    if (eh->e_phentsize < sizeof(elf32_phdr_t) || eh->e_phnum == 0u ||
        eh->e_entry == 0u) {
        serial_puts("[ELF] bad ELF header fields\n");
        kfree(file);
        return -1;
    }

    const uint32_t entry_vaddr = eh->e_entry;

    uint32_t *pd = paging_create_pd();
    if (!pd) {
        kfree(file);
        return -1;
    }

    for (uint16_t i = 0u; i < eh->e_phnum; ++i) {
        const elf32_phdr_t *ph = elf_phdr(file, filesize, i);
        if (!ph || ph->p_type != PT_LOAD) {
            continue;
        }
        if (elf_load_segment(pd, file, filesize, ph) != 0) {
            kfree(file);
            return -1; /* PD/frames intentionally left parked */
        }
    }

    /* User stack. */
    const uint32_t sframes = pmm_alloc_frames(USER_STACK_PAGES);
    if (sframes == 0u) {
        serial_puts("[ELF] no frames for user stack\n");
        kfree(file);
        return -1;
    }
    for (uint32_t i = 0u; i < USER_STACK_PAGES; ++i) {
        if (paging_map_page(pd, USER_STACK_TOP - (i + 1u) * PAGING_PAGE_SIZE,
                            sframes + i * PAGING_PAGE_SIZE,
                            PAGING_FLAG_WRITABLE | PAGING_FLAG_USER) != 0) {
            serial_puts("[ELF] stack map failed\n");
            kfree(file);
            return -1;
        }
    }

    /* Ring-0 trap stack for while the CPU is in ring 3. */
    uint8_t *kstack = (uint8_t *)kmalloc(4096u);
    if (!kstack) {
        serial_puts("[ELF] kmalloc kernel stack failed\n");
        kfree(file);
        return -1;
    }
    tss_set_kernel_stack((uint32_t)(uintptr_t)kstack + 4096u);

    serial_puts("[ELF] launching ");
    serial_puts(name);
    serial_puts(" entry=0x");
    serial_put_u32(entry_vaddr);
    serial_puts(" cr3=0x");
    serial_put_u32(paging_phys_addr(pd));
    serial_putc('\n');

    kfree(file);

    sched_fs_preempt_enable(0);

    /* Switch into the program's fresh address space before iret'ing to
     * ring 3; sched_fs_restore_kernel_cr3() undoes this after SYS_EXIT. */
    __asm__ volatile ("mov %0, %%cr3" : : "r"(paging_phys_addr(pd)));

    launch_ring3((void *)(uintptr_t)entry_vaddr,
                 (void *)(uintptr_t)USER_STACK_TOP);
    /* Back in ring 0 after SYS_EXIT -- still on the user's CR3. */
    sched_fs_restore_kernel_cr3();
    kfree(kstack);
    sched_fs_preempt_enable(1);
    serial_puts("[ELF] user program exited\n");
    return 0;
}
