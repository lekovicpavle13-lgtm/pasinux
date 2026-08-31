#include "syscall.h"
#include "fat12.h"
#include "keyboard.h"
#include "mm_fs.h"
#include "serial.h"
#include "timer.h"
#include "vga.h"

#include <stddef.h>
#include <stdint.h>

volatile uint32_t g_syscall_ret_esp;
volatile uint32_t g_syscall_ret_eip;
volatile uint32_t g_syscall_did_exit;

/* ---- open(2)-style flags ---- */
#define O_RDONLY 1u
#define O_WRONLY 2u
#define O_RDWR   3u
#define O_CREAT  0x0100u

#define MAX_FDS 8u
#define CONSOLE_READ_FD  0u
#define CONSOLE_WRITE_FD 1u

typedef struct {
    int used;
    fat12_fs_t* fs;
    file_info_t info;
    uint32_t pos;
} fd_entry_t;

static fat12_fs_t* g_sys_fs;
static fd_entry_t g_fds[MAX_FDS];

/* Called once after FAT12 mount so file syscalls know the volume. */
void syscall_set_fs(fat12_fs_t* fs)
{
    g_sys_fs = fs;
}

static void console_putc(char c)
{
    vga_putc(c);
    serial_putc(c); /* mirror so headless QEMU runs see program output */
}

/* Rebuild the user-facing "NAME.EXT" form of an 8.3 directory entry. */
static void info_display_name(const file_info_t* fi, char out[13])
{
    uint32_t p = 0u;
    for (int i = 0; i < 8 && fi->name[i] != ' '; ++i) {
        out[p++] = fi->name[i];
    }
    if (fi->name[8] != ' ') {
        out[p++] = '.';
        for (int i = 8; i < 11 && fi->name[i] != ' '; ++i) {
            out[p++] = fi->name[i];
        }
    }
    out[p] = '\0';
}

uint32_t sys_open(const char* path, uint32_t flags)
{
    if (!g_sys_fs || !path || path[0] == '\0') {
        return 0xFFFFFFFFu;
    }

    file_info_t info;
    int found = (fat12_find_file(g_sys_fs, path, &info) == 0);
    if (!found && (flags & O_CREAT) != 0u) {
        if (fat12_create_file(g_sys_fs, path) != 0 ||
            fat12_find_file(g_sys_fs, path, &info) != 0) {
            return 0xFFFFFFFFu;
        }
        found = 1;
    }
    if (!found) {
        return 0xFFFFFFFFu;
    }

    const uint32_t acc = flags & 0xFFu;
    if (acc != O_RDONLY && acc != O_WRONLY && acc != O_RDWR) {
        return 0xFFFFFFFFu;
    }

    for (uint32_t fd = CONSOLE_WRITE_FD + 1u; fd < MAX_FDS; ++fd) {
        if (!g_fds[fd].used) {
            g_fds[fd].used = 1;
            g_fds[fd].fs = g_sys_fs;
            g_fds[fd].info = info;
            g_fds[fd].pos = 0u;
            return fd;
        }
    }
    return 0xFFFFFFFFu;
}

uint32_t sys_read(uint32_t fd, void* buf, uint32_t count)
{
    if (!buf || count == 0u) {
        return 0u;
    }

    if (fd == CONSOLE_READ_FD) {
        /* Take the keyboard back from the TUI window manager for the
         * duration of one line edit, then hand it back. */
        char line[128];
        keyboard_set_tui_mode(0);
        uint32_t n = keyboard_readline_vga(line, (uint16_t)(count < sizeof(line)
                                                 ? count : sizeof(line)));
        keyboard_set_tui_mode(1);
        for (uint32_t i = 0u; i <= n; ++i) { /* include NUL */
            ((char*)buf)[i] = line[i];
        }
        return n;
    }

    if (fd < MAX_FDS && g_fds[fd].used && g_fds[fd].fs) {
        uint32_t size = 0u;
        void* data = fat12_read_file(g_fds[fd].fs, &g_fds[fd].info, &size);
        if (!data) {
            return 0xFFFFFFFFu;
        }
        uint32_t avail = (g_fds[fd].pos < size) ? size - g_fds[fd].pos : 0u;
        uint32_t n = (count < avail) ? count : avail;
        const uint8_t* src = (const uint8_t*)data + g_fds[fd].pos;
        uint8_t* dst = (uint8_t*)buf;
        for (uint32_t i = 0u; i < n; ++i) {
            dst[i] = src[i];
        }
        g_fds[fd].pos += n;
        kfree(data);
        return n;
    }
    return 0xFFFFFFFFu;
}

uint32_t sys_write(uint32_t fd, const void* buf, uint32_t count)
{
    if (!buf) {
        return 0xFFFFFFFFu;
    }

    if (fd == CONSOLE_WRITE_FD) {
        const char* s = (const char*)buf;
        for (uint32_t i = 0u; i < count; ++i) {
            console_putc(s[i]);
        }
        return count;
    }

    if (fd < MAX_FDS && g_fds[fd].used && g_fds[fd].fs) {
        /* Whole-file truncate-and-write ("save"). */
        char name[13];
        info_display_name(&g_fds[fd].info, name);
        if (fat12_write_file(g_fds[fd].fs, name, buf, count) != 0) {
            return 0xFFFFFFFFu;
        }
        g_fds[fd].info.size = count;
        g_fds[fd].pos = 0u;
        return count;
    }
    return 0xFFFFFFFFu;
}

uint32_t sys_close(uint32_t fd)
{
    if (fd == CONSOLE_READ_FD || fd == CONSOLE_WRITE_FD || fd >= MAX_FDS ||
        !g_fds[fd].used) {
        return 0xFFFFFFFFu;
    }
    g_fds[fd].used = 0;
    g_fds[fd].fs = NULL;
    return 0u;
}

uint32_t sys_seek(uint32_t fd, uint32_t offset, uint32_t whence)
{
    if (fd >= MAX_FDS || !g_fds[fd].used || !g_fds[fd].fs) {
        return 0xFFFFFFFFu;
    }
    uint32_t size = g_fds[fd].info.size;
    uint32_t np;
    switch (whence) {
    case 0u: np = offset; break;                    /* SEEK_SET */
    case 1u: np = g_fds[fd].pos + offset; break;    /* SEEK_CUR */
    case 2u: np = size + offset; break;             /* SEEK_END */
    default: return 0xFFFFFFFFu;
    }
    if (np > size) {
        np = size;
    }
    g_fds[fd].pos = np;
    return np;
}

uint32_t syscall_handler(uint32_t eax, uint32_t ebx, uint32_t ecx,
                         uint32_t edx, uint32_t esi)
{
    (void)esi;

    switch (eax) {
    case SYS_PRINT:
        serial_puts((const char*)(uintptr_t)ebx);
        return 0u;

    case SYS_EXIT:
        serial_puts("[SYS] returning to kernel\n");
        g_syscall_did_exit = 1u;
        return 0u;

    case SYS_GETTIME:
        return timer_ticks();

    case SYS_OPEN:
        return sys_open((const char*)(uintptr_t)ebx, ecx);

    case SYS_READ:
        return sys_read(ebx, (void*)(uintptr_t)ecx, edx);

    case SYS_WRITE:
        return sys_write(ebx, (const void*)(uintptr_t)ecx, edx);

    case SYS_CLOSE:
        return sys_close(ebx);

    case SYS_SEEK:
        return sys_seek(ebx, ecx, edx);

    default:
        serial_puts("[SYS] unknown syscall: ");
        serial_put_u32(eax);
        serial_puts("\n");
        return 0xFFFFFFFFu;
    }
}
