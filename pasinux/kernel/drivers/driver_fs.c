#include "driver_fs.h"
#include "keyboard.h"
#include "serial.h"
#include "timer.h"
#include "vga.h"

#define MAX_MESSAGES 16u
#define NUM_QUEUES   4u

typedef struct ipc_fs_msg {
    uint64_t msg_id;
    uint64_t src_pid;
    uint64_t dst_pid;
    uint8_t priority;
    chess_message_t payload;
    struct ipc_fs_msg* next;
} ipc_fs_msg_t;

static ipc_fs_msg_t g_pool[MAX_MESSAGES];
static uint8_t g_used[MAX_MESSAGES];
static ipc_fs_msg_t* g_queues[NUM_QUEUES];
static uint64_t g_count;
static uint64_t g_next_id;


static driver_t* g_driver_list;

static void _msg_pool_init(void) {
    for (size_t i = 0u; i < MAX_MESSAGES; ++i) g_used[i] = 0u;
    for (size_t i = 0u; i < NUM_QUEUES; ++i) g_queues[i] = NULL;
    g_count = 0u;
    g_next_id = 0u;
}

static ipc_fs_msg_t* _alloc_msg(void) {
    for (size_t i = 0u; i < MAX_MESSAGES; ++i) {
        if (!g_used[i]) {
            g_used[i] = 1u;
            ipc_fs_msg_t* m = &g_pool[i];
            m->msg_id = ++g_next_id;
            m->src_pid = 0u;
            m->dst_pid = 0u;
            m->priority = 0u;
            for (size_t j = 0u; j < sizeof(m->payload.move_str); ++j)
                m->payload.move_str[j] = '\0';
            m->payload.msg_type = 0u;
            m->payload.promotion = 0;
            m->payload.score = 0;
            m->next = NULL;
            return m;
        }
    }
    return NULL;
}

static void _enqueue(ipc_fs_msg_t* msg) {
    if (!msg || msg->priority >= NUM_QUEUES) return;
    msg->next = NULL;
    ipc_fs_msg_t** head = &g_queues[msg->priority];
    if (!*head) {
        *head = msg;
    } else {
        ipc_fs_msg_t* tail = *head;
        while (tail->next) tail = tail->next;
        tail->next = msg;
    }
    ++g_count;
}

static ipc_fs_msg_t* _dequeue_highest(void) {
    for (int p = (int)NUM_QUEUES - 1; p >= 0; --p) {
        ipc_fs_msg_t* msg = g_queues[p];
        if (msg) {
            g_queues[p] = msg->next;
            msg->next = NULL;
            if (g_count > 0u) --g_count;
            return msg;
        }
    }
    return NULL;
}

static void _free_msg(ipc_fs_msg_t* msg) {
    if (!msg) return;
    size_t idx = (size_t)(msg - g_pool);
    if (idx < MAX_MESSAGES) g_used[idx] = 0u;
}

static void _copy_move(char* dst, size_t dst_size, const char* src) {
    if (!dst || dst_size == 0u) return;
    size_t i = 0u;
    if (src) {
        for (; src[i] != '\0' && i < dst_size - 1u; ++i) dst[i] = src[i];
    }
    dst[i] = '\0';
}

static int8_t _score_move(const char* move) {
    int score = 0;
    if (move) {
        for (size_t i = 0u; move[i] != '\0'; ++i) score += (unsigned char)move[i];
    }
    return (int8_t)((score % 101) - 50);
}

static int fs_serial_init(void* d) { (void)d; serial_init(); return 0; }
static int fs_serial_open(void* d, int f) { (void)d; (void)f; return 0; }
static int fs_serial_close(void* d) { (void)d; return 0; }
static kssize_t fs_serial_read(void* d, void* buf, size_t cnt) {
    (void)d; (void)buf; (void)cnt; return 0;
}
static kssize_t fs_serial_write(void* d, const void* buf, size_t cnt) {
    (void)d; (void)buf; (void)cnt; return 0;
}
static int fs_serial_ioctl(void* d, unsigned long r, void* a) {
    (void)d; (void)r; (void)a; return -1;
}
static const driver_ops_t serial_ops = {
    fs_serial_init, fs_serial_open, fs_serial_close,
    fs_serial_read, fs_serial_write, fs_serial_ioctl
};
static driver_t serial_driver = {
    "serial", DRIVER_TYPE_CHAR, NULL, &serial_ops, NULL
};

