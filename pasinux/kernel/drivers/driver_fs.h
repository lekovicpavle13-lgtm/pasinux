#ifndef DRIVER_FS_H
#define DRIVER_FS_H

#include <stddef.h>
#include <stdint.h>

#define DRIVER_TYPE_CHAR  0u
#define DRIVER_TYPE_BLOCK 1u
#define DRIVER_TYPE_NET   2u
#define DRIVER_TYPE_INPUT 3u

typedef long long kssize_t;

typedef struct driver_ops {
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

void drivers_init_fs(void);

void driver_register(driver_t* driver);
driver_t* driver_lookup(const char* name);
driver_t* driver_get_list_head(void);

void ipc_fs_init(void);

void ipc_chess_send_state(uint64_t dst_pid, const char* fen);
void ipc_chess_send_move(uint64_t dst_pid, const char* move, int8_t promotion);
void ipc_chess_send_draw_offer(uint64_t dst_pid);
void ipc_chess_send_draw_accept(uint64_t dst_pid);
void ipc_chess_send_resign(uint64_t dst_pid);
void ipc_chess_send_resign_accept(uint64_t dst_pid);

uint64_t ipc_fs_poll(uint64_t max_messages);

#endif