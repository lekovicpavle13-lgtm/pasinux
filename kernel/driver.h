#ifndef DRIVER_H
#define DRIVER_H

#include <stddef.h>
#include <stdint.h>

#define DRIVER_TYPE_CHAR  0u
#define DRIVER_TYPE_BLOCK 1u
#define DRIVER_TYPE_NET   2u
#define DRIVER_TYPE_INPUT 3u

#define IPC_MAX_PAYLOAD 256u

typedef enum { DRIVER_CONSOLE, DRIVER_VGA_TEXT } driver_type_t;


void vga_putchar(char c, uint8_t color);
void vga_clear(uint8_t color);
void vga_set_cursor(uint8_t x, uint8_t y);


typedef long long kssize_t;typedef struct driver_ops {
    int (*init)(void* device_data);
    int (*open)(void* device_data, int flags);
    int (*close)(void* device_data);
    kssize_t (*read)(void* device_data, void* buf, size_t count);
    kssize_t (*write)(void* device_data, const void* buf, size_t count);
    int (*ioctl)(void* device_data, unsigned long request, void* arg);
} driver_ops_t;

typedef struct driver {
    const char* name;
    uint8_t type;
    void* device_data;
    const driver_ops_t* ops;
    struct driver* next;
} driver_t;

typedef struct ipc_message {
    uint64_t msg_id;
    uint64_t src_pid;
    uint64_t dst_pid;
    uint8_t priority;
    uint64_t timestamp;
    size_t payload_size;
    unsigned char payload[IPC_MAX_PAYLOAD];
    struct ipc_message* next;
} ipc_message_t;

typedef struct {
    ipc_message_t* queues[4];
    uint64_t msg_count;
} msg_queue_t;

#define CHESS_MSG_MOVE          0x01u
#define CHESS_MSG_RESIGN        0x02u
#define CHESS_MSG_DRAW          0x03u
#define CHESS_MSG_RESIGN_ACCEPT 0x04u
#define CHESS_MSG_DRAW_OFFER    0x05u
#define CHESS_MSG_DRAW_ACCEPT   0x06u
#define CHESS_MSG_STATE         0x0Fu

typedef struct {
    uint8_t msg_type;
    char move_str[64];
    int8_t promotion;
    int8_t score;
} chess_message_t;

void drivers_init(void);
void driver_register(driver_t* driver);
driver_t* driver_lookup(const char* name);

void msg_queue_init(msg_queue_t* queue);
int msg_send(uint64_t dst_pid, const void* data, size_t size, uint8_t priority);
ipc_message_t* msg_recv(uint64_t* src_pid, uint64_t timeout_ticks);
void msg_free(ipc_message_t* msg);

void chess_send_move(uint64_t dst_pid, const char* move, int8_t promotion);
void chess_send_state(uint64_t dst_pid, const char* fen);
void chess_send_draw_offer(uint64_t dst_pid);
void chess_send_draw_accept(uint64_t dst_pid);
void chess_send_resign(uint64_t dst_pid);
void chess_send_resign_accept(uint64_t dst_pid);

#endif
