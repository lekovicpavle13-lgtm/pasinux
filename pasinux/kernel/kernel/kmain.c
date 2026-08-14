#include "ata.h"
#include "driver_fs.h"
#include "fat12.h"
#include "gdt.h"
#include "idt.h"
#include "interrupt.h"
#include "keyboard.h"
#include "mm_fs.h"
#include "net_arp.h"
#include "net_eth.h"
#include "net_tcp.h"
#include "paging.h"
#include "pci.h"
#include "rtl8139.h"
#include "sched_fs.h"
#include "serial.h"
#include "syscall.h"
#include "timer.h"
#include "tss.h"
#include "vga.h"
#include <stdint.h>

extern void launch_ring3(void *entry, void *user_stack_top);
extern char user_start[];

/* Background process counters */
static volatile uint32_t g_init_iters;
static volatile uint32_t g_worker_iters;
static volatile uint32_t g_idle_iters;

/* Mounted FAT12 volume (set in freestanding_subsystems_up). */
static fat12_fs_t* g_fs;

static void init_entry(void) {
    serial_puts("[PROC] init: started\n");
    for (;;) {
        ++g_init_iters;
    }
}

static void worker_entry(void) {
    serial_puts("[PROC] worker: started\n");
    for (;;) {
        ++g_worker_iters;
    }
}

static void idle_demo_entry(void) {
    serial_puts("[PROC] idle-demo: started\n");
    for (;;) {
        ++g_idle_iters;
    }
}

static void freestanding_subsystems_up(void) {
    serial_init();
    serial_puts("[KERNEL] pasinux freestanding kernel booting\n");
    vga_clear();
    vga_write(0, 0, "pasinux kernel booting...");
    vga_write(1, 0, "VGA text mode active 80x25");
    serial_puts("[KERNEL] VGA initialized: 80x25 text mode\n");

    /* Quick VGA self-test: read back what we just wrote */
    volatile uint16_t* vga = (volatile uint16_t*)0xC00B8000;
    uint16_t first_char = vga[0];
    serial_puts("[KERNEL] VGA self-test (read-back): first char='");
    serial_putc((char)(first_char & 0xFF));
    serial_puts("'\n");

    paging_init_higher_half();

    init_memory();

    gdt_install();
    serial_puts("[GDT] loaded 6 entries (kernel cs/ds + user cs/ds + TSS)\n");

    tss_flush();
    serial_puts("[TSS] loaded TR\n");

    idt_init();
    serial_puts("[IDT] IDT loaded, PIC remapped, PIT at 100 Hz, keyboard IRQ1\n");

    irq_init_handlers();
    serial_puts("[IRQ] handler table initialized\n");

    sched_fs_init();
    serial_puts("[SCHED] scheduler ready\n");

    drivers_init_fs();

    serial_puts("[IPC] ipc ready\n");

    pci_scan_all();

    // Bring up the ATA block driver and mount the FAT12 volume that this
    // floppy image carries.
    if (ata_init() == 0) {
        serial_puts("[FS] ATA driver up; mounting FAT12...\n");
        g_fs = fat12_mount("ata");
        if (g_fs) {
            serial_puts("[FS] FAT12 mounted OK\n");
            // Boot-time self-test of the exact find+read path the shell's
            // `cat` uses. KERNEL.BIN spans 83 clusters, so this exercises
            // FAT-chain walking end to end.
            file_info_t kbin;
            if (fat12_find_file(g_fs, "KERNEL.BIN", &kbin) == 0) {
                uint32_t sz = 0u;
                void* data = fat12_read_file(g_fs, &kbin, &sz);
                serial_puts("[FS] selftest: KERNEL.BIN found, first_cluster=");
                /* simple hex dump of the 16-bit first_cluster (no libc) */
                {
                    unsigned int cv = kbin.first_cluster;
                    char hx[16]; int hi = 0;
                    static const char digs[] = "0123456789ABCDEF";
                    do { hx[hi++] = digs[cv & 0xFu]; cv >>= 4; } while (cv && hi < 15);
                    while (hi > 0) serial_putc(hx[--hi]);
                }
                if (data) {
                    serial_puts(" read ");
                    // cheap decimal print
                    char n[16]; int nd = 0; uint32_t v = sz;
                    if (v == 0u) n[nd++] = '0';
                    while (v > 0u && nd < 15) { n[nd++] = (char)('0' + (v % 10u)); v /= 10u; }
                    while (nd > 0) serial_putc(n[--nd]);
                    serial_puts(" bytes: OK\n");
                    kfree(data);
                } else {
                    serial_puts(" read FAILED\n");
                }
            } else {
                serial_puts("[FS] selftest: KERNEL.BIN not found\n");
            }
        } else {
            serial_puts("[FS] FAT12 mount FAILED\n");
        }
    } else {
        serial_puts("[FS] ATA init failed; filesystem unavailable\n");
    }
}

