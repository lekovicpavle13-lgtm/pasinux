#ifndef IPC_FS_H
#define IPC_FS_H

#include <stddef.h>
#include <stdint.h>


#define CHESS_MSG_MOVE          0x01u
#define CHESS_MSG_STATE         0x0Fu
#define CHESS_MSG_DRAW_OFFER    0x05u
#define CHESS_MSG_DRAW_ACCEPT   0x06u
#define CHESS_MSG_RESIGN        0x02u
#define CHESS_MSG_RESIGN_ACCEPT 0x04u

typedef struct {
    uint8_t msg_type;
    char move_str[64];
    int8_t promotion;
    int8_t score;
} chess_message_t;

void ipc_fs_init(void);


void ipc_chess_send_state(uint64_t dst_pid, const char* fen);
void ipc_chess_send_move(uint64_t dst_pid, const char* move, int8_t promotion);
void ipc_chess_send_draw_offer(uint64_t dst_pid);
void ipc_chess_send_draw_accept(uint64_t dst_pid);
void ipc_chess_send_resign(uint64_t dst_pid);
void ipc_chess_send_resign_accept(uint64_t dst_pid);

uint64_t ipc_fs_poll(uint64_t max_messages);


void ipc_fs_reset(void);

#endif