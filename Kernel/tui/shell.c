#include "tui_wm.h"
#include "tui_core.h"
#include "widgets.h"
#include "fat12.h"
#include "http.h"
#include "serial.h"
#include "mm_fs.h"
#include "rtc.h"
#include "sched_fs.h"
#include "timer.h"

static fat12_fs_t* g_fs = NULL;
static tui_win_t* g_shell_win = NULL;
static tui_readline_t g_shell_readline;
static char g_shell_output[2000];
static int g_shell_output_len = 0;
static int g_shell_output_pos = 0;

void vga_shell_run_tui(const char* line);
static void about_draw(tui_surface_t* surf, tui_win_t* win);
static void clock_draw(tui_surface_t* surf, tui_win_t* win);

static uint32_t km_strlen(const char* s) {
    uint32_t n = 0;
    while (s[n]) ++n;
    return n;
}

static void u32_to_str(uint32_t v, char* buf, int bufsz) {
    char tmp[12];
    int n = 0;
    if (bufsz <= 0) return;
    if (v == 0u) tmp[n++] = '0';
    while (v > 0u && n < 11) { tmp[n++] = (char)('0' + (v % 10u)); v /= 10u; }
    int out = 0;
    while (n > 0 && out < bufsz - 1) buf[out++] = tmp[--n];
    buf[out] = '\0';
}

static void shell_append_output(const char* text) {
    size_t len = 0;
    while (text[len]) len++;
    if (g_shell_output_len + len >= (int)sizeof(g_shell_output) - 1) {
        int shift = len + 10;
        if (shift > g_shell_output_len) shift = g_shell_output_len;
        for (int i = 0; i < g_shell_output_len - shift; ++i) {
            g_shell_output[i] = g_shell_output[i + shift];
        }
        g_shell_output_len -= shift;
    }
    for (size_t i = 0; i < len; ++i) {
        g_shell_output[g_shell_output_len++] = text[i];
    }
    g_shell_output[g_shell_output_len] = '\0';
    g_shell_output_pos = g_shell_output_len;
}

static void shell_draw(tui_surface_t* surf, tui_win_t* win) {
    (void)win;
    uint8_t attr = tui_theme_attr(4);
    int line = 0;
    int col = 0;
    for (int i = 0; i < g_shell_output_len && line < surf->height - 2; ++i) {
        char c = g_shell_output[i];
        if (c == '\n') {
            line++;
            col = 0;
        } else {
            if (col < surf->width - 2) {
                tui_surface_put(surf, line, col, c, attr);
                col++;
            }
        }
    }
    tui_readline_draw(&g_shell_readline);
}

static void shell_key(tui_win_t* win, uint8_t ascii, uint8_t scancode, uint8_t alt, uint8_t ctrl, uint8_t shift) {
    (void)win;
    (void)alt;
    (void)ctrl;
    (void)shift;
    
    if (scancode == 0x1C) {
        const char* line = tui_readline_get_line(&g_shell_readline);
        tui_readline_add_history(&g_shell_readline, line);
        shell_append_output(line);
        shell_append_output("\n");
        vga_shell_run_tui(line);
        tui_readline_clear(&g_shell_readline);
    } else {
        tui_readline_key(&g_shell_readline, ascii, scancode, alt, ctrl, shift);
    }
}

static int km_strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

typedef void (*shell_cmd_fn)(const char* args);

#define PKG_MAX_DOWNLOAD (128u * 1024u)
static char g_pkg_host[16] = "10.0.2.2";

