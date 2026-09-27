#include "hal_data.h"
#include "ipc_ra8p1.h"
static bsp_ipc_semaphore_handle_t handle;
static int bound;
static int take_sem(void *ctx) {return R_BSP_IpcSemaphoreTake(ctx)==FSP_SUCCESS?0:-1;}
static void give_sem(void *ctx) {(void)R_BSP_IpcSemaphoreGive(ctx);}
static void barrier(void *ctx) {(void)ctx;__DMB();}
int ipc_ra8p1_bind(ipc_endpoint_t *e,volatile ipc_shared_t *ram,unsigned semaphore) {
    if(bound||!e||!ram||((uintptr_t)ram&31U)||semaphore>=16)return -1;
    handle.semaphore_num=(uint8_t)semaphore;
    e->ram=ram;e->sync.ctx=&handle;e->sync.try_lock=take_sem;
    e->sync.unlock=give_sem;e->sync.barrier=barrier;bound=1;return 0;
}
