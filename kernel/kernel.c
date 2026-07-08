#include "driver.h"
#include "ipc.h"
#include "mm.h"
#include "scheduler.h"
#include <stddef.h>

void kernel_main(void) {
    printf("[KERNEL] pasinux kernel core starting\n");
    init_memory();
    scheduler_init();
    drivers_init();
    ipc_init();

    (void)create_process(init_entry, "init", SCHED_PRIORITY_HIGH);
    (void)create_process(worker_entry, "worker", SCHED_PRIORITY_NORMAL);
    (void)create_process(idle_entry_demo, "idle-demo", SCHED_PRIORITY_LOW);

    scheduler_run(8);
    (void)ipc_poll(16);

    scheduler_dump_state();
    print_memory_stats();
    printf("[KERNEL] shutdown complete\n");
}

static void init_entry(void) {
    static int ran;
    if (ran) {
        scheduler_yield();
        return;
    }

    ran = 1;
    printf("[KERNEL] init process running\n");
    chess_send_state(1, "startpos");
    chess_send_move(1, "e2e4", 0);
    scheduler_yield();
}

static void worker_entry(void) {
    static int ran;
    if (ran) {
        scheduler_yield();
        return;
    }

    ran = 1;
    printf("[KERNEL] worker process running\n");
    chess_send_move(1, "e7e5", 0);
    scheduler_yield();
}

static void idle_entry_demo(void) {
    scheduler_yield();
}

int main(void) {
    kernel_main();
    return 0;
}
