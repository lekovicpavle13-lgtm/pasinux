#include "ipc.h"

#include <stdio.h>

void ipc_init(void) {
    printf("[IPC] ipc ready\n");
}

int ipc_process_message(const ipc_message_t* msg) {
    if (!msg) {
        return IPC_NOT_HANDLED;
    }

    if (msg->payload_size != sizeof(chess_message_t)) {
        printf("[IPC] unhandled message id=%llu size=%zu (expected %zu)\n",
               (unsigned long long)msg->msg_id,
               msg->payload_size,
               sizeof(chess_message_t));
        return IPC_NOT_HANDLED;
    }

    const chess_message_t* chess_msg = (const chess_message_t*)msg->payload;
    switch (chess_msg->msg_type) {
    case CHESS_MSG_MOVE:
        printf("[IPC] chess move from pid=%llu to pid=%llu: %s promotion=%d score=%d\n",
               (unsigned long long)msg->src_pid,
               (unsigned long long)msg->dst_pid,
               chess_msg->move_str,
               chess_msg->promotion,
               chess_msg->score);
        return IPC_HANDLED;

    case CHESS_MSG_STATE:
        printf("[IPC] chess state: %s\n", chess_msg->move_str);
        return IPC_HANDLED;

    case CHESS_MSG_DRAW_OFFER:
        printf("[IPC] draw offer\n");
        return IPC_HANDLED;

    case CHESS_MSG_DRAW_ACCEPT:
        printf("[IPC] draw accepted\n");
        return IPC_HANDLED;

    case CHESS_MSG_RESIGN:
        printf("[IPC] resignation\n");
        return IPC_HANDLED;

    case CHESS_MSG_RESIGN_ACCEPT:
        printf("[IPC] resignation accepted\n");
        return IPC_HANDLED;

    default:
        printf("[IPC] unhandled chess message id=%llu type=0x%02x\n",
               (unsigned long long)msg->msg_id,
               chess_msg->msg_type);
        return IPC_NOT_HANDLED;
    }
}

uint64_t ipc_poll(uint64_t max_messages) {
    uint64_t handled = 0;

    while (handled < max_messages) {
        uint64_t src_pid = 0;
        ipc_message_t* msg = msg_recv(&src_pid, 0);

        if (!msg) {
            break;
        }

        (void)src_pid;

        if (ipc_process_message(msg) == IPC_HANDLED) {
            handled++;
        }

        msg_free(msg);
    }

    return handled;
}
