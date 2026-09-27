#include "bsp_api.h"
#include "r_ioport.h"
const ioport_pin_cfg_t g_bsp_pin_cfg_data[]={
{ .pin = BSP_IO_PORT_05_PIN_12, .pin_cfg = ((uint32_t) IOPORT_CFG_DRIVE_MID
                  | (uint32_t) IOPORT_CFG_NMOS_ENABLE | (uint32_t) IOPORT_CFG_PERIPHERAL_PIN
                  | (uint32_t) IOPORT_PERIPHERAL_IIC) },
{ .pin = BSP_IO_PORT_05_PIN_11, .pin_cfg = ((uint32_t) IOPORT_CFG_DRIVE_MID
                  | (uint32_t) IOPORT_CFG_NMOS_ENABLE | (uint32_t) IOPORT_CFG_PERIPHERAL_PIN
                  | (uint32_t) IOPORT_PERIPHERAL_IIC) },
{ .pin = BSP_IO_PORT_00_PIN_06, .pin_cfg = ((uint32_t) IOPORT_CFG_PORT_DIRECTION_OUTPUT
                  | (uint32_t) IOPORT_CFG_PORT_OUTPUT_LOW) },
{ .pin = BSP_IO_PORT_00_PIN_08, .pin_cfg = ((uint32_t) IOPORT_CFG_IRQ_ENABLE
                  | (uint32_t) IOPORT_CFG_PORT_DIRECTION_INPUT) },
{ .pin = BSP_IO_PORT_00_PIN_09, .pin_cfg = ((uint32_t) IOPORT_CFG_IRQ_ENABLE
                  | (uint32_t) IOPORT_CFG_PORT_DIRECTION_INPUT) },
{ .pin = BSP_IO_PORT_04_PIN_02, .pin_cfg = ((uint32_t) IOPORT_CFG_DRIVE_MID
                  | (uint32_t) IOPORT_CFG_PORT_DIRECTION_OUTPUT | (uint32_t) IOPORT_CFG_PORT_OUTPUT_LOW) },
{ .pin = BSP_IO_PORT_04_PIN_12, .pin_cfg = ((uint32_t) IOPORT_CFG_DRIVE_MID
                  | (uint32_t) IOPORT_CFG_PORT_DIRECTION_OUTPUT | (uint32_t) IOPORT_CFG_PORT_OUTPUT_LOW) },
{ .pin = BSP_IO_PORT_04_PIN_13, .pin_cfg = ((uint32_t) IOPORT_CFG_DRIVE_MID
                  | (uint32_t) IOPORT_CFG_PORT_DIRECTION_OUTPUT | (uint32_t) IOPORT_CFG_PORT_OUTPUT_LOW) }
};
const ioport_cfg_t g_bsp_pin_cfg={.number_of_pins=sizeof(g_bsp_pin_cfg_data)/sizeof(g_bsp_pin_cfg_data[0]),.p_pin_cfg_data=g_bsp_pin_cfg_data};
void BSP_PinConfigSecurityInit(void){}