static void cmd_help(const char* args) {
    (void)args;
    shell_append_output("pasinux TUI commands:\n");
    shell_append_output("  help        show this help\n");
    shell_append_output("  ps          show process stats\n");
    shell_append_output("  mm          show heap info\n");
    shell_append_output("  uptime      show uptime and scheduler stats\n");
    shell_append_output("  dump        dump full state\n");
    shell_append_output("  info        show kernel info\n");
    shell_append_output("  clear       clear shell window\n");
    shell_append_output("  echo <text> echo text back\n");
    shell_append_output("  ls          list FAT12 root directory\n");
    shell_append_output("  cat <file>  print a file from FAT12 volume\n");
    shell_append_output("  touch <f>   create an empty file\n");
    shell_append_output("  write <f> <text>  write text to a file\n");
    shell_append_output("  mkdir <d>   create a directory\n");
    shell_append_output("  rm <f>      delete a file/dir\n");
    shell_append_output("  pkg install <f>   download file from pkg host\n");
    shell_append_output("  pkg host <ip>     set pkg server IP (default 10.0.2.2)\n");
    shell_append_output("  about       show about window\n");
    shell_append_output("  clock       show clock window\n");
}

static void cmd_ps(const char* args) {
    (void)args;
    char buf[32];
    shell_append_output("current process: ");
    shell_append_output(sched_fs_current_name());
    shell_append_output("\n");
    u32_to_str(sched_fs_switches(), buf, sizeof(buf));
    shell_append_output("context switches: ");
    shell_append_output(buf);
    shell_append_output("\n");
    u32_to_str(sched_fs_ticks(), buf, sizeof(buf));
    shell_append_output("scheduler ticks: ");
    shell_append_output(buf);
    shell_append_output("\n");
}

static void cmd_mm(const char* args) {
    (void)args;
    mem_stats_t mem = get_memory_stats();
    shell_append_output("heap info:\n");
    char buf[32];
    u32_to_str((uint32_t)mem.allocation_count, buf, sizeof(buf));
    shell_append_output("  allocations: "); shell_append_output(buf); shell_append_output("\n");
    u32_to_str((uint32_t)mem.free_count, buf, sizeof(buf));
    shell_append_output("  frees: "); shell_append_output(buf); shell_append_output("\n");
    u32_to_str((uint32_t)mem.current_usage, buf, sizeof(buf));
    shell_append_output("  current usage: "); shell_append_output(buf); shell_append_output("\n");
    u32_to_str((uint32_t)mem.peak_usage, buf, sizeof(buf));
    shell_append_output("  peak usage: "); shell_append_output(buf); shell_append_output("\n");
    u32_to_str((uint32_t)mem.failed_allocations, buf, sizeof(buf));
    shell_append_output("  failed: "); shell_append_output(buf); shell_append_output("\n");
}

static void cmd_uptime(const char* args) {
    (void)args;
    char buf[32];
    uint32_t t = timer_ticks();
    shell_append_output("uptime: ");
    u32_to_str(t / 100u, buf, sizeof(buf));
    shell_append_output(buf);
    shell_append_output("s (");
    u32_to_str(t, buf, sizeof(buf));
    shell_append_output(buf);
    shell_append_output(" ticks)\n");
    shell_append_output("sched ticks: ");
    u32_to_str(sched_fs_ticks(), buf, sizeof(buf));
    shell_append_output(buf);
    shell_append_output("\nsched switches: ");
    u32_to_str(sched_fs_switches(), buf, sizeof(buf));
    shell_append_output(buf);
    shell_append_output("\n");
}

static void cmd_dump(const char* args) {
    (void)args;
    cmd_uptime("");
    cmd_ps("");
}

static void cmd_info(const char* args) {
    (void)args;
    shell_append_output("pasinux v0.1 - x86 hobby OS\n");
    shell_append_output("x86 32-bit protected mode\n");
    shell_append_output("PIT 100Hz | Preemptive scheduler | VGA text | Serial I/O\n");
    shell_append_output("TUI Window Manager active\n");
}

static void cmd_clear(const char* args) {
    (void)args;
    g_shell_output_len = 0;
    g_shell_output_pos = 0;
    g_shell_output[0] = '\0';
}

static void cmd_echo(const char* args) {
    shell_append_output(args);
    shell_append_output("\n");
}