static void ring3_demo(void){

    uint32_t *kernel_stack = (uint32_t*)kmalloc(4096u);
    if (!kernel_stack) {
        serial_puts("[SYS] ring3: kmalloc kernel stack failed\n");
        return;
    }
    tss_set_kernel_stack((uint32_t)(uintptr_t)kernel_stack + 4096u);

    /* Allocate a user-mode stack */
    uint32_t *user_stack = (uint32_t*)kmalloc(4096u);
    if (!user_stack) {
        serial_puts("[SYS] ring3: kmalloc user stack failed\n");
        kfree(kernel_stack);
        return;
    }

    serial_puts("[SYS] launching ring-3 test\n");
    launch_ring3(user_start, (void*)((uint32_t)(uintptr_t)user_stack + 4096u));
}

static void freestanding_demo(void) {
    ipc_chess_send_state(1, "startpos");
    ipc_chess_send_move(1, "e2e4", 0);

    serial_puts("[IPC] poll queue\n");
    ipc_fs_poll(16);

    serial_puts("[MM] === memory stats ===\n");
    print_memory_stats();

    serial_puts("[DEMO] kmalloc/kfree test\n");
    void* a = kmalloc(64);
    if (a) {
        serial_puts("[DEMO] allocated 64 bytes ok\n");
        kfree(a);
        serial_puts("[DEMO] freed ok\n");
    }
    print_memory_stats();
}

static void u32_to_str(uint32_t v, char* buf, size_t n) {
    char tmp[12];
    size_t i = 0;
    if (v == 0u) {
        tmp[i++] = '0';
    } else {
        while (v > 0u && i < sizeof(tmp) - 1u) {
            tmp[i++] = (char)('0' + (v % 10u));
            v /= 10u;
        }
    }
    size_t j = 0;
    while (i > 0u && j < n - 1u) {
        buf[j++] = tmp[--i];
    }
    buf[j] = '\0';
}

static void vga_shell_help(void) {
    vga_puts("pasinux commands:\n");
    vga_puts("  help        show this help\n");
    vga_puts("  ps          show process stats\n");
    vga_puts("  mm          show heap info\n");
    vga_puts("  uptime      show uptime and scheduler stats\n");
    vga_puts("  dump        dump full state to VGA\n");
    vga_puts("  info        show kernel info\n");
    vga_puts("  clear       clear VGA screen\n");
    vga_puts("  echo <text> echo text back\n");
    vga_puts("  sudo <cmd>  execute command as root\n");
    vga_puts("  neofetch    show system info\n");
    vga_puts("  pci         list PCI devices\n");
    vga_puts("  netstat     show NIC status\n");
    vga_puts("  arp         show ARP cache\n");
    vga_puts("  ls          list FAT12 root directory\n");
    vga_puts("  cat <file>  print a file from FAT12 volume\n");
    }

/* Print one byte of a file as a hex pair (used by the fs shell commands). */
static void vga_puts_hex(uint8_t b) {
    static const char digits[] = "0123456789abcdef";
    char pair[3];
    pair[0] = digits[(b >> 4) & 0x0Fu];
    pair[1] = digits[b & 0x0Fu];
    pair[2] = '\0';
    vga_puts(pair);
}

