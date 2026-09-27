#include "hal_data.h"
#include <tk/tkernel.h>
#include "dualcore_board.h"
#include "bus_owner.h"
#include "tof/VL53L1X_api.h"
#include <string.h>
static volatile unsigned completed, failed;
static ID completion_flag;
static tof_safety_result_t latest;
static uint32_t seen;
static uint8_t tx[260];
void g_i2c_master_for_peripheral_callback(i2c_master_callback_args_t *a) {
 if(a->event==I2C_MASTER_EVENT_ABORTED){failed=1;completed=1;}
 else if(a->event==I2C_MASTER_EVENT_RX_COMPLETE||a->event==I2C_MASTER_EVENT_TX_COMPLETE)completed=1;
 if(completed&&completion_flag>0)(void)tk_set_flg(completion_flag,1);
}
static int await_transfer(fsp_err_t rc) {
 if(rc!=FSP_SUCCESS)return -1;
 UINT pattern;
 if(tk_wai_flg(completion_flag,1,TWF_ORW|TWF_BITCLR,&pattern,20)!=E_OK){(void)R_IIC_MASTER_Abort(&g_i2c_master_for_peripheral_ctrl);return -1;}
 return failed?-1:0;
}
static int bus_transfer(uint32_t address,uint16_t reg,unsigned regbytes,uint8_t *data,unsigned n,int write) {
 if(!data||!n||n>256||(regbytes!=1&&regbytes!=2))return -1;
 if(R_IIC_MASTER_SlaveAddressSet(&g_i2c_master_for_peripheral_ctrl,address,I2C_MASTER_ADDR_MODE_7BIT)!=FSP_SUCCESS)return -1;
 tx[0]=regbytes==2?(uint8_t)(reg>>8):(uint8_t)reg;
 if(regbytes==2)tx[1]=(uint8_t)reg;
 if(write)memcpy(tx+regbytes,data,n);
 completed=failed=0;
 (void)tk_clr_flg(completion_flag,0);
 if(await_transfer(R_IIC_MASTER_Write(&g_i2c_master_for_peripheral_ctrl,tx,regbytes+(write?n:0),!write)))return -1;
 if(!write){completed=failed=0;(void)tk_clr_flg(completion_flag,0);return await_transfer(R_IIC_MASTER_Read(&g_i2c_master_for_peripheral_ctrl,data,n,false));}
 return 0;
}
int8_t VL53L1_WriteMulti(uint16_t dev,uint16_t r,uint8_t *p,uint32_t n){return (int8_t)bus_transfer(dev>>1,r,2,p,n,1);}
int8_t VL53L1_ReadMulti(uint16_t dev,uint16_t r,uint8_t *p,uint32_t n){return (int8_t)bus_transfer(dev>>1,r,2,p,n,0);}
int8_t VL53L1_WrByte(uint16_t d,uint16_t r,uint8_t v){return VL53L1_WriteMulti(d,r,&v,1);}
int8_t VL53L1_WrWord(uint16_t d,uint16_t r,uint16_t v){uint8_t b[2]={(uint8_t)(v>>8),(uint8_t)v};return VL53L1_WriteMulti(d,r,b,2);}
int8_t VL53L1_WrDWord(uint16_t d,uint16_t r,uint32_t v){uint8_t b[4]={(uint8_t)(v>>24),(uint8_t)(v>>16),(uint8_t)(v>>8),(uint8_t)v};return VL53L1_WriteMulti(d,r,b,4);}
int8_t VL53L1_RdByte(uint16_t d,uint16_t r,uint8_t *v){return VL53L1_ReadMulti(d,r,v,1);}
int8_t VL53L1_RdWord(uint16_t d,uint16_t r,uint16_t *v){uint8_t b[2];int8_t rc=VL53L1_ReadMulti(d,r,b,2);if(!rc)*v=(uint16_t)((b[0]<<8)|b[1]);return rc;}
int8_t VL53L1_RdDWord(uint16_t d,uint16_t r,uint32_t *v){uint8_t b[4];int8_t rc=VL53L1_ReadMulti(d,r,b,4);if(!rc)*v=((uint32_t)b[0]<<24)|((uint32_t)b[1]<<16)|((uint32_t)b[2]<<8)|b[3];return rc;}
int8_t VL53L1_WaitMs(uint16_t d,int32_t ms){(void)d;return tk_dly_tsk((RELTIM)ms)==E_OK?0:-1;}
static int tof_init(void) {
 uint8_t id=0,boot=0;
 if(VL53L1_RdByte(0x52,0x010f,&id)||id!=0xea)return -1;
 for(unsigned i=0;i<200;i++) {
  if(VL53L1X_BootState(0x52,&boot))return -1;
  if(boot)break;
  (void)tk_dly_tsk(10);
 }
 if(!boot||VL53L1X_SensorInit(0x52)||VL53L1X_SetDistanceMode(0x52,2)||
    VL53L1X_SetTimingBudgetInMs(0x52,50)||VL53L1X_SetInterMeasurementInMs(0x52,60)||
    VL53L1X_StartRanging(0x52))return -1;
 return 0;
}
/* Open-space handling (TRON demo decision 2026-09-25).
 * ULD range status 2 (signal fail) and 4 (out of bounds) mean "no usable return",
 * which is what the VL53L1X reports when nothing is inside its range.  They are
 * treated as "clear ahead" only when the sensor's own estimate is also far, and
 * are then published as a fixed far distance.  Status 7 (wrap-around) means the
 * phase is ambiguous and the true target is at or beyond the reported value
 * (aliasing only ever makes a target look closer), so the reported distance is
 * published as a conservative lower bound and the normal 150/220mm stop/release
 * thresholds still apply (bench: 6-7% of samples were status 7 at 0.9-1.9m).  Every other non-zero status
 * (sigma fail, min range, hardware fail, ...) and every I2C error
 * stays invalid, which latches the M33 emergency stop exactly as before. */
