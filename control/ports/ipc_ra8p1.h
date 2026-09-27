#ifndef IPC_RA8P1_H
#define IPC_RA8P1_H
#include "control_ipc.h"
/* Owner must reserve memory and configure BOTH MPUs non-cacheable first.
 * HSEM number must be unused by BSP boot/network/AI code. */
int ipc_ra8p1_bind(ipc_endpoint_t *,volatile ipc_shared_t *,unsigned semaphore);
#endif