static void cmd_ls(const char* args) {
    (void)args;
    const uint32_t entries = g_fs->root_entry_count;
    for (uint32_t j = 0; j < entries; ++j) {
        const uint8_t* e = g_fs->root_dir + j * 32u;
        if (e[0] == 0x00) break;
        if (e[0] == 0xE5) continue;
        if (e[11] == 0x0F) continue;
        for (int k = 0; k < 8; ++k) {
            char c = (char)e[k];
            if (c == ' ') break;
            char buf[2] = {c, '\0'};
            shell_append_output(buf);
        }
        if (e[8] != ' ') {
            shell_append_output(".");
            for (int k = 8; k < 11; ++k) {
                char c = (char)e[k];
                if (c == ' ') break;
                char buf[2] = {c, '\0'};
                shell_append_output(buf);
            }
        }
        uint32_t size = (uint32_t)e[28] | ((uint32_t)e[29] << 8) |
                        ((uint32_t)e[30] << 16) | ((uint32_t)e[31] << 24);
        shell_append_output("  (");
        char buf[16];
        int ndig = 0;
        uint32_t s = size;
        if (s == 0u) buf[ndig++] = '0';
        while (s > 0u && ndig < 15) { buf[ndig++] = (char)('0' + (s % 10u)); s /= 10u; }
        while (ndig > 0) { char c = buf[--ndig]; char b[2] = {c, '\0'}; shell_append_output(b); }
        shell_append_output(" bytes)\n");
    }
}

static void cmd_cat(const char* args) {
    const char* fname = args;
    if (!*fname) {
        shell_append_output("usage: cat <file>\n");
        return;
    }
    file_info_t info;
    if (fat12_find_file(g_fs, fname, &info) != 0) {
        shell_append_output("cat: file not found: ");
        shell_append_output(fname);
        shell_append_output("\n");
        return;
    }
    uint32_t size = 0u;
    void* data = fat12_read_file(g_fs, &info, &size);
    if (!data) {
        shell_append_output("cat: read failed\n");
        return;
    }
    for (uint32_t j = 0; j < size; ++j) {
        uint8_t b = ((const uint8_t*)data)[j];
        if (b == '\n' || b == '\r') {
            shell_append_output("\n");
        } else if (b >= 0x20 && b <= 0x7E) {
            char buf[2] = {(char)b, '\0'};
            shell_append_output(buf);
        } else {
            shell_append_output("\\x");
            static const char digits[] = "0123456789abcdef";
            char pair[3];
            pair[0] = digits[(b >> 4) & 0x0Fu];
            pair[1] = digits[b & 0x0Fu];
            pair[2] = '\0';
            shell_append_output(pair);
        }
    }
    shell_append_output("\n");
    kfree(data);
}

static void cmd_touch(const char* args) {
    const char* arg = args;
    if (!*arg) { shell_append_output("usage: touch <file>\n"); return; }
    int rc = fat12_create_file(g_fs, arg);
    if (rc != 0) shell_append_output("touch: failed (exists or dir full)\n");
    else         shell_append_output("touch: ok\n");
}

static void cmd_write(const char* args) {
    const char* sp = args;
    while (*sp && *sp != ' ' && *sp != '\t') sp++;
    char fname[64];
    int flen = sp - args;
    if (flen == 0 || flen >= 64) { shell_append_output("usage: write <file> <text...>\n"); return; }
    for (int j = 0; j < flen; ++j) fname[j] = args[j];
    fname[flen] = '\0';
    const char* text = sp;
    while (*text == ' ' || *text == '\t') text++;
    int rc = fat12_write_file(g_fs, fname, text, km_strlen(text));
    if (rc != 0) shell_append_output("write: failed (out of space)?\n");
    else         shell_append_output("write: ok\n");
}

static void cmd_mkdir(const char* args) {
    const char* arg = args;
    if (!*arg) { shell_append_output("usage: mkdir <dir>\n"); return; }
    int rc = fat12_create_dir(g_fs, arg);
    if (rc != 0) shell_append_output("mkdir: failed (exists or dir full)\n");
    else         shell_append_output("mkdir: ok\n");
}