/* ls: list the root directory of the mounted FAT12 volume. */
static void vga_shell_ls(void) {
    if (!g_fs) {
        vga_puts("filesystem not mounted\n");
        return;
    }
    const uint32_t entries = g_fs->root_entry_count;
    for (uint32_t i = 0u; i < entries; ++i) {
        const uint8_t* e = g_fs->root_dir + i * 32u;
        if (e[0] == 0x00) break;             /* end of directory */
        if (e[0] == 0xE5) continue;          /* deleted entry */
        if (e[11] == 0x0F) continue;         /* long-file-name marker */
        /* Print the 8.3 name with the pad spaces dropped. */
        for (int k = 0; k < 8; ++k) {
            char c = (char)e[k];
            if (c == ' ') break;
            vga_putc(c);
        }
        if (e[8] != ' ') {
            vga_putc('.');
            for (int k = 8; k < 11; ++k) {
                char c = (char)e[k];
                if (c == ' ') break;
                vga_putc(c);
            }
        }
        uint32_t size = (uint32_t)e[28] | ((uint32_t)e[29] << 8) |
                        ((uint32_t)e[30] << 16) | ((uint32_t)e[31] << 24);
        vga_puts("  (");
        uint32_t s = size;
        char num[16];
        int ndig = 0;
        if (s == 0u) num[ndig++] = '0';
        while (s > 0u && ndig < 15) { num[ndig++] = (char)('0' + (s % 10u)); s /= 10u; }
        while (ndig > 0) { vga_putc(num[--ndig]); }
        vga_puts(" bytes)\n");
    }
}

/* cat <file>: read and print a file from the mounted FAT12 volume. */
static void vga_shell_cat(const char* name) {
    if (!g_fs) {
        vga_puts("filesystem not mounted\n");
        return;
    }
    file_info_t info;
    if (fat12_find_file(g_fs, name, &info) != 0) {
        vga_puts("cat: file not found: ");
        vga_puts(name);
        vga_puts("\n");
        return;
    }
    uint32_t size = 0u;
    void* data = fat12_read_file(g_fs, &info, &size);
    if (!data) {
        vga_puts("cat: read failed\n");
        return;
    }
    /* Print file content, escaping non-printable bytes as hex. */
    for (uint32_t i = 0u; i < size; ++i) {
        uint8_t b = ((const uint8_t*)data)[i];
        if (b == '\n' || b == '\r') {
            vga_puts("\n");
        } else if (b >= 0x20 && b <= 0x7E) {
            vga_putc((char)b);
        } else {
            vga_puts("\\x");
            vga_puts_hex(b);
        }
    }
    vga_puts("\n");
    kfree(data);
}

static void vga_shell_ps(void) {
    char buf[32];
    vga_puts("current process: ");
    vga_puts(sched_fs_current_name());
    vga_puts("\n");
    u32_to_str(sched_fs_switches(), buf, sizeof(buf));
    vga_puts("context switches: ");
    vga_puts(buf);
    vga_puts("\n");
    u32_to_str(sched_fs_ticks(), buf, sizeof(buf));
    vga_puts("scheduler ticks: ");
    vga_puts(buf);
    vga_puts("\n");
}

static void vga_shell_uptime(void) {
    char buf[32];
    uint32_t t = timer_ticks();
    vga_puts("uptime: ");
    u32_to_str(t / 100u, buf, sizeof(buf));
    vga_puts(buf);
    vga_puts("s (");
    u32_to_str(t, buf, sizeof(buf));
    vga_puts(buf);
    vga_puts(" ticks)\n");
    vga_puts("sched ticks: ");
    u32_to_str(sched_fs_ticks(), buf, sizeof(buf));
    vga_puts(buf);
    vga_puts("\nsched switches: ");
    u32_to_str(sched_fs_switches(), buf, sizeof(buf));
    vga_puts(buf);
    vga_puts("\n");
}

static void vga_shell_info(void) {
    vga_puts("pasinux v0.1 - x86 hobby OS\n");
    vga_puts("x86 32-bit protected mode\n");
    vga_puts("PIT 100Hz | Preemptive scheduler | VGA text | Serial I/O\n");
    vga_puts("registered drivers: ");
    driver_t *d = driver_get_list_head();
    while (d) {
        vga_puts(d->name);
        if (d->next) vga_puts(", ");
        d = d->next;
    }
    vga_puts("\n");
}

static void vga_shell_mm(void) {
    char buf[32];
    mem_stats_t mem = get_memory_stats();
    vga_puts("heap info:\n");
    u32_to_str((uint32_t)mem.allocation_count, buf, sizeof(buf));
    vga_puts("  allocations: "); vga_puts(buf); vga_puts("\n");
    u32_to_str((uint32_t)mem.free_count, buf, sizeof(buf));
    vga_puts("  frees: "); vga_puts(buf); vga_puts("\n");
    u32_to_str((uint32_t)mem.current_usage, buf, sizeof(buf));
    vga_puts("  current usage: "); vga_puts(buf); vga_puts("\n");
    u32_to_str((uint32_t)mem.peak_usage, buf, sizeof(buf));
    vga_puts("  peak usage: "); vga_puts(buf); vga_puts("\n");
    u32_to_str((uint32_t)mem.failed_allocations, buf, sizeof(buf));
    vga_puts("  failed: "); vga_puts(buf); vga_puts("\n");
}

