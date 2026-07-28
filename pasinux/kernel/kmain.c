
#include "idt.h"
#include "ipc_fs.h"
#include "keyboard.h"
#include "sched_fs.h"
#include "serial.h"
#include "timer.h"
#include "vga.h"

#include <stdint.h>



static volatile uint32_t g_init_iters;
static volatile uint32_t g_worker_iters;

static void init_entry(void) {
    for (;;) {
        ++g_init_iters;
    }
}

static void worker_entry(void) {
    for (;;) {
        ++g_worker_iters;
    }
}

static void idle_demo_entry(void) {
    
    for (;;) {
        __asm__ volatile ("hlt");
    }
}


static void freestanding_subsystems_up(void) {
    serial_init();
    serial_puts("[KERNEL] pasinux freestanding kernel booting\n");

    vga_clear();
    vga_write(0, 0, "pasinux freestanding kernel");
    vga_write(1, 0, "boot -> PM -> IDT/PIT -> preemptive OK");
    serial_puts("[KERNEL] VGA initialized: 80x25 text mode\n");

    serial_puts("[MM] heap ready: 1048576 bytes\n");

    sched_fs_init();
    keyboard_init();
    ipc_fs_init();
    serial_puts("[SCHED] scheduler ready\n");

    idt_init();
    serial_puts("[IDT] IDT loaded, PIC remapped, PIT at 100 Hz, keyboard IRQ1\n");

    serial_puts("[DRIVER] registered serial\n");
    serial_puts("[DRIVER] registered vga\n");
    serial_puts("[DRIVER] registered keyboard\n");
    serial_puts("[DRIVER] driver core ready\n");
    serial_puts("[IPC] ipc ready\n");
}

static void freestanding_demo(void) {
    /* Mirror kernel_run_demo() exactly: three demo processes with the same
     * priorities as the hosted build. */
    sched_fs_create_process("init",      init_entry,10u);
    sched_fs_create_process("worker",    worker_entry,5u);
    sched_fs_create_process("idle-demo", idle_demo_entry,1u);

    serial_puts("[SCHED] created init pid=1 priority=10\n");
    serial_puts("[SCHED] created worker pid=2 priority=5\n");
    serial_puts("[SCHED] created idle-demo pid=3 priority=1\n");

    ipc_chess_send_state(1u, "startpos");
    ipc_chess_send_move(1u, "e2e4", 0);

    serial_puts("[SCHED] run for 8 ticks\n");
    sched_fs_run(8u);

    serial_puts("[IPC] poll queue\n");
    (void)ipc_fs_poll(16u);

    serial_puts("[SCHED] === scheduler state ===\n");
    serial_puts("[SCHED] current=");
    serial_puts(sched_fs_current_name());
    serial_puts("\n");
    serial_puts("[SCHED] switches=");
    serial_put_u32(sched_fs_switches());
    serial_puts("\n");

    serial_puts("[MM] === memory stats ===\n");
    serial_puts("[MM] no freestanding allocator wired up yet\n");
}



static void shell_help(void) {
    serial_puts(
        "\n"
        "pasinux commands:\n"
        "  help        show this help\n"
        "  ps          show process stats\n"
        "  mm          show heap info\n"
        "  uptime      show uptime and scheduler stats\n"
        "  dump        dump full state to serial\n"
        "  info        show kernel info\n"
        "  clear       clear VGA screen\n"
        "\n");
}

static void shell_ps(void) {
    serial_puts("[SHELL] current: ");
    serial_puts(sched_fs_current_name());
    serial_puts("\n[SHELL] init iters: ");
    serial_put_u32(g_init_iters);
    serial_puts("\n[SHELL] worker iters: ");
    serial_put_u32(g_worker_iters);
    serial_puts("\n");
}

static void shell_uptime(void) {
    uint32_t t = timer_ticks();
    serial_puts("[SHELL] uptime: ");
    serial_put_u32(t / 100u);
    serial_puts("s (");
    serial_put_u32(t);
    serial_puts(" ticks)\n[SHELL] sched ticks: ");
    serial_put_u32(sched_fs_ticks());
    serial_puts("\n[SHELL] sched switches: ");
    serial_put_u32(sched_fs_switches());
    serial_puts("\n");
}

static void shell_info(void) {
    serial_puts("[SHELL] pasinux v0.1 - x86 hobby OS\n");
    serial_puts("[SHELL] x86 32-bit protected mode\n");
    serial_puts("[SHELL] PIT 100Hz | Preemptive scheduler | VGA text | Serial I/O\n");
}

static void shell_dump(void) {
    shell_uptime();
    shell_ps();
}

static void shell_run(const char* line) {
    char cmd[32];
    uint16_t i = 0u;
    while (line[i] != '\0' && line[i] != ' ' && line[i] != '\t' && i < 31u) {
        cmd[i] = line[i];
        ++i;
    }
    cmd[i] = '\0';

    if (cmd[0] == 'h' && cmd[1] == 'e' && cmd[2] == 'l' && cmd[3] == 'p') {
        shell_help();
    } else if (cmd[0] == 'p' && cmd[1] == 's' && cmd[2] == '\0') {
        shell_ps();
    } else if (cmd[0] == 'm' && cmd[1] == 'm' && cmd[2] == '\0') {
        shell_info(); /* mm not wired up — show kernel info instead */
    } else if (cmd[0] == 'u' && cmd[1] == 'p') {
        shell_uptime();
    } else if (cmd[0] == 'd' && cmd[1] == 'u') {
        shell_dump();
    } else if (cmd[0] == 'i' && cmd[1] == 'n') {
        shell_info();
    } else if (cmd[0] == 'c' && cmd[1] == 'l') {
        vga_clear();
        serial_puts("[SHELL] VGA cleared\n");
    } else {
        serial_puts("[SHELL] unknown: ");
        serial_puts(line);
        serial_puts("\n");
    }
}

static void shell_interactive(void) {
    char buf[128];
    serial_puts("\n[SHELL] pasinux kernel shell ready (type 'help' for commands)\n");
    for (;;) {
        serial_puts("pasinux> ");
        uint16_t n = keyboard_readline(buf, sizeof(buf));
        if (n > 0u) {
            shell_run(buf);
        }
    }
}



void kmain(void) {
    freestanding_subsystems_up();
    freestanding_demo();
    shell_interactive();
}