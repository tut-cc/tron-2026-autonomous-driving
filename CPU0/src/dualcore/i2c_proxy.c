#include "i2c_control.h"
#include "dualcore_board.h"
/* The prebuilt sensor takes this object's address; no M85 IIC driver is opened. */
iic_master_instance_ctrl_t g_i2c_master_for_peripheral_ctrl;
static uint32_t address=0x3c, sequence;
fsp_err_t __wrap_R_IIC_MASTER_SlaveAddressSet(i2c_master_ctrl_t *ctrl,uint32_t a,i2c_master_addr_mode_t mode) {
 (void)ctrl;if(mode!=I2C_MASTER_ADDR_MODE_7BIT || a>127)return FSP_ERR_INVALID_ARGUMENT;
 address=a;return FSP_SUCCESS;
}
fsp_err_t i2c_control_init(void) {
 for(unsigned i=0;i<10000;i++) {
  if(DC_SHARED->ready==DC_READY)return FSP_SUCCESS;
  R_BSP_SoftwareDelay(1,BSP_DELAY_UNITS_MILLISECONDS);
 }
 return FSP_ERR_TIMEOUT;
}
static fsp_err_t transfer(uint16_t reg,unsigned bytes,unsigned wr,uint8_t *value) {
 if(!value||DC_SHARED->ready!=DC_READY||dc_rpc_lock())return FSP_ERR_NOT_OPEN;
 if(DC_SHARED->rpc_state==DC_RPC_DONE)DC_SHARED->rpc_state=DC_RPC_IDLE;
 if(DC_SHARED->rpc_state!=DC_RPC_IDLE){dc_rpc_unlock();return FSP_ERR_IN_USE;}
 uint32_t seq=++sequence;
 DC_SHARED->rpc_seq=seq;DC_SHARED->rpc_session=DC_SHARED->session;
 DC_SHARED->address=address;DC_SHARED->reg=reg;DC_SHARED->reg_bytes=bytes;
 DC_SHARED->write=wr;DC_SHARED->value=wr?*value:0;
 __DMB();DC_SHARED->rpc_state=DC_RPC_REQUEST;dc_rpc_unlock();
 for(unsigned i=0;i<500;i++) {
  if(!dc_rpc_lock()) {
   if(DC_SHARED->rpc_state==DC_RPC_DONE&&DC_SHARED->rpc_seq==seq) {
    int rc=DC_SHARED->result;*value=(uint8_t)DC_SHARED->value;
    DC_SHARED->rpc_state=DC_RPC_IDLE;dc_rpc_unlock();
    return rc?FSP_ERR_ABORTED:FSP_SUCCESS;
   }
   dc_rpc_unlock();
  }
  R_BSP_SoftwareDelay(1,BSP_DELAY_UNITS_MILLISECONDS);
 }
 return FSP_ERR_TIMEOUT;
}
fsp_err_t read_reg_8bit(uint8_t r,uint8_t *v){return transfer(r,1,0,v);}
fsp_err_t read_reg_16bit(uint16_t r,uint8_t *v){return transfer(r,2,0,v);}
fsp_err_t write_reg_8bit(uint8_t r,uint8_t v){return transfer(r,1,1,&v);}
fsp_err_t write_reg_16bit(uint16_t r,uint8_t v){return transfer(r,2,1,&v);}