static void cmd_rm(const char* args) {
    const char* fname = args;
    if (!*fname) {
        shell_append_output("usage: rm <file>\n");
        return;
    }
    if (fat12_delete_file(g_fs, fname) != 0) {
        shell_append_output("rm: not found\n");
    } else {
        shell_append_output("rm: ok\n");
    }
}

static void cmd_about(const char* args) {
    (void)args;
    tui_win_t* about = tui_win_create("About", 40, 10);
    if (about) {
        about->on_draw = about_draw;
        tui_win_raise(about);
    }
}

static void cmd_clock(const char* args) {
    (void)args;
    tui_win_t* clock = tui_win_create("Clock", 25, 8);
    if (clock) {
        clock->on_draw = clock_draw;
        tui_win_raise(clock);
    }
}

static int valid_ip_str(const char* s) {
    if (!*s) return 0;
    for (int i = 0; s[i]; ++i) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || c == '.')) return 0;
        if (i >= 15) return 0;
    }
    return 1;
}

static void cmd_pkg(const char* args) {
    char sub[16];
    int i = 0;
    while (args[i] && args[i] != ' ' && args[i] != '\t' && i < 15) {
        sub[i] = args[i];
        i++;
    }
    sub[i] = '\0';
    const char* rest = args + i;
    while (*rest == ' ' || *rest == '\t') rest++;

    if (km_strcmp(sub, "install") == 0) {
        if (!*rest) { shell_append_output("usage: pkg install <name>\n"); return; }
        char path[144];
        int p = 0;
        path[p++] = '/';
        while (rest[p - 1] != '\0' && p < (int)sizeof(path) - 1) {
            path[p] = rest[p - 1];
            p++;
        }
        path[p] = '\0';

        uint8_t* buf = kmalloc(PKG_MAX_DOWNLOAD);
        if (!buf) {
            shell_append_output("pkg: out of memory\n");
            return;
        }
        shell_append_output("[pkg] GET http://");
        shell_append_output(g_pkg_host);
        shell_append_output(path);
        shell_append_output(" ...\n");

        uint32_t len = 0;
        if (http_download(g_pkg_host, path, buf, PKG_MAX_DOWNLOAD, &len) != 0) {
            shell_append_output("pkg: download failed\n");
        } else if (len == 0) {
            shell_append_output("pkg: empty response (server up? file exists?)\n");
        } else if (fat12_write_file(g_fs, rest, buf, len) != 0) {
            shell_append_output("pkg: disk write failed\n");
        } else {
            char num[16];
            u32_to_str(len, num, sizeof(num));
            shell_append_output("pkg: saved ");
            shell_append_output(num);
            shell_append_output(" bytes as ");
            shell_append_output(rest);
            shell_append_output("\n");
        }
        kfree(buf);
    } else if (km_strcmp(sub, "host") == 0) {
        if (!*rest) {
            shell_append_output("pkg host: ");
            shell_append_output(g_pkg_host);
            shell_append_output("\n");
            return;
        }
        if (!valid_ip_str(rest)) {
            shell_append_output("pkg: bad IP\n");
            return;
        }
        int j = 0;
        while (rest[j] && j < 15) { g_pkg_host[j] = rest[j]; j++; }
        g_pkg_host[j] = '\0';
        shell_append_output("pkg: host set\n");
    } else {
        shell_append_output("usage: pkg install <name> | pkg host <ip>\n");
    }
}

static const struct {
    const char* name;
    shell_cmd_fn fn;
} g_cmds[] = {
    {"help", cmd_help},
    {"ps", cmd_ps},
    {"mm", cmd_mm},
    {"uptime", cmd_uptime},
    {"dump", cmd_dump},
    {"info", cmd_info},
    {"clear", cmd_clear},
    {"echo", cmd_echo},
    {"ls", cmd_ls},
    {"cat", cmd_cat},
    {"touch", cmd_touch},
    {"write", cmd_write},
    {"mkdir", cmd_mkdir},
    {"rm", cmd_rm},
    {"about", cmd_about},
    {"clock", cmd_clock},
    {"pkg", cmd_pkg},
};

