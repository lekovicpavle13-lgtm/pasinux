#include "driver.h"
#include "ipc.h"
#include "mm.h"
#include "scheduler.h"

#include <stdio.h>

static void init_entry(void);
static void worker_entry(void);
static void idle_entry_demo(void);

void kernel_run_demo(void) {
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



static void spawned_entry(void) {
    scheduler_yield();
}

void kernel_init_all(void) {
    init_memory();
    scheduler_init();
    drivers_init();
    ipc_init();
}

void kernel_reset(void) {
    printf("[KERNEL] reset\n");
    kernel_init_all();
}

process_t* kernel_spawn_process(const char* name, uint8_t priority) {
    return create_process(spawned_entry, name, priority);
}


#ifndef PASINUX_GUI_BUILD
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static void shell_help(void) {
    printf("\n"
           "pasinux kernel shell commands:\n"
           "  help           show this help\n"
           "  ps             list all processes\n"
           "  mm             show memory stats\n"
           "  sched          dump scheduler state\n"
           "  spawn <name> <pri>  spawn a new process (pri: low|normal|high)\n"
           "  send <pid> <move>   send a chess move\n"
           "  run [ticks]    run scheduler for N ticks (default 4)\n"
           "  reap           collect zombie processes\n"
           "  reset          reset all kernel subsystems\n"
           "  quit           exit the simulator\n"
           "\n");
}

static void shell_ps(void) {
    process_t* cur = scheduler_get_current();
    printf("[SHELL] current: %s pid=%llu state=%u pri=%u\n",
           cur ? cur->name : "none",
           cur ? (unsigned long long)cur->pid : 0ULL,
           cur ? cur->state : 0,
           cur ? cur->priority : 0);

    process_t* head = scheduler_get_ready_head();
    if (!head) {
        printf("[SHELL] ready queue: empty\n");
        return;
    }

    process_t* p = head;
    do {
        printf("  %-12s pid=%-5llu pri=%-2u state=%u cpu=%llu\n",
               p->name,
               (unsigned long long)p->pid,
               p->priority,
               p->state,
               (unsigned long long)p->cpu_time);
        p = process_get_next(p);
    } while (p && p != head);
}

static void shell_run_tokens(int argc, char** argv) {
    if (argc < 1) {
        printf("[SHELL] missing command\n");
        return;
    }

    const char* cmd = argv[0];

    if (strcmp(cmd, "help") == 0) {
        shell_help();
    } else if (strcmp(cmd, "ps") == 0) {
        shell_ps();
    } else if (strcmp(cmd, "mm") == 0) {
        print_memory_stats();
    } else if (strcmp(cmd, "sched") == 0) {
        scheduler_dump_state();
        ipc_poll(16);
    } else if (strcmp(cmd, "spawn") == 0) {
        const char* name = (argc > 1) ? argv[1] : "proc";
        uint8_t priority = SCHED_PRIORITY_NORMAL;
        if (argc > 2) {
            if (strcmp(argv[2], "high") == 0) {
                priority = SCHED_PRIORITY_HIGH;
            } else if (strcmp(argv[2], "low") == 0) {
                priority = SCHED_PRIORITY_LOW;
            }
        }
        create_process(spawned_entry, name, priority);
    } else if (strcmp(cmd, "send") == 0) {
        uint64_t pid = (argc > 1) ? (uint64_t)strtoull(argv[1], NULL, 10) : 1;
        const char* move = (argc > 2) ? argv[2] : "e2e4";
        chess_send_move(pid, move, 0);
        ipc_poll(4);
    } else if (strcmp(cmd, "run") == 0) {
        uint64_t ticks = (argc > 1) ? (uint64_t)strtoull(argv[1], NULL, 10) : 4;
        printf("[SHELL] running scheduler for %llu ticks\n",
               (unsigned long long)ticks);
        scheduler_run(ticks);
        ipc_poll(16);
    } else if (strcmp(cmd, "reap") == 0) {
        process_reap_zombies();
    } else if (strcmp(cmd, "reset") == 0) {
        kernel_reset();
    } else if (strcmp(cmd, "quit") == 0) {
        printf("[SHELL] goodbye\n");
        return;
    } else {
        printf("[SHELL] unknown command: %s  (try 'help')\n", cmd);
    }
}

static void shell_split(char* line, char** argv, int* argc) {
    *argc = 0;
    while (*line) {
        while (*line && isspace((unsigned char)*line)) {
            *line++ = '\0';
        }
        if (!*line) {
            break;
        }
        argv[(*argc)++] = line;
        if (*argc >= 16) {
            break;
        }
        while (*line && !isspace((unsigned char)*line)) {
            line++;
        }
    }
}

static void kernel_shell(void) {
    printf("[SHELL] pasinux kernel shell ready (type 'help' for commands)\n");
    char line[256];
    char* argv[16];
    int argc;

    for (;;) {
        printf("pasinux> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }

        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }

        if (len == 0) {
            continue;
        }

        shell_split(line, argv, &argc);
        if (argc > 0) {
            if (strcmp(argv[0], "quit") == 0) {
                printf("[SHELL] goodbye\n");
                break;
            }
            shell_run_tokens(argc, argv);
        }
    }
}
#endif


void kernel_main(void) {
    kernel_run_demo();

#ifndef PASINUX_GUI_BUILD
    kernel_shell();
#endif
}



#ifndef PASINUX_GUI_BUILD
int main(void) {
    kernel_main();
    return 0;
}
#endif
