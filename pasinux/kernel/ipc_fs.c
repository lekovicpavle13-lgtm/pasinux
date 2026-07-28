#include "ipc_fs.h"

#include "serial.h"

#include "io.h"

#define IPC_FS_MAX_MESSAGES 16u
#define IPC_FS_QUEUES       4u

typedef struct ipc_fs_msg {
    uint64_t msg_id;
    uint64_t src_pid;
    uint64_t dst_pid;
    uint8_t priority;
    chess_message_t payload;
    struct ipc_fs_msg* next;
} ipc_fs_msg_t;

static ipc_fs_msg_t g_pool[IPC_FS_MAX_MESSAGES];
static uint8_t g_used[IPC_FS_MAX_MESSAGES];
static ipc_fs_msg_t* g_queues[IPC_FS_QUEUES];
static uint64_t g_count;
static uint64_t g_next_id;

static ipc_fs_msg_t* alloc_msg(void) {
    for (size_t i = 0; i < IPC_FS_MAX_MESSAGES; ++i) {
        if (!g_used[i]) {
            g_used[i] = 1u;
            g_pool[i].msg_id = ++g_next_id;
            g_pool[i].src_pid = 0u;
            g_pool[i].dst_pid = 0u;
            g_pool[i].priority = 0u;
            for (size_t j = 0; j < sizeof(g_pool[i].payload.move_str); ++j) {
                g_pool[i].payload.move_str[j] = '\0';
            }
            g_pool[i].payload.msg_type = 0u;
            g_pool[i].payload.promotion = 0;
            g_pool[i].payload.score = 0;
            g_pool[i].next = NULL;
            return &g_pool[i];
        }
    }
    return NULL;
}

static void enqueue(ipc_fs_msg_t* msg) {
    if (!msg || msg->priority >= IPC_FS_QUEUES) {
        return;
    }
    msg->next = NULL;
    ipc_fs_msg_t** head = &g_queues[msg->priority];
    if (!*head) {
        *head = msg;
    } else {
        ipc_fs_msg_t* tail = *head;
        while (tail->next) {
            tail = tail->next;
        }
        tail->next = msg;
    }
    ++g_count;
}

static ipc_fs_msg_t* dequeue_highest(void) {
    for (int p = (int)IPC_FS_QUEUES - 1; p >= 0; --p) {
        ipc_fs_msg_t* msg = g_queues[p];
        if (msg) {
            g_queues[p] = msg->next;
            msg->next = NULL;
            if (g_count > 0u) {
                --g_count;
            }
            return msg;
        }
    }
    return NULL;
}

static void free_msg(ipc_fs_msg_t* msg) {
    if (!msg) {
        return;
    }
    size_t idx = (size_t)(msg - g_pool);
    if (idx < IPC_FS_MAX_MESSAGES) {
        g_used[idx] = 0u;
    }
}

static void copy_move(char* dst, size_t dst_size, const char* src) {
    if (!dst || dst_size == 0u) {
        return;
    }
    size_t i = 0u;
    if (src) {
        for (; src[i] != '\0' && i < dst_size - 1u; ++i) {
            dst[i] = src[i];
        }
    }
    dst[i] = '\0';
}

static int8_t score_move(const char* move) {
    int score = 0;
    if (move) {
        for (size_t i = 0u; move[i] != '\0'; ++i) {
            score += (unsigned char)move[i];
        }
    }
    return (int8_t)((score % 101) - 50);
}

void ipc_fs_init(void) {
    for (size_t i = 0; i < IPC_FS_MAX_MESSAGES; ++i) {
        g_used[i] = 0u;
    }
    for (size_t i = 0; i < IPC_FS_QUEUES; ++i) {
        g_queues[i] = NULL;
    }
    g_count = 0u;
    g_next_id = 0u;
}

void ipc_fs_reset(void) {
    ipc_fs_init();
}

static void send_chess(uint64_t dst_pid, const chess_message_t* payload, uint8_t priority) {
    if (!payload) {
        return;
    }
    ipc_fs_msg_t* msg = alloc_msg();
    if (!msg) {
        serial_puts("[IPC] pool exhausted\n");
        return;
    }
    msg->src_pid = 1u; 
    msg->dst_pid = dst_pid;
    msg->priority = priority;
    msg->payload = *payload;

    serial_puts("[DRIVER] queued message id=");
    serial_put_u32((uint32_t)msg->msg_id);
    serial_puts(" src=");
    serial_put_u32((uint32_t)msg->src_pid);
    serial_puts(" dst=");
    serial_put_u32((uint32_t)msg->dst_pid);
    serial_puts(" priority=");
    serial_put_u32(msg->priority);
    serial_puts(" size=");
    serial_put_u32((uint32_t)sizeof(chess_message_t));
    serial_puts("\n");
    enqueue(msg);
}

void ipc_chess_send_move(uint64_t dst_pid, const char* move, int8_t promotion) {
    chess_message_t payload;
    payload.msg_type = CHESS_MSG_MOVE;
    copy_move(payload.move_str, sizeof(payload.move_str), move);
    payload.promotion = promotion;
    payload.score = score_move(move);
    send_chess(dst_pid, &payload, 2u);
}

void ipc_chess_send_state(uint64_t dst_pid, const char* fen) {
    chess_message_t payload;
    payload.msg_type = CHESS_MSG_STATE;
    copy_move(payload.move_str, sizeof(payload.move_str), fen);
    payload.promotion = 0;
    payload.score = 0;
    send_chess(dst_pid, &payload, 1u);
}

void ipc_chess_send_draw_offer(uint64_t dst_pid) {
    chess_message_t payload;
    payload.msg_type = CHESS_MSG_DRAW_OFFER;
    payload.move_str[0] = '\0';
    payload.promotion = 0;
    payload.score = 0;
    send_chess(dst_pid, &payload, 2u);
}

void ipc_chess_send_draw_accept(uint64_t dst_pid) {
    chess_message_t payload;
    payload.msg_type = CHESS_MSG_DRAW_ACCEPT;
    payload.move_str[0] = '\0';
    payload.promotion = 0;
    payload.score = 0;
    send_chess(dst_pid, &payload, 2u);
}

void ipc_chess_send_resign(uint64_t dst_pid) {
    chess_message_t payload;
    payload.msg_type = CHESS_MSG_RESIGN;
    payload.move_str[0] = '\0';
    payload.promotion = 0;
    payload.score = 0;
    send_chess(dst_pid, &payload, 0u);
}

void ipc_chess_send_resign_accept(uint64_t dst_pid) {
    chess_message_t payload;
    payload.msg_type = CHESS_MSG_RESIGN_ACCEPT;
    payload.move_str[0] = '\0';
    payload.promotion = 0;
    payload.score = 0;
    send_chess(dst_pid, &payload, 2u);
}

static int dispatch(const chess_message_t* chess_msg) {
    if (!chess_msg) {
        return 0;
    }
    switch (chess_msg->msg_type) {
    case CHESS_MSG_MOVE:
        serial_puts("[IPC] chess move: ");
        serial_puts(chess_msg->move_str);
        serial_puts(" promotion=");
        serial_put_u32((uint32_t)(int8_t)chess_msg->promotion);
        serial_puts(" score=");
        serial_put_u32((uint32_t)(int8_t)chess_msg->score);
        serial_puts("\n");
        return 1;
    case CHESS_MSG_STATE:
        serial_puts("[IPC] chess state: ");
        serial_puts(chess_msg->move_str);
        serial_puts("\n");
        return 1;
    case CHESS_MSG_DRAW_OFFER:
        serial_puts("[IPC] draw offer\n");
        return 1;
    case CHESS_MSG_DRAW_ACCEPT:
        serial_puts("[IPC] draw accepted\n");
        return 1;
    case CHESS_MSG_RESIGN:
        serial_puts("[IPC] resignation\n");
        return 1;
    case CHESS_MSG_RESIGN_ACCEPT:
        serial_puts("[IPC] resignation accepted\n");
        return 1;
    default:
        serial_puts("[IPC] unhandled chess message type=0x");
        
        {
            char hex[3];
            uint8_t v = chess_msg->msg_type;
            const char* digits = "0123456789abcdef";
            hex[0] = digits[(v >> 4) & 0x0Fu];
            hex[1] = digits[v & 0x0Fu];
            hex[2] = '\0';
            serial_puts(hex);
        }
        serial_puts("\n");
        return 0;
    }
}

uint64_t ipc_fs_poll(uint64_t max_messages) {
    uint64_t handled = 0u;
    while (handled < max_messages) {
        ipc_fs_msg_t* msg = dequeue_highest();
        if (!msg) {
            break;
        }
        if (dispatch(&msg->payload)) {
            ++handled;
        }
        free_msg(msg);
    }
    return handled;
}