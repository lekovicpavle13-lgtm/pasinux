#include "syscall.h"
#include "fat12.h"
#include "keyboard.h"
#include "mm_fs.h"
#include "paging.h"
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

#define SYSCALL_STRING_MAX 256u
#define SYSCALL_READLINE_MAX 128u

typedef struct {
    int used;
    fat12_fs_t* fs;
    file_info_t info;
    uint32_t pos;
} fd_entry_t;

static fat12_fs_t* g_sys_fs;
static fd_entry_t g_fds[MAX_FDS];

void syscall_set_fs(fat12_fs_t* fs)
{
    g_sys_fs = fs;
}

static void console_putc(char c)
{
    vga_putc(c);
    serial_putc(c);
}

/*
 * Rebuild the user-facing "NAME.EXT" form of an 8.3 directory entry.
 */
static void info_display_name(const file_info_t* fi,
                              char out[13])
{
    uint32_t p = 0u;

    for (int i = 0;
         i < 8 && fi->name[i] != ' ';
         ++i) {
        out[p++] = fi->name[i];
    }

    if (fi->name[8] != ' ') {
        out[p++] = '.';

        for (int i = 8;
             i < 11 && fi->name[i] != ' ';
             ++i) {
            out[p++] = fi->name[i];
        }
    }

    out[p] = '\0';
}

