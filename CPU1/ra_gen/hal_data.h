#ifndef HAL_DATA_H
#define HAL_DATA_H
#include "common_data.h"
#include "r_gpt.h"
#include "r_iic_master.h"
extern gpt_instance_ctrl_t g_motor_tick_ctrl;
extern const timer_cfg_t g_motor_tick_cfg;
extern iic_master_instance_ctrl_t g_i2c_master_for_peripheral_ctrl;
extern const i2c_master_cfg_t g_i2c_master_for_peripheral_cfg;
void motor_pwm_callback(timer_callback_args_t *);
void g_i2c_master_for_peripheral_callback(i2c_master_callback_args_t *);
void g_hal_init(void);
void hal_entry(void);
#endif
