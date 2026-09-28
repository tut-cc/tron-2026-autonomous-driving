#include "hal_data.h"
#include "control_runtime.h"
#include "motor_output_drv8833.h"
#include "motor_build_profile.h"
#include "motor_pins_ra8p1.h"
/* The supplied old BSP is M85-only. Do not mistake a successful M85
 * compile for a working M33 kernel. Enable validation only for compile tests. */
#if BSP_CFG_CPU_CORE != 1 && !defined(CONTROL_SINGLE_CORE_VALIDATION)
#error "Requires a ported M33 FSP + micro T-Kernel project (CPU1)"
#endif
static motor_output_drv8833_t driver;
static const bsp_io_port_pin_t pins[4]=MOTOR_PINS_RA8P1;
static unsigned levels,ready;
/* Diagnostic (debugger): HIGH writes per pin while driving (not the all-HIGH brake). */
volatile uint32_t g_motor_drive_high_writes[4];
static uint32_t enter(void){uint32_t p=__get_PRIMASK();__disable_irq();__DMB();return p;}
static void leave(uint32_t p){__DMB();__set_PRIMASK(p);}
static int write_pins(void *ctx,unsigned a,unsigned b,unsigned c,unsigned d) {
    unsigned want=a|(b<<1)|(c<<2)|(d<<3),level,i;int failed=0;(void)ctx;
    if (!MOTOR_PHYSICAL_OUTPUT_ENABLE) want=0;
    for(level=0;level<2;++level)for(i=0;i<4;++i) {
        unsigned bit=1U<<i;
        if(((want^levels)&bit)&&((want>>i)&1U)==level) {
            if(R_IOPORT_PinWrite(&g_ioport_ctrl,pins[i],level?BSP_IO_LEVEL_HIGH:BSP_IO_LEVEL_LOW)!=FSP_SUCCESS)failed=1;
            else {levels=(levels&~bit)|(want&bit);if(level&&want!=0xFU)g_motor_drive_high_writes[i]++;}
        }
    }
    return failed?-1:0;
}
void motor_pwm_callback(timer_callback_args_t *a) {
    if(ready && a && a->event==TIMER_EVENT_CYCLE_END)motor_output_drv8833_tick_100us(&driver);
}
void control_hw_stop(void) {
    uint32_t p=enter();motor_output_drv8833_disarm(&driver,MOTOR_STOP_BRAKE);leave(p);
}
int control_hw_arm(void) {
    uint32_t p=enter();int rc=motor_output_drv8833_arm(&driver);leave(p);return rc;
}
int control_hw_apply(const control_motor_output_t *o,uint32_t now) {
    uint32_t p=enter();int rc=motor_output_drv8833_apply(&driver,o);leave(p);
    (void)now;return rc;
}
int control_hw_fault(void){uint32_t p=enter();int f=(int)driver.fault;leave(p);return f;}
/* RESET-only build: SW1(P009)/SW2(P008) are not configured or read. */
int control_hw_init(void) {
    unsigned i;
    const motor_drv8833_config_t c = {
        .context = 0,
        .write_pins = write_pins,
        .duty_limit = MOTOR_DUTY_LIMIT,
        .invert_left = MOTOR_LEFT_INVERTED,
        .invert_right = MOTOR_RIGHT_INVERTED,
        .swap_sides = MOTOR_SWAP_SIDES,
    };
    if(ready)return -1;
    for(i=0;i<4;++i)if(R_IOPORT_PinCfg(&g_ioport_ctrl,pins[i],IOPORT_CFG_PORT_DIRECTION_OUTPUT|IOPORT_CFG_PORT_OUTPUT_LOW)!=FSP_SUCCESS)return -1;
    if(motor_output_drv8833_init(&driver,&c))return -1;
    if(R_GPT_Open(&g_motor_tick_ctrl,&g_motor_tick_cfg)!=FSP_SUCCESS)return -1;
    ready=1;
    if(R_GPT_Start(&g_motor_tick_ctrl)!=FSP_SUCCESS){ready=0;control_hw_stop();(void)R_GPT_Close(&g_motor_tick_ctrl);return -1;}
    return 0;
}