#define TOF_CLEAR_MIN_ESTIMATE_MM 1000U
#define TOF_CLEAR_DISTANCE_MM 4000U
volatile uint32_t g_tof_status_count[16];
volatile uint16_t g_tof_status_last_mm[16];
static void tof_record_status(uint8_t status,uint16_t distance) {
 unsigned k=status<15U?status:15U;
 g_tof_status_count[k]++;g_tof_status_last_mm[k]=distance;
}
static int tof_status_is_clear(uint8_t status,uint16_t distance) {
 return (status==2U||status==4U)&&distance>=TOF_CLEAR_MIN_ESTIMATE_MM;
}
static void publish_tof(unsigned valid,uint16_t distance,uint32_t stamp) {
 uint32_t mask=__get_PRIMASK();__disable_irq();
 latest.seq++;latest.valid=(uint8_t)valid;latest.distance_mm=distance;latest.sample_timestamp_ms=stamp;
 __DMB();__set_PRIMASK(mask);
}
/* Called by priority-3 safety task: bounded memory copy, never waits on IIC. */
int bus_tof_poll(tof_safety_result_t *sample,uint32_t now) {
 (void)now;uint32_t mask=__get_PRIMASK();__disable_irq();
 if(seen==latest.seq){__set_PRIMASK(mask);return 1;}
 *sample=latest;seen=latest.seq;__set_PRIMASK(mask);return 0;
}
static void rpc_step(void) {
 if(dc_rpc_lock())return;
 if(DC_SHARED->rpc_state!=DC_RPC_REQUEST){dc_rpc_unlock();return;}
 uint32_t session=DC_SHARED->rpc_session,address=DC_SHARED->address,reg=DC_SHARED->reg;
 unsigned bytes=DC_SHARED->reg_bytes,write=DC_SHARED->write;
 uint8_t value=(uint8_t)DC_SHARED->value;
 DC_SHARED->rpc_state=DC_RPC_BUSY;dc_rpc_unlock();
 /* Only camera and board switch transactions are accepted; ToF remains local. */
 int rc=-1;
 if(session==DC_SHARED->session&&(address==0x3c||address==0x43)&&reg<=65535&&write<=1)
  rc=bus_transfer(address,(uint16_t)reg,bytes,&value,1,(int)write);
 while(dc_rpc_lock())(void)tk_dly_tsk(1);
 DC_SHARED->result=rc;DC_SHARED->value=value;__DMB();DC_SHARED->rpc_state=DC_RPC_DONE;
 dc_rpc_unlock();
}
static void bus_task(INT n,void *x) {
 (void)n;(void)x;
 int open=R_IIC_MASTER_Open(&g_i2c_master_for_peripheral_ctrl,&g_i2c_master_for_peripheral_cfg)==FSP_SUCCESS;
 int healthy=open&&tof_init()==0;
 if(!healthy)publish_tof(0,0,control_now_ms());
 __DMB();DC_SHARED->ready=DC_READY;
 uint32_t poll=0;
 for(;;) {
  uint32_t now=control_now_ms();
  /* ToF always precedes camera traffic, which is limited to one request per pass. */
  if(healthy&&now-poll>=10) {
   uint8_t ready=0,status=255;uint16_t distance=0;poll=now;
   int rc=VL53L1X_CheckForDataReady(0x52,&ready);
   if(!rc&&ready) {
    rc=VL53L1X_GetRangeStatus(0x52,&status);
    if(!rc)rc=VL53L1X_GetDistance(0x52,&distance);
    if(!rc)rc=VL53L1X_ClearInterrupt(0x52);
    if(!rc)tof_record_status(status,distance);
    if(!rc&&status==0)publish_tof(1,distance,now);
    else if(!rc&&tof_status_is_clear(status,distance))publish_tof(1,TOF_CLEAR_DISTANCE_MM,now);
    else if(!rc&&status==7U)publish_tof(1,distance,now);
    else publish_tof(0,distance,now);
   } else if(rc)publish_tof(0,0,now);
  }
  if(open)rpc_step();
  (void)tk_dly_tsk(1);
 }
}
int bus_owner_start(void) {
 T_CFLG flag={0};flag.flgatr=TA_TFIFO;completion_flag=tk_cre_flg(&flag);if(completion_flag<=0)return -1;
 T_CTSK t={0};t.tskatr=TA_HLNG|TA_RNG0|TA_FPU;t.task=bus_task;t.itskpri=8;t.stksz=4096;
 ID id=tk_cre_tsk(&t);return id>0&&tk_sta_tsk(id,0)==E_OK?0:-1;
}
