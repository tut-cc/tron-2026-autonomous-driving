#include "hal_data.h"
#include "dualcore_board.h"
#include <tk/tkernel.h>
#include "dualcore/bus_owner.h"
extern void knl_start_mtkernel(void);
static ipc_endpoint_t endpoint;
static volatile unsigned smoke_bad;
static void smoke(INT index,void *unused) {
 (void)unused;
 /* Live FP value crosses a blocking call and a competing FP task repeatedly. */
 float value=(float)(index+1);
 for(unsigned i=0;i<20;i++) {
  value=value*1.25f+0.5f;
  float expected=value;
  __asm__ volatile("vmov s16, %0"::"r"(*(uint32_t *)&value):"s16");
  if(tk_dly_tsk((RELTIM)(10+index*10))!=E_OK)smoke_bad=1;
  uint32_t bits;__asm__ volatile("vmov %0, s16":"=r"(bits));
  if(bits!=*(uint32_t *)&expected)smoke_bad=1;
  DC_SHARED->smoke[index]++;
 }
 tk_ext_tsk();
}
static void supervisor(INT n,void *x) {
 (void)n;(void)x;
 for(int i=0;i<2;i++) {
  T_CTSK t={0};t.tskatr=TA_HLNG|TA_RNG0|TA_FPU;t.task=smoke;t.itskpri=(PRI)(10+i);t.stksz=2048;
  ID id=tk_cre_tsk(&t);if(id<=0||tk_sta_tsk(id,i)!=E_OK)goto fail;
 }
 (void)tk_dly_tsk(1000);
 if(smoke_bad||DC_SHARED->smoke[0]!=20||DC_SHARED->smoke[1]!=20)goto fail;
 /* FSP handlers use kernel services through the kernel's task-independent wrapper. */
 const INT irq[5]={0,6,7,8,9};
 const FP handlers[5]={(FP)gpt_counter_overflow_isr,(FP)iic_master_rxi_isr,(FP)iic_master_txi_isr,(FP)iic_master_tei_isr,(FP)iic_master_eri_isr};
 for(unsigned i=0;i<5;i++) {
  T_DINT d={0};d.intatr=TA_HLNG;d.inthdr=handlers[i];if(tk_def_int((UINT)irq[i],&d)!=E_OK)goto fail;
 }
 if(dc_bind(&endpoint))goto fail;
 control_runtime_config_t cfg={&endpoint,DC_SHARED->session,bus_tof_poll};
 if(control_runtime_start(&cfg)||bus_owner_start())goto fail;
 tk_ext_tsk();return;
fail:
 DC_SHARED->fault=1;DC_SHARED->ready=0;
 for(;;)(void)tk_dly_tsk(1000);
}
INT usermain(void) {
 T_CTSK t={0};t.tskatr=TA_HLNG|TA_RNG0|TA_FPU;t.task=supervisor;t.itskpri=9;t.stksz=4096;
 ID id=tk_cre_tsk(&t);
 if(id>0&&tk_sta_tsk(id,0)==E_OK)tk_ext_tsk();
 DC_SHARED->fault=2;for(;;)(void)tk_dly_tsk(1000);
}
void hal_entry(void) {
 dc_memory_init();
 if(DC_SHARED->magic!=DC_BOOT_MAGIC||!DC_SHARED->session||DC_SHARED->inverse!=~DC_SHARED->session)for(;;)__WFI();
 knl_start_mtkernel();
}
void R_BSP_WarmStart(bsp_warm_start_event_t event) {
 if(event==BSP_WARM_START_POST_C) {
  if(R_IOPORT_Open(&g_ioport_ctrl,&g_bsp_pin_cfg)!=FSP_SUCCESS)for(;;)__WFI();
 }
}
