#ifndef DUALCORE_BOARD_H
#define DUALCORE_BOARD_H
#include "control_ipc.h"
#define DC_BASE 0x221d3000UL
#define DC_BOOT_MAGIC 0x44434232UL
#define DC_READY 0x4d333352UL
#define DC_RPC_IDLE 0U
#define DC_RPC_REQUEST 1U
#define DC_RPC_BUSY 2U
#define DC_RPC_DONE 3U
/* Boot header and bus mailbox are separate from the supplied control protocol. */
typedef struct {
 uint32_t magic, session, inverse, ready;
 uint32_t smoke[2], fault, reserved;
 uint32_t rpc_state, rpc_seq, rpc_session, address;
 uint32_t reg, reg_bytes, write, value;
 int32_t result;
 uint32_t reserved2[15];
 ipc_shared_t control;
} dc_shared_t;
#define DC_SHARED ((volatile dc_shared_t *)DC_BASE)
typedef char dc_size_check[(sizeof(dc_shared_t)<=4096)?1:-1];
void dc_memory_init(void);
int dc_bind(ipc_endpoint_t *endpoint);
void dc_primary_start(void);
int dc_rpc_lock(void);
void dc_rpc_unlock(void);
#endif
