/***********************************************************************************************************************
 * File Name    : usb_pcdc_console.h
 * Description  : USB PCDC terminal and binary stream helper.
 **********************************************************************************************************************/
/***********************************************************************************************************************
* Copyright (c) 2026
*
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************************************************************/

#ifndef USB_PCDC_CONSOLE_H_
#define USB_PCDC_CONSOLE_H_

#include <stdint.h>
#include "bsp_api.h"

fsp_err_t usb_pcdc_console_init(void);
void usb_pcdc_console_deinit(void);
fsp_err_t usb_pcdc_console_write(uint8_t const * p_data, uint32_t length);
/* Video diagnostics use this variant so an absent USB terminal cannot stall
 * unrelated Ethernet/HTTP video service. */
fsp_err_t usb_pcdc_console_write_if_ready(uint8_t const * p_data, uint32_t length);
uint32_t usb_pcdc_console_printf(char const * p_format, ...);
uint32_t usb_pcdc_console_read(void * const p_buffer, uint32_t buffer_size);
uint32_t usb_pcdc_console_has_data(void);
uint32_t usb_pcdc_console_has_key(void);
void usb_pcdc_console_poll(void);

#endif /* USB_PCDC_CONSOLE_H_ */
