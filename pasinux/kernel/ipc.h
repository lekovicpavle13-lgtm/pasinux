#ifndef IPC_H
#define IPC_H

#include <stdint.h>

#include "driver.h"

#define IPC_NOT_HANDLED 0
#define IPC_HANDLED     1

void ipc_init(void);
int ipc_process_message(const ipc_message_t* msg);
uint64_t ipc_poll(uint64_t max_messages);

#endif