void vga_shell_run_tui(const char* line) {
    if (!g_fs) {
        shell_append_output("filesystem not mounted\n");
        return;
    }

    char cmd[32];
    int i = 0;
    while (line[i] && line[i] != ' ' && line[i] != '\t' && i < 31) {
        cmd[i] = line[i];
        i++;
    }
    cmd[i] = '\0';
    if (cmd[0] == '\0') return;

    const char* rest = line + i;
    while (*rest == ' ' || *rest == '\t') rest++;

    for (size_t c = 0; c < sizeof(g_cmds) / sizeof(g_cmds[0]); ++c) {
        if (km_strcmp(cmd, g_cmds[c].name) == 0) {
            g_cmds[c].fn(rest);
            return;
        }
    }

    shell_append_output("unknown: ");
    shell_append_output(line);
    shell_append_output("\n");
}

static void about_draw(tui_surface_t* surf, tui_win_t* win) {
    (void)win;
    tui_surface_write(surf, 1, 2, "pasinux v0.1", tui_theme_attr(5));
    tui_surface_write(surf, 2, 2, "x86 32-bit hobby OS", tui_theme_attr(5));
    tui_surface_write(surf, 3, 2, "TUI Window Manager", tui_theme_attr(5));
    tui_surface_write(surf, 5, 2, "Press ESC to close", tui_theme_attr(7));
}

static void clock_draw(tui_surface_t* surf, tui_win_t* win) {
    (void)win;
    int h, m, s;
    if (rtc_read_time(&h, &m, &s) == 0) {
        char time_str[16];
        time_str[0] = '0' + h / 10;
        time_str[1] = '0' + h % 10;
        time_str[2] = ':';
        time_str[3] = '0' + m / 10;
        time_str[4] = '0' + m % 10;
        time_str[5] = ':';
        time_str[6] = '0' + s / 10;
        time_str[7] = '0' + s % 10;
        time_str[8] = '\0';
        tui_surface_write(surf, 2, 4, time_str, tui_theme_attr(5));
    } else {
        tui_surface_write(surf, 2, 4, "RTC error", tui_theme_attr(6));
    }
}

void tui_shell_init(fat12_fs_t* fs) {    g_fs = fs;
    
    g_shell_win = tui_win_create("Shell", TUI_WIDTH, TUI_HEIGHT);
    if (g_shell_win) {
        g_shell_win->on_draw = shell_draw;
        g_shell_win->on_key = shell_key;
        g_shell_win->pinned_bottom = 1;
        tui_readline_init(&g_shell_readline, "pasinux$ ", tui_theme_attr(7));
        g_shell_readline.parent = g_shell_win;
        tui_win_raise(g_shell_win);
    }
    
    tui_win_t* about = tui_win_create("About", 40, 10);
    if (about) {
        about->x = (TUI_WIDTH - 40) / 2;
        about->y = (TUI_HEIGHT - 10) / 2;
        about->on_draw = about_draw;
        tui_win_raise(about);
    }
    
    tui_win_t* clock = tui_win_create("Clock", 25, 8);
    if (clock) {
        clock->x = TUI_WIDTH - 27;
        clock->y = 2;
        clock->on_draw = clock_draw;
        tui_win_raise(clock);
    }
}
/* WM keyboard-equivalent hooks: F2 reopens About, F3 reopens Clock. */
void tui_shell_open_about(void) {
    tui_win_t* about = tui_win_create("About", 40, 10);
    if (about) {
        about->x = (TUI_WIDTH - 40) / 2;
        about->y = (TUI_HEIGHT - 10) / 2;
        about->on_draw = about_draw;
        tui_win_raise(about);
    }
}

void tui_shell_open_clock(void) {
    tui_win_t* clock = tui_win_create("Clock", 25, 8);
    if (clock) {
        clock->x = TUI_WIDTH - 27;
        clock->y = 2;
        clock->on_draw = clock_draw;
        tui_win_raise(clock);
    }
}