static void vga_shell_dump(void) {
    vga_shell_uptime();
    vga_shell_ps();
}

static void vga_shell_pci(void) {
    pci_scan_all();
}

static void vga_shell_netstat(void) {
    rtl8139_t* nic = rtl8139_get();
    if (!nic || nic->io_base == 0u) {
        vga_puts("NIC: not initialized\n");
        return;
    }
    char buf[12];
    vga_puts("RTL8139 NIC:\n");
    vga_puts("  IO base: 0x");
    {
        const char* hex = "0123456789ABCDEF";
        uint16_t io = nic->io_base;
        buf[0] = hex[(io >> 12) & 0x0Fu];
        buf[1] = hex[(io >> 8) & 0x0Fu];
        buf[2] = hex[(io >> 4) & 0x0Fu];
        buf[3] = hex[io & 0x0Fu];
        buf[4] = '\0';
        vga_puts(buf);
    }
    vga_puts("\n");
    u32_to_str(nic->packet_count, buf, sizeof(buf));
    vga_puts("  packets: "); vga_puts(buf); vga_puts("\n");
    u32_to_str(nic->byte_count, buf, sizeof(buf));
    vga_puts("  bytes: "); vga_puts(buf); vga_puts("\n");
}

static void vga_shell_arp(void) {
    arp_print_cache();
    /* ARP cache only prints to serial, so let user know */
    vga_puts("Check serial for ARP cache\n");
}

static void vga_shell_neofetch(void) {
    char buf[32];
    vga_puts("  ______   ___   _   _  _  _  _   _\n");
    vga_puts(" / _ _  \\ / _ \\ | \\ | || || || \\ | |\n");
    vga_puts("| | | | || | | ||  \\| || || ||  \\| |\n");
    vga_puts("| | | | || | | || . ` || || || . ` |\n");
    vga_puts("| |_| | || |_| || |\\  || || || |\\  |\n");
    vga_puts(" \\___\\_\\  \\___/ |_| \\_||_||_||_| \\_|\n");
    vga_puts("------------------------------------\n");
    vga_puts("OS: pasinux v0.1\n");
    vga_puts("Kernel: x86 32-bit protected mode\n");
    vga_puts("Shell: VGA text mode 80x25\n");
    vga_puts("PIT: 100 Hz\n");
    vga_puts("Scheduler: preemptive round-robin\n");
    vga_puts("PS/2 keyboard: active\n");
    vga_puts("Serial: COM1 115200 baud\n");
    u32_to_str(sched_fs_ticks(), buf, sizeof(buf));
    vga_puts("Uptime: "); vga_puts(buf); vga_puts(" ticks\n");
}

static void vga_shell_run(const char *line) {
    char cmd[32];
    uint16_t i = 0;
    while (line[i] != '\0' && line[i] != ' ' && line[i] != '\t' && i < 31) {
        cmd[i] = line[i];
        ++i;
    }
    cmd[i] = '\0';

    if (cmd[0] == 'h' && cmd[1] == 'e' && cmd[2] == 'l' && cmd[3] == 'p') {
        vga_shell_help();
    } else if (cmd[0] == 'p' && cmd[1] == 's' && cmd[2] == '\0') {
        vga_shell_ps();
    } else if (cmd[0] == 'm' && cmd[1] == 'm' && cmd[2] == '\0') {
        vga_shell_mm();
    } else if (cmd[0] == 'u' && cmd[1] == 'p') {
        vga_shell_uptime();
    } else if (cmd[0] == 'd' && cmd[1] == 'u') {
        vga_shell_dump();
    } else if (cmd[0] == 'i' && cmd[1] == 'n') {
        vga_shell_info();
    } else if (cmd[0] == 'c' && cmd[1] == 'l') {
        vga_clear();
    } else if (cmd[0] == 'e' && cmd[1] == 'c' && cmd[2] == 'h' && cmd[3] == 'o') {
        const char* msg = line + 4;
        while (*msg == ' ') msg++;
        vga_puts(msg);
        vga_puts("\n");
    } else if (cmd[0] == 's' && cmd[1] == 'u' && cmd[2] == 'd' && cmd[3] == 'o') {
        const char* rest = line + 4;
        while (*rest == ' ') rest++;
        if (*rest) {
            vga_puts("[sudo] executing as root...\n");
            vga_shell_run(rest);
        } else {
            vga_puts("usage: sudo <command>\n");
        }
    } else if (cmd[0] == 'n' && cmd[1] == 'e' && cmd[2] == 'o') {
        vga_shell_neofetch();
    } else if (cmd[0] == 'p' && cmd[1] == 'c' && cmd[2] == 'i' && cmd[3] == '\0') {
        vga_shell_pci();
    } else if (cmd[0] == 'n' && cmd[1] == 'e' && cmd[2] == 't' && cmd[3] == 's') {
        vga_shell_netstat();
    } else if (cmd[0] == 'a' && cmd[1] == 'r' && cmd[2] == 'p' && cmd[3] == '\0') {
        vga_shell_arp();
    } else if (cmd[0] == 'l' && cmd[1] == 's' && cmd[2] == '\0') {
        vga_shell_ls();
    } else if (cmd[0] == 'c' && cmd[1] == 'a' && cmd[2] == 't') {
        const char* fname = line + 3;
        while (*fname == ' ') fname++;
        if (*fname) {
            vga_shell_cat(fname);
        } else {
            vga_puts("usage: cat <file>\n");
        }
    } else {
        vga_puts("unknown: ");
        vga_puts(line);
        vga_puts("\n");
    }
}

