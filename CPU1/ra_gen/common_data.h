#ifndef COMMON_DATA_H
#define COMMON_DATA_H
#include "bsp_api.h"
#include "r_ioport.h"
extern ioport_instance_ctrl_t g_ioport_ctrl;
extern const ioport_cfg_t g_bsp_pin_cfg;
void g_common_init(void);
#endif
