#include "driver.h"

#include "scheduler.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define MAX_MESSAGES 64u




volatile uint8_t* vga_buffer = (uint8_t*)0xB8000; 
uint16_t vga_ptr = 0; 
void vga_putchar(char c, uint8_t color) {
    if (c == '\n') {
        vga_ptr = (vga_ptr / 80 + 1) * 80;
    } else {
        vga_buffer[vga_ptr++] = (color << 8) | c;
    }
    if (vga_ptr >= 80*25) vga_clear(BLACK); 
}

void vga_clear(uint8_t color) {
    for (uint16_t i = 0; i < 80*25; i++) {
        vga_buffer[i] = (color << 8) | ' ';
    }
    vga_ptr = 0;
}

static driver_t* driver_list;
static msg_queue_t kernel_msg_queue;
static ipc_message_t message_pool[MAX_MESSAGES];
static bool message_used[MAX_MESSAGES];
static uint64_t next_msg_id = 1;

static ipc_message_t* allocate_message(void) {
    for (size_t i = 0; i < MAX_MESSAGES; i++) {
        if (!message_used[i]) {
            message_used[i] = true;
            memset(&message_pool[i], 0, sizeof(message_pool[i]));
            message_pool[i].msg_id = next_msg_id++;
            return &message_pool[i];
        }
    }
    return NULL;
}

static void enqueue_message(msg_queue_t* queue, ipc_message_t* msg) {
    ipc_message_t** head = &queue->queues[msg->priority];
    msg->next = NULL;
    if (!*head) {
        *head = msg;
    } else {
        ipc_message_t* tail = *head;
        while (tail->next) {
            tail = tail->next;
        }
        tail->next = msg;
    }
    queue->msg_count++;
}

static int console_init(void* device_data) {
    (void)device_data;
    printf("[DRIVER] console ready\n");
    return 0;
}

static int console_open(void* device_data, int flags) {
    (void)device_data;
    (void)flags;
    return 0;
}

static int console_close(void* device_data) {
    (void)device_data;
    return 0;
}

static kssize_t console_read(void* device_data, void* buf, size_t count) {
    (void)device_data;
    if (!buf || count == 0) {
        return 0;
    }

    int ch = getchar();
    if (ch == EOF) {
        return 0;
    }

    ((char*)buf)[0] = (char)ch;
    return 1;
}

static kssize_t console_write(void* device_data, const void* buf, size_t count) {
    (void)device_data;
    if (!buf || count == 0) {
        return 0;
    }

    fwrite(buf, 1, count, stdout);
    fflush(stdout);
    return (kssize_t)count;
}

static int console_ioctl(void* device_data, unsigned long request, void* arg) {
    (void)device_data;
    (void)request;
    (void)arg;
    return -1;
}

static const driver_ops_t console_ops = {
    .init = console_init,
    .open = console_open,
    .close = console_close,
    .read = console_read,
    .write = console_write,
    .ioctl = console_ioctl,
};

static driver_t console_driver = {
    .name = "console",
    .type = DRIVER_TYPE_CHAR,
    .device_data = NULL,
    .ops = &console_ops,
    .next = NULL,
};

void msg_queue_init(msg_queue_t* queue) {
    if (queue) {
        memset(queue, 0, sizeof(*queue));
    }
}

void drivers_init(void) {
    driver_list = NULL;
    memset(message_used, 0, sizeof(message_used));
    msg_queue_init(&kernel_msg_queue);
    driver_register(&console_driver);
    if (console_driver.ops && console_driver.ops->init) {
        console_driver.ops->init(console_driver.device_data);
    }
    printf("[DRIVER] driver core ready\n");
}

void driver_register(driver_t* driver) {
    if (!driver || !driver->name) {
        return;
    }

    driver->next = driver_list;
    driver_list = driver;
    printf("[DRIVER] registered %s\n", driver->name);
}

driver_t* driver_lookup(const char* name) {
    for (driver_t* driver = driver_list; driver; driver = driver->next) {
        if (strcmp(driver->name, name) == 0) {
            return driver;
        }
    }
    return NULL;
}

int msg_send(uint64_t dst_pid, const void* data, size_t size, uint8_t priority) {
    if (!data || size == 0 || priority > 3u || size > IPC_MAX_PAYLOAD) {
        return -1;
    }

    ipc_message_t* msg = allocate_message();
    if (!msg) {
        return -1;
    }

    process_t* current = scheduler_get_current();
    uint64_t src_pid = current ? current->pid : 0;
    msg->src_pid = src_pid;
    msg->dst_pid = dst_pid;
    msg->priority = priority;
    msg->timestamp = get_scheduler_stats() ? get_scheduler_stats()->scheduler_ticks : 0;
    msg->payload_size = size;
    memcpy(msg->payload, data, size);

    enqueue_message(&kernel_msg_queue, msg);
    printf("[DRIVER] queued message id=%llu src=%llu dst=%llu priority=%u size=%zu\n",
           (unsigned long long)msg->msg_id,
           (unsigned long long)msg->src_pid,
           (unsigned long long)msg->dst_pid,
           msg->priority,
           msg->payload_size);
    return 0;
}

ipc_message_t* msg_recv(uint64_t* src_pid, uint64_t timeout_ticks) {
    (void)timeout_ticks;

    for (int priority = 3; priority >= 0; priority--) {
        ipc_message_t* msg = kernel_msg_queue.queues[priority];
        if (msg) {
            kernel_msg_queue.queues[priority] = msg->next;
            msg->next = NULL;
            if (kernel_msg_queue.msg_count > 0) {
                kernel_msg_queue.msg_count--;
            }
            if (src_pid) {
                *src_pid = msg->src_pid;
            }
            return msg;
        }
    }

    return NULL;
}

void msg_free(ipc_message_t* msg) {
    if (!msg) {
        return;
    }

    size_t index = (size_t)(msg - message_pool);
    if (index < MAX_MESSAGES) {
        message_used[index] = false;
    }
}

static int8_t score_move(const char* move) {
    int score = 0;
    for (size_t i = 0; move && move[i]; i++) {
        score += (unsigned char)move[i];
    }
    return (int8_t)((score % 101) - 50);
}

void chess_send_move(uint64_t dst_pid, const char* move, int8_t promotion) {
    chess_message_t chess_msg;
    memset(&chess_msg, 0, sizeof(chess_msg));
    chess_msg.msg_type = CHESS_MSG_MOVE;
    snprintf(chess_msg.move_str, sizeof(chess_msg.move_str), "%s", move ? move : "");
    chess_msg.promotion = promotion;
    chess_msg.score = score_move(move);
    (void)msg_send(dst_pid, &chess_msg, sizeof(chess_msg), 2);
}

void chess_send_state(uint64_t dst_pid, const char* fen) {
    chess_message_t chess_msg;
    memset(&chess_msg, 0, sizeof(chess_msg));
    chess_msg.msg_type = CHESS_MSG_STATE;
    snprintf(chess_msg.move_str, sizeof(chess_msg.move_str), "%s", fen ? fen : "");
    (void)msg_send(dst_pid, &chess_msg, sizeof(chess_msg), 1);
}

void chess_send_draw_offer(uint64_t dst_pid) {
    chess_message_t chess_msg;
    memset(&chess_msg, 0, sizeof(chess_msg));
    chess_msg.msg_type = CHESS_MSG_DRAW_OFFER;
    (void)msg_send(dst_pid, &chess_msg, sizeof(chess_msg), 2);
}

void chess_send_draw_accept(uint64_t dst_pid) {
    chess_message_t chess_msg;
    memset(&chess_msg, 0, sizeof(chess_msg));
    chess_msg.msg_type = CHESS_MSG_DRAW_ACCEPT;
    (void)msg_send(dst_pid, &chess_msg, sizeof(chess_msg), 2);
}

void chess_send_resign(uint64_t dst_pid) {
    chess_message_t chess_msg;
    memset(&chess_msg, 0, sizeof(chess_msg));
    chess_msg.msg_type = CHESS_MSG_RESIGN;
    (void)msg_send(dst_pid, &chess_msg, sizeof(chess_msg), 0);
}

void chess_send_resign_accept(uint64_t dst_pid) {
    chess_message_t chess_msg;
    memset(&chess_msg, 0, sizeof(chess_msg));
    chess_msg.msg_type = CHESS_MSG_RESIGN_ACCEPT;
    (void)msg_send(dst_pid, &chess_msg, sizeof(chess_msg), 2);
}
