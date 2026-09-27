#include "hal_data.h"
#include "dualcore_board.h"
#include "ipc_ra8p1.h"
static bsp_ipc_semaphore_handle_t bus_sem = {.semaphore_num=4};
/* SysTick/PendSV/SVC are owned by the µT-Kernel port on both cores.
 * Camera/NPU code must use task waits or driver callbacks; it must not add a
 * second SysTick handler or program the reload register. */
int dc_rpc_lock(void){return R_BSP_IpcSemaphoreTake(&bus_sem)==FSP_SUCCESS?0:-1;}
void dc_rpc_unlock(void){__DMB();(void)R_BSP_IpcSemaphoreGive(&bus_sem);}
void dc_memory_init(void) {
 /* Region 7 reserved for shared RAM. FSP owns its other MPU regions. */
 __DMB();
 ARM_MPU_Disable();
 ARM_MPU_SetMemAttr(7,ARM_MPU_ATTR(ARM_MPU_ATTR_NON_CACHEABLE,ARM_MPU_ATTR_NON_CACHEABLE));
 ARM_MPU_SetRegion(7,ARM_MPU_RBAR(DC_BASE,ARM_MPU_SH_INNER,0,0,1),ARM_MPU_RLAR(DC_BASE+4095,7));
 ARM_MPU_Enable(MPU_CTRL_PRIVDEFENA_Msk);
 __DSB();__ISB();
}
int dc_bind(ipc_endpoint_t *e){return ipc_ra8p1_bind(e,&DC_SHARED->control,3);}
void dc_primary_start(void) {
#if BSP_CFG_CPU_CORE == 0
 dc_memory_init();
 /* Coordinated reset only: CPU1 must still be held at reset here. */
 uint32_t previous=DC_SHARED->session;
 uint32_t next=(DC_SHARED->magic==DC_BOOT_MAGIC && DC_SHARED->inverse==~previous)?previous+1U:1U;
 if(!next)next=1;
 DC_SHARED->ready=0;
 DC_SHARED->control.magic=0;
 DC_SHARED->rpc_state=DC_RPC_IDLE;
 DC_SHARED->magic=DC_BOOT_MAGIC;
 DC_SHARED->session=next;DC_SHARED->inverse=~next;
 DC_SHARED->smoke[0]=DC_SHARED->smoke[1]=DC_SHARED->fault=0;
 __DSB();R_BSP_SecondaryCoreStart();
#endif
}
