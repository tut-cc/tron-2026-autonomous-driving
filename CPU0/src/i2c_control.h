/***********************************************************************************************************************
 * File Name    : i2c_control.h
 * Description  : I2C helper API used by the camera and board switch control.
 **********************************************************************************************************************/
/***********************************************************************************************************************
* Copyright (c) 2026
*
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************************************************************/

#ifndef I2C_CONTROL_H_
#define I2C_CONTROL_H_

#include "common_utils.h"

fsp_err_t i2c_control_init(void);
fsp_err_t read_reg_8bit(uint8_t reg_addr, uint8_t * p_data);
fsp_err_t read_reg_16bit(uint16_t reg_addr, uint8_t * p_data);
fsp_err_t write_reg_8bit(uint8_t reg_addr, uint8_t data);
fsp_err_t write_reg_16bit(uint16_t reg_addr, uint8_t data);

#endif /* I2C_CONTROL_H_ */