static void vga_shell_interactive(void) {
    char buf[128];
    vga_puts("pasinux VGA shell ready (type 'help')\n");
    for (;;) {
        vga_puts("$ ");
        uint16_t n = keyboard_readline_vga(buf, sizeof(buf));
        if (n > 0) {
            vga_shell_run(buf);
        }
    }
}

void kmain(void) {
    freestanding_subsystems_up();
    ring3_demo();
    vga_write(2, 0, "Ring-3 test: OK             ");
    freestanding_demo();

    /* Busy-wait heartbeat: busy-loop to let time pass, check timer_ticks */
    uint32_t last_tick = timer_ticks();
    int hb_ok = 0;
    for (int hb = 0; hb < 3; ++hb) {
        /* Busy-wait ~500ms (no HLT — it sleeps forever if PIT is dead) */
        for (volatile uint32_t d = 0; d < 10000000u; ++d) {
            __asm__ volatile ("" : : : "memory");
        }
        uint32_t now = timer_ticks();
        serial_puts("[PIT] heartbeat=");
        serial_put_u32((uint32_t)hb);
        serial_puts(" tick=");
        serial_put_u32(now);
        if (now != last_tick) {
            serial_puts(" CHANGED\n");
            hb_ok = 1;
        } else {
            serial_puts(" STALLED\n");
        }
        last_tick = now;
    }
    if (hb_ok) {
        vga_write(3, 0, "Timer/PIT/IRQ: OK          ");
    } else {
        vga_write(3, 0, "Timer/PIT: STALLED!!       ");
    }

    /* Init RTL8139 NIC if present on PCI bus */
    {
        uint16_t rtl_addr = pci_find_device(RTL8139_VENDOR_ID, RTL8139_DEVICE_ID);
        if (rtl_addr != PCI_NOT_FOUND) {
            serial_puts("[NET] RTL8139 found, initializing...\n");
            if (rtl8139_init(rtl_addr, rtl8139_get()) == 0) {
                serial_puts("[NET] RTL8139 ready\n");
                net_set_mac(rtl8139_get()->mac);
                arp_init();
                tcp_init();
                serial_puts("[NET] TCP/IP stack initialized\n");
            } else {
                serial_puts("[NET] RTL8139 init FAILED\n");
            }
        } else {
            serial_puts("[NET] RTL8139 not found\n");
        }
    }

    serial_puts("[SCHED] creating background processes\n");
    sched_fs_create_process("init",     init_entry,     10);
    sched_fs_create_process("worker",   worker_entry,    5);
    sched_fs_create_process("idle-demo", idle_demo_entry, 1);
    serial_puts("[SCHED] background processes created, entering VGA shell\n");
    vga_write(4, 0, "Scheduler: 3 procs running ");
    vga_write(5, 0, "Scheduler: 3 procs running ");
    vga_write(6, 0, "RTL8139 NIC: active          ");
    vga_set_cursor(8, 0);
    vga_enable_cursor();

    vga_shell_interactive();
}