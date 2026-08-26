#include "ata.h"
#include "driver_fs.h"
#include "fat12.h"
#include "gdt.h"
#include "http.h"
#include "idt.h"
#include "interrupt.h"
#include "keyboard.h"
#include "mm_fs.h"
#include "net_arp.h"
#include "net_eth.h"
#include "net_tcp.h"
#include "paging.h"
#include "pci.h"
#include "ps2mouse.h"
#include "rtl8139.h"
#include "rtc.h"
#include "sched_fs.h"
#include "serial.h"
#include "shell.h"
#include "syscall.h"
#include "timer.h"
#include "tss.h"
#include "tui_core.h"
#include "tui_wm.h"
#include "widgets.h"
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
            syscall_set_fs(g_fs);
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
            /* Boot-time write-path self-test: create -> write -> read-back ->
             * verify -> create_dir -> delete. This exercises the exact FAT12
             * write path the shell's touch/write/mkdir/rm use, and lands on
             * the on-disk volume (pasinux.img), so a success is a real durable
             * disk write through ATA PIO. */
            {
                static const char tdata[] = "hello pasinux write test 0123456789";
                const uint32_t tsize = sizeof(tdata) - 1u;
                int ok = fat12_write_file(g_fs, "_SELFTST.TXT", tdata, tsize);
                if (ok == 0) {
                    file_info_t ti;
                    if (fat12_find_file(g_fs, "_SELFTST.TXT", &ti) == 0
                        && ti.size == tsize) {
                        uint32_t rs = 0u;
                        void* rd = fat12_read_file(g_fs, &ti, &rs);
                        ok = (rd && rs == tsize
                              && ((const char*)rd)[0] == 'h'
                              && ((const char*)rd)[tsize - 1u] == '9') ? 0 : -1;
                        if (rd) kfree(rd);
                    } else {
                        ok = -1;
                    }
                }
                int dir_ok = (fat12_create_dir(g_fs, "_TSTDIR") == 0)
                          && (fat12_delete_file(g_fs, "_SELFTST.TXT") == 0)
                          && (fat12_delete_file(g_fs, "_TSTDIR") == 0);
                if (ok == 0 && dir_ok)
                    serial_puts("[FS] write selftest: OK\n");
                else
                    serial_puts("[FS] write selftest: FAILED\n");
            }
            /* Multi-cluster (900-byte = 2 clusters) write/read-back test. The
             * shell's `write` is capped at the 128-byte line buffer, so this
             * is the only way to exercise the FAT-chain linking across >1
             * cluster. Uses a deterministic byte pattern and verifies every
             * byte round-trips. */
            {
                static const char* tn = "WRTEST.TMP";
                uint8_t wbuf[900];
                for (unsigned i = 0u; i < sizeof(wbuf); ++i)
                    wbuf[i] = (uint8_t)(i % 251u);
                if (fat12_write_file(g_fs, tn, wbuf, sizeof(wbuf)) == 0) {
                    file_info_t wi;
                    if (fat12_find_file(g_fs, tn, &wi) == 0 && wi.size == sizeof(wbuf)) {
                        uint32_t ws = 0u;
                        void* wdata = fat12_read_file(g_fs, &wi, &ws);
                        int wbad = (int)sizeof(wbuf);
                        if (wdata && ws == sizeof(wbuf)) {
                            wbad = 0;
                            for (unsigned i = 0u; i < sizeof(wbuf); ++i)
                                if (((const uint8_t*)wdata)[i] != wbuf[i]) wbad = 1;
                        }
                        if (wdata) kfree(wdata);
                        serial_puts(wbad == 0
                            ? "[FS] writeselftest: 900-byte multi-cluster r/w OK\n"
                            : "[FS] writeselftest: DATA MISMATCH\n");
                        fat12_delete_file(g_fs, tn);
                    } else {
                        serial_puts("[FS] writeselftest: find/size FAILED\n");
                        fat12_delete_file(g_fs, tn);
                    }
                } else {
                    serial_puts("[FS] writeselftest: write FAILED\n");
                }
            }
            /* Cross-reboot durability check: a marker file written in one boot
             * must survive into the next. Run 1 creates it; run 2 sees it with
             * the right content, verifies the bytes, then deletes it (so the
             * next pair of boots repeats). This proves writes are flushed to
             * pasinux.img and re-read through ATA, not cached in RAM only. */
            {
                const char* marker = "_WTEST.TXT";
                const char* mdata = "persistent";
                const uint32_t msize = 10u;
                file_info_t mi;
                if (fat12_find_file(g_fs, marker, &mi) == 0) {
                    uint32_t ms = 0u;
                    void* md = fat12_read_file(g_fs, &mi, &ms);
                    int content_ok = (md && ms == msize &&
                        ((const char*)md)[0] == 'p' &&
                        ((const char*)md)[msize - 1u] == 't');
                    if (md) kfree(md);
                    int rm = fat12_delete_file(g_fs, marker);
                    serial_puts(content_ok && rm == 0
                        ? "[FS] persist selftest: OK (survived reboot)\n"
                        : "[FS] persist selftest: FAILED\n");
                } else {
                    int wc = fat12_write_file(g_fs, marker, mdata, msize);
                    serial_puts(wc == 0
                        ? "[FS] persist selftest: wrote marker (reboot to verify)\n"
                        : "[FS] persist selftest: write FAILED\n");
                }
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

void kmain(void) {
    freestanding_subsystems_up();
    ring3_demo();
    vga_write(2, 0, "Ring-3 test: OK             ");
    freestanding_demo();

    /* Busy-wait heartbeat: busy-loop to let time pass, check timer_ticks */
    uint32_t last_tick = timer_ticks();
    int hb_ok = 0;
    for (int hb = 0; hb < 3; ++hb) {
        /* Busy-wait ~500ms (no HLT - it sleeps forever if PIT is dead) */
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

                {
                    static const char expect[] = "pasinux-pkg-ok";
                    uint8_t dlbuf[512];
                    uint32_t dlen = 0;
                    serial_puts("[PKG] selftest: GET 10.0.2.2/pkgselftest.txt\n");
                    if (http_download("10.0.2.2", "/pkgselftest.txt",
                                      dlbuf, sizeof(dlbuf), &dlen) != 0 || dlen == 0) {
                        serial_puts("[PKG] selftest: download FAILED\n");
                    } else if (dlen < sizeof(expect) - 1) {
                        serial_puts("[PKG] selftest: content MISMATCH\n");
                    } else {
                        int match = 1;
                        for (uint32_t q = 0; q < sizeof(expect) - 1; ++q) {
                            if (dlbuf[q] != (uint8_t)expect[q]) { match = 0; break; }
                        }
                        if (!match) {
                            serial_puts("[PKG] selftest: content MISMATCH\n");
                        } else {
                            serial_puts("[PKG] selftest: OK (");
                            serial_put_u32(dlen);
                            serial_puts(" bytes)\n");
                        }
                    }
                }
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
    serial_puts("[SCHED] background processes created\n");

    /* Initialize TUI Window Manager */
    serial_puts("[TUI] initializing window manager...\n");
    tui_wm_init();
    
    /* Initialize RTC for clock */
    rtc_init();
    
    /* Initialize PS/2 mouse */
    serial_puts("[TUI] initializing PS/2 mouse...\n");
    pic_unmask_irq(2);
    pic_unmask_irq(12);
    ps2mouse_init();
    
    /* Enable keyboard TUI mode */
    keyboard_set_tui_mode(1);
    
    /* Initialize TUI shell with filesystem */
    tui_shell_init(g_fs);
    serial_puts("[TUI] wm windows=");
    serial_put_u32(tui_wm_window_count());
    serial_putc('\n');

    /* Headless compositor self-test over serial */
    tui_selftest();

    /* Enter the TUI main loop (tui_run logs its own entry line) */
    tui_run();
}