uint32_t sys_open(const char* path,
                  uint32_t flags)
{
    if (!g_sys_fs ||
        !path ||
        path[0] == '\0') {
        return 0xFFFFFFFFu;
    }

    file_info_t info;

    int found =
        (fat12_find_file(
            g_sys_fs,
            path,
            &info) == 0);

    if (!found &&
        (flags & O_CREAT) != 0u) {

        if (fat12_create_file(
                g_sys_fs,
                path) != 0 ||
            fat12_find_file(
                g_sys_fs,
                path,
                &info) != 0) {
            return 0xFFFFFFFFu;
        }

        found = 1;
    }

    if (!found) {
        return 0xFFFFFFFFu;
    }

    const uint32_t acc =
        flags & 0xFFu;

    if (acc != O_RDONLY &&
        acc != O_WRONLY &&
        acc != O_RDWR) {
        return 0xFFFFFFFFu;
    }

    for (uint32_t fd = CONSOLE_WRITE_FD + 1u;
         fd < MAX_FDS;
         ++fd) {

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

uint32_t sys_read(uint32_t fd,
                  void* buf,
                  uint32_t count)
{
    if (!buf || count == 0u) {
        return 0u;
    }

    /*
     * Console input is first collected into kernel memory.
     * Only after that do we copy it into the user buffer.
     */
    if (fd == CONSOLE_READ_FD) {
        char line[SYSCALL_READLINE_MAX];

        keyboard_set_tui_mode(0);

        uint16_t line_limit =
            (uint16_t)(
                count < sizeof(line)
                    ? count
                    : sizeof(line)
            );

        uint32_t n =
            keyboard_readline_vga(
                line,
                line_limit
            );

        keyboard_set_tui_mode(1);

        /*
         * Preserve the previous API's NUL-terminated result, but make
         * sure the user buffer is large enough for it.
         */
        uint32_t copy_len =
            n + 1u;

        if (copy_len > count) {
            return 0xFFFFFFFFu;
        }

        if (paging_copy_to_user(
                (uint32_t)(uintptr_t)buf,
                line,
                copy_len) != 0) {
            return 0xFFFFFFFFu;
        }

        return n;
    }

    if (fd < MAX_FDS &&
        g_fds[fd].used &&
        g_fds[fd].fs) {

        uint32_t size = 0u;

        void* data =
            fat12_read_file(
                g_fds[fd].fs,
                &g_fds[fd].info,
                &size
            );

        if (!data) {
            return 0xFFFFFFFFu;
        }

        uint32_t avail =
            (g_fds[fd].pos < size)
                ? size - g_fds[fd].pos
                : 0u;

        uint32_t n =
            (count < avail)
                ? count
                : avail;

        const uint8_t* src =
            (const uint8_t *)data +
            g_fds[fd].pos;

        if (paging_copy_to_user(
                (uint32_t)(uintptr_t)buf,
                src,
                n) != 0) {

            kfree(data);
            return 0xFFFFFFFFu;
        }

        g_fds[fd].pos += n;

        kfree(data);

        return n;
    }

    return 0xFFFFFFFFu;
}

uint32_t sys_write(uint32_t fd,
                   const void* buf,
                   uint32_t count)
{
    if (!buf && count != 0u) {
        return 0xFFFFFFFFu;
    }

    if (fd == CONSOLE_WRITE_FD) {
        /*
         * Do not let the kernel's console loop dereference an unchecked
         * user pointer.
         *
         * Copy in bounded chunks so large writes do not require an
         * arbitrarily large kernel buffer.
         */
        uint8_t chunk[128];
        uint32_t pos = 0u;

        while (pos < count) {
            uint32_t n =
                count - pos;

            if (n > sizeof(chunk)) {
                n = sizeof(chunk);
            }

            if (paging_copy_from_user(
                    chunk,
                    (uint32_t)(uintptr_t)buf + pos,
                    n) != 0) {
                return 0xFFFFFFFFu;
            }

            for (uint32_t i = 0u;
                 i < n;
                 ++i) {
                console_putc(
                    (char)chunk[i]
                );
            }

            pos += n;
        }

        return count;
    }

    if (fd < MAX_FDS &&
        g_fds[fd].used &&
        g_fds[fd].fs) {

        /*
         * fat12_write_file() consumes the buffer synchronously.
         * Validate the complete user range before handing it to the
         * filesystem.
         */
        if (paging_validate_user_range(
                (uint32_t)(uintptr_t)buf,
                count,
                0) != 0) {
            return 0xFFFFFFFFu;
        }

        char name[13];

        info_display_name(
            &g_fds[fd].info,
            name
        );

        if (fat12_write_file(
                g_fds[fd].fs,
                name,
                buf,
                count) != 0) {
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
    if (fd == CONSOLE_READ_FD ||
        fd == CONSOLE_WRITE_FD ||
        fd >= MAX_FDS ||
        !g_fds[fd].used) {
        return 0xFFFFFFFFu;
    }

    g_fds[fd].used = 0;
    g_fds[fd].fs = NULL;

    return 0u;
}

uint32_t sys_seek(uint32_t fd,
                  uint32_t offset,
                  uint32_t whence)
{
    if (fd >= MAX_FDS ||
        !g_fds[fd].used ||
        !g_fds[fd].fs) {
        return 0xFFFFFFFFu;
    }

    uint32_t size =
        g_fds[fd].info.size;

    uint32_t np;

    switch (whence) {
    case 0u:
        np = offset;
        break;

    case 1u:
        /*
         * Reject unsigned wraparound.
         */
        if (offset >
            0xFFFFFFFFu - g_fds[fd].pos) {
            return 0xFFFFFFFFu;
        }

        np =
            g_fds[fd].pos + offset;
        break;

    case 2u:
        /*
         * Reject unsigned wraparound.
         */
        if (offset >
            0xFFFFFFFFu - size) {
            return 0xFFFFFFFFu;
        }

        np =
            size + offset;
        break;

    default:
        return 0xFFFFFFFFu;
    }

    if (np > size) {
        np = size;
    }

    g_fds[fd].pos = np;

    return np;
}

uint32_t syscall_handler(uint32_t eax,
                         uint32_t ebx,
                         uint32_t ecx,
                         uint32_t edx,
                         uint32_t esi)
{
    (void)esi;

    switch (eax) {

    case SYS_PRINT: {
        /*
         * SYS_PRINT previously passed the raw user pointer directly to
         * serial_puts(), allowing an invalid pointer to fault the kernel.
         */
        char message[SYSCALL_STRING_MAX];

        if (paging_copy_user_string(
                message,
                ebx,
                sizeof(message)) != 0) {
            return 0xFFFFFFFFu;
        }

        serial_puts(message);

        return 0u;
    }

    case SYS_EXIT:
        serial_puts(
            "[SYS] returning to kernel\n"
        );

        g_syscall_did_exit = 1u;

        return 0u;

    case SYS_GETTIME:
        return timer_ticks();

    case SYS_OPEN: {
        /*
         * Copy the path into kernel memory before FAT12 touches it.
         */
        char path[SYSCALL_STRING_MAX];

        if (paging_copy_user_string(
                path,
                ebx,
                sizeof(path)) != 0) {
            return 0xFFFFFFFFu;
        }

        return sys_open(
            path,
            ecx
        );
    }

    case SYS_READ:
        return sys_read(
            ebx,
            (void *)(uintptr_t)ecx,
            edx
        );

    case SYS_WRITE:
        return sys_write(
            ebx,
            (const void *)(uintptr_t)ecx,
            edx
        );

    case SYS_CLOSE:
        return sys_close(ebx);

    case SYS_SEEK:
        return sys_seek(
            ebx,
            ecx,
            edx
        );

    default:
        serial_puts(
            "[SYS] unknown syscall: "
        );

        serial_put_u32(eax);

        serial_puts("\n");

        return 0xFFFFFFFFu;
    }
}