static int fs_vga_init(void* d) { (void)d; /* screen already initialized by kmain */ return 0; }
static int fs_vga_open(void* d, int f) { (void)d; (void)f; return 0; }
static int fs_vga_close(void* d) { (void)d; return 0; }
static kssize_t fs_vga_read(void* d, void* buf, size_t cnt) { (void)d; (void)buf; (void)cnt; return 0; }
static kssize_t fs_vga_write(void* d, const void* buf, size_t cnt) { (void)d; (void)buf; (void)cnt; return 0; }
static int fs_vga_ioctl(void* d, unsigned long r, void* a) { (void)d; (void)r; (void)a; return -1; }
static const driver_ops_t vga_ops = {
    fs_vga_init, fs_vga_open, fs_vga_close,
    fs_vga_read, fs_vga_write, fs_vga_ioctl
};
static driver_t vga_driver = {
    "vga", DRIVER_TYPE_CHAR, NULL, &vga_ops, NULL
};

static int fs_kbd_init(void* d) { (void)d; keyboard_init(); return 0; }
static int fs_kbd_open(void* d, int f) { (void)d; (void)f; return 0; }
static int fs_kbd_close(void* d) { (void)d; return 0; }
static kssize_t fs_kbd_read(void* d, void* buf, size_t cnt) { (void)d; (void)buf; (void)cnt; return 0; }
static kssize_t fs_kbd_write(void* d, const void* buf, size_t cnt) { (void)d; (void)buf; (void)cnt; return 0; }
static int fs_kbd_ioctl(void* d, unsigned long r, void* a) { (void)d; (void)r; (void)a; return -1; }
static const driver_ops_t kbd_ops = {
    fs_kbd_init, fs_kbd_open, fs_kbd_close,
    fs_kbd_read, fs_kbd_write, fs_kbd_ioctl
};
static driver_t kbd_driver = {
    "keyboard", DRIVER_TYPE_INPUT, NULL, &kbd_ops, NULL
};

static int fs_timer_init(void* d) { (void)d; return 0; }
static int fs_timer_open(void* d, int f) { (void)d; (void)f; return 0; }
static int fs_timer_close(void* d) { (void)d; return 0; }
static kssize_t fs_timer_read(void* d, void* buf, size_t cnt) { (void)d; (void)buf; (void)cnt; return 0; }
static kssize_t fs_timer_write(void* d, const void* buf, size_t cnt) { (void)d; (void)buf; (void)cnt; return 0; }
static int fs_timer_ioctl(void* d, unsigned long r, void* a) { (void)d; (void)r; (void)a; return -1; }
static const driver_ops_t timer_ops = {
    fs_timer_init, fs_timer_open, fs_timer_close,
    fs_timer_read, fs_timer_write, fs_timer_ioctl
};
static driver_t timer_driver = {
    "timer", DRIVER_TYPE_CHAR, NULL, &timer_ops, NULL
};

void driver_register(driver_t* driver) {
    if (!driver || !driver->name) return;
    driver->next = g_driver_list;
    g_driver_list = driver;
    serial_puts("[DRIVER] registered ");
    serial_puts(driver->name);
    serial_puts("\n");
}

driver_t* driver_lookup(const char* name) {
    for (driver_t* d = g_driver_list; d; d = d->next) {
        const char* a = d->name;
        const char* b = name;
        int match = 1;
        for (; *a && *b; ++a, ++b) { if (*a != *b) { match = 0; break; } }
        if (match && *a == '\0' && *b == '\0') return d;
    }
    return NULL;
}

driver_t* driver_get_list_head(void) { return g_driver_list; }

void ipc_fs_init(void) { _msg_pool_init(); }

static void _send_chess(uint64_t dst_pid, const chess_message_t* payload, uint8_t priority) {
    if (!payload) return;
    ipc_fs_msg_t* msg = _alloc_msg();
    if (!msg) { serial_puts("[IPC] pool exhausted\n"); return; }
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
    _enqueue(msg);
}

void ipc_chess_send_move(uint64_t dst_pid, const char* move, int8_t promotion) {
    chess_message_t p;
    p.msg_type = CHESS_MSG_MOVE;
    _copy_move(p.move_str, sizeof(p.move_str), move);
    p.promotion = promotion;
    p.score = _score_move(move);
    _send_chess(dst_pid, &p, 2u);
}

void ipc_chess_send_state(uint64_t dst_pid, const char* fen) {
    chess_message_t p;
    p.msg_type = CHESS_MSG_STATE;
    _copy_move(p.move_str, sizeof(p.move_str), fen);
    p.promotion = 0;
    p.score = 0;
    _send_chess(dst_pid, &p, 1u);
}

void ipc_chess_send_draw_offer(uint64_t dst_pid) {
    chess_message_t p;
    p.msg_type = CHESS_MSG_DRAW_OFFER;
    p.move_str[0] = '\0';
    p.promotion = 0;
    p.score = 0;
    _send_chess(dst_pid, &p, 2u);
}

void ipc_chess_send_draw_accept(uint64_t dst_pid) {
    chess_message_t p;
    p.msg_type = CHESS_MSG_DRAW_ACCEPT;
    p.move_str[0] = '\0';
    p.promotion = 0;
    p.score = 0;
    _send_chess(dst_pid, &p, 2u);
}

void ipc_chess_send_resign(uint64_t dst_pid) {
    chess_message_t p;
    p.msg_type = CHESS_MSG_RESIGN;
    p.move_str[0] = '\0';
    p.promotion = 0;
    p.score = 0;
    _send_chess(dst_pid, &p, 0u);
}

void ipc_chess_send_resign_accept(uint64_t dst_pid) {
    chess_message_t p;
    p.msg_type = CHESS_MSG_RESIGN_ACCEPT;
    p.move_str[0] = '\0';
    p.promotion = 0;
    p.score = 0;
    _send_chess(dst_pid, &p, 2u);
}

static int _dispatch(const chess_message_t* chess_msg) {
    if (!chess_msg) return 0;
    switch (chess_msg->msg_type) {
    case CHESS_MSG_MOVE:
        serial_puts("[IPC] chess move: ");
        serial_puts(chess_msg->move_str);
        serial_puts(" promotion=");
        serial_put_u32((uint32_t)(int8_t)chess_msg->promotion);
        serial_puts(" score=");
        serial_put_i32((int32_t)(int8_t)chess_msg->score);
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
    default: {
        serial_puts("[IPC] unhandled chess message type=0x");
        char hex[3];
        uint8_t v = chess_msg->msg_type;
        const char* digits = "0123456789abcdef";
        hex[0] = digits[(v >> 4u) & 0x0Fu];
        hex[1] = digits[v & 0x0Fu];
        hex[2] = '\0';
        serial_puts(hex);
        serial_puts("\n");
        return 0;
    }
    }
}

uint64_t ipc_fs_poll(uint64_t max_messages) {
    uint64_t handled = 0u;
    while (handled < max_messages) {
        ipc_fs_msg_t* msg = _dequeue_highest();
        if (!msg) break;
        if (_dispatch(&msg->payload)) ++handled;
        _free_msg(msg);
    }
    return handled;
}

void drivers_init_fs(void) {
    g_driver_list = NULL;

    driver_register(&serial_driver);
    if (serial_driver.ops && serial_driver.ops->init)
        serial_driver.ops->init(serial_driver.device_data);

    driver_register(&vga_driver);
    if (vga_driver.ops && vga_driver.ops->init)
        vga_driver.ops->init(vga_driver.device_data);

    driver_register(&kbd_driver);
    if (kbd_driver.ops && kbd_driver.ops->init)
        kbd_driver.ops->init(kbd_driver.device_data);

    driver_register(&timer_driver);
    if (timer_driver.ops && timer_driver.ops->init)
        timer_driver.ops->init(timer_driver.device_data);

    _msg_pool_init();

    serial_puts("[DRIVER] driver core ready\n");
}