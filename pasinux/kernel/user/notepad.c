/* notepad.c — pasinux user-mode text editor (ELF32, ring 3).
 *
 * Line-oriented editor over the raw console:
 *   :help          list commands
 *   :new           clear buffer
 *   :open <file>   load a file from disk
 *   :save <file>   write buffer to disk
 *   :list          show numbered lines
 *   :del <n>       delete line n
 *   :quit          exit
 * Anything else is appended as a new line of text.
 *
 * Talks to the kernel exclusively through int 0x80:
 *   eax=nr, ebx/ecx/edx args, result in eax.
 */

typedef unsigned int u32;
typedef int s32;

#define SYS_EXIT  1u
#define SYS_OPEN  3u
#define SYS_READ  4u
#define SYS_WRITE 5u
#define SYS_CLOSE 6u

#define O_RDONLY 1u
#define O_WRONLY 2u
#define O_CREAT  0x0100u

#define MAX_LINES 120u
#define LINE_LEN  78u
#define IOBUF_SZ  8192u

static char g_text[MAX_LINES][LINE_LEN];
static u32  g_count;
static char g_iobuf[IOBUF_SZ];

static inline u32 syscall3(u32 nr, u32 a, u32 b, u32 c)
{
    u32 ret;
    __asm__ volatile ("int $0x80"
                      : "=a"(ret)
                      : "a"(nr), "b"(a), "c"(b), "d"(c)
                      : "memory");
    return ret;
}

/* ELF entry point: call main(), then SYS_EXIT with its return value. */
__asm__(
    ".global _start\n"
    "_start:\n"
    "  call main\n"
    "  movl %eax, %ebx\n"
    "  movl $1, %eax\n"
    "  int $0x80\n"
    "1: jmp 1b\n");

static int str_eq(const char* a, const char* b)
{
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static void out(const char* s, u32 len)
{
    if (len) {
        syscall3(SYS_WRITE, 1u, (u32)s, len);
    }
}

static void outs(const char* s)
{
    u32 n = 0;
    while (s[n]) {
        ++n;
    }
    out(s, n);
}

static void outu(u32 v)
{
    char buf[12];
    int i = 0;
    if (v == 0u) {
        buf[i++] = '0';
    }
    while (v > 0u) {
        buf[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (i > 0) {
        --i;
        out(&buf[i], 1u);
    }
}

static u32 rdline(char* buf, u32 max)
{
    return syscall3(SYS_READ, 0u, (u32)buf, max);
}

/* Copy src into dst up to cap-1 chars; returns length copied. */
static u32 copy_line(const char* src, u32 srclen, char* dst, u32 cap)
{
    u32 n = (srclen < cap - 1u) ? srclen : cap - 1u;
    for (u32 i = 0u; i < n; ++i) {
        dst[i] = src[i];
    }
    for (u32 i = n; i < cap; ++i) {
        dst[i] = '\0';
    }
    return n;
}

static void cmd_new(void)
{
    for (u32 i = 0u; i < MAX_LINES; ++i) {
        for (u32 j = 0u; j < LINE_LEN; ++j) {
            g_text[i][j] = '\0';
        }
    }
    g_count = 0u;
    outs("* new buffer\n");
}

static void cmd_list(void)
{
    for (u32 i = 0u; i < g_count; ++i) {
        out("  ", 2u);
        outu(i + 1u);
        outs(": ");
        outs(g_text[i]);
        outs("\n");
    }
    outs("(");
    outu(g_count);
    outs(" lines)\n");
}

static void cmd_del(const char* arg)
{
    u32 n = 0u;
    while (*arg >= '0' && *arg <= '9') {
        n = n * 10u + (u32)(*arg - '0');
        ++arg;
    }
    if (n == 0u || n > g_count) {
        outs("* bad line number\n");
        return;
    }
    for (u32 i = n - 1u; i + 1u < g_count; ++i) {
        copy_line(g_text[i + 1u], LINE_LEN, g_text[i], LINE_LEN);
    }
    --g_count;
    g_text[g_count][0] = '\0';
    outs("* deleted line ");
    outu(n);
    outs("\n");
}

static void cmd_open(const char* fname)
{
    if (!*fname) {
        outs("* usage: :open <file>\n");
        return;
    }
    const u32 fd = syscall3(SYS_OPEN, (u32)fname, O_RDONLY, 0u);
    if ((s32)fd < 0) {
        outs("* open failed: ");
        outs(fname);
        outs("\n");
        return;
    }

    u32 total = 0u;
    for (;;) {
        const u32 got = syscall3(SYS_READ, fd, (u32)(g_iobuf + total),
                                 IOBUF_SZ - total);
        if ((s32)got <= 0 || total + got >= IOBUF_SZ) {
            break;
        }
        total += got;
    }
    syscall3(SYS_CLOSE, fd, 0u, 0u);

    cmd_new();
    u32 pos = 0u;
    while (pos < total && g_count < MAX_LINES) {
        u32 len = 0u;
        while (pos + len < total && g_iobuf[pos + len] != '\n') {
            ++len;
        }
        copy_line(g_iobuf + pos, len, g_text[g_count], LINE_LEN);
        ++g_count;
        pos += len + 1u; /* skip newline */
    }
    outs("* loaded ");
    outu(g_count);
    outs(" lines from ");
    outs(fname);
    outs("\n");
}

static void cmd_save(const char* fname)
{
    if (!*fname) {
        outs("* usage: :save <file>\n");
        return;
    }
    u32 total = 0u;
    for (u32 i = 0u; i < g_count; ++i) {
        const char* l = g_text[i];
        u32 len = 0u;
        while (l[len]) {
            ++len;
        }
        if (total + len + 1u >= IOBUF_SZ) {
            break;
        }
        for (u32 j = 0u; j < len; ++j) {
            g_iobuf[total++] = l[j];
        }
        g_iobuf[total++] = '\n';
    }

    const u32 fd = syscall3(SYS_OPEN, (u32)fname, O_WRONLY | O_CREAT, 0u);
    if ((s32)fd < 0) {
        outs("* save failed: ");
        outs(fname);
        outs("\n");
        return;
    }
    const u32 wrote = syscall3(SYS_WRITE, fd, (u32)g_iobuf, total);
    syscall3(SYS_CLOSE, fd, 0u, 0u);
    outs("* saved ");
    outu(wrote);
    outs(" bytes to ");
    outs(fname);
    outs("\n");
}

static void help(void)
{
    outs("notepad — line editor\n"
         "  :help        this help\n"
         "  :new         clear buffer\n"
         "  :open <file> load file\n"
         "  :save <file> save file\n"
         "  :list        show lines\n"
         "  :del <n>     delete line n\n"
         "  :quit        exit\n"
         "type text + Enter to append a line\n");
}

int main(void)
{
    outs("[notepad running in ring 3]\n");
    help();
    outs("np> ");

    static char line[LINE_LEN + 2u];
    for (;;) {
        const u32 n = rdline(line, sizeof(line));
        if (n == 0u) { /* ESC or empty enter */
            outs("np> ");
            continue;
        }

        if (line[0] == ':') {
            char* arg = line;
            while (*arg && *arg != ' ') {
                ++arg;
            }
            if (*arg) {
                *arg++ = '\0';
                while (*arg == ' ') {
                    ++arg;
                }
            }

            if (str_eq(line, ":quit")) {
                break;
            } else if (str_eq(line, ":help")) {
                help();
            } else if (str_eq(line, ":new")) {
                cmd_new();
            } else if (str_eq(line, ":list")) {
                cmd_list();
            } else if (str_eq(line, ":open")) {
                cmd_open(arg);
            } else if (str_eq(line, ":save")) {
                cmd_save(arg);
            } else if (str_eq(line, ":del")) {
                cmd_del(arg);
            } else {
                outs("* unknown command: ");
                outs(line);
                outs("\n");
            }
            outs("np> ");
            continue;
        }

        if (g_count >= MAX_LINES) {
            outs("* buffer full\n");
        } else {
            copy_line(line, n, g_text[g_count], LINE_LEN);
            ++g_count;
        }
        outs("np> ");
    }

    outs("* bye\n");
    return 0;
}
