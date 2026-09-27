#ifndef CONTROL_RUNTIME_H
#define CONTROL_RUNTIME_H
#include "control_ipc.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    ipc_endpoint_t *ipc;
    uint32_t boot_session;
    /* M33-local ToF driver: 0=new sample, 1=not ready, negative=error.
     * Nonblocking. Fill seq, valid, distance_mm and actual sample time.
     * Must not depend on M85 AI task or its I2C ownership. */
    int (*tof_poll)(tof_safety_result_t *,uint32_t now);
} control_runtime_config_t;
/* First internal fault that forced the fail-safe stop (published in
 * g_control_fault for the debugger; 0 = none).  Any of these leaves the
 * vehicle in EMERGENCY/VC_INTERNAL until a board reset. */
typedef enum {
    CONTROL_FAULT_NONE = 0,
    CONTROL_FAULT_INIT,         /* runtime start failed (HW, IPC, kernel object) */
    CONTROL_FAULT_CLOCK,        /* tk_get_otm() failed                           */
    CONTROL_FAULT_LOCK,         /* state mutex not acquired within 10ms          */
    CONTROL_FAULT_UNLOCK,       /* tk_unl_mtx() failed                           */
    CONTROL_FAULT_STOP_FLAG,    /* stop event flag set/wait failed               */
    CONTROL_FAULT_QUEUE,        /* message buffer returned a wrong size          */
    CONTROL_FAULT_DELAY,        /* tk_dly_tsk() failed                           */
    CONTROL_FAULT_HW,           /* control_hw_fault() / arm / apply failed       */
    CONTROL_FAULT_MOTION_STALE  /* DecisionTask output older than 50ms           */
} control_fault_t;
extern volatile uint32_t g_control_fault;
/* Task context, after FSP g_ioport opened. Do not start legacy motor tasks. */
int control_runtime_start(const control_runtime_config_t *);
int control_submit_ai(const ai_perception_result_t *);
int control_submit_web(const vc_web_t *);
int control_get_status(vc_status_t *);
void control_request_stop(int emergency);
uint32_t control_now_ms(void);
/* Hardware port; these are CONTROL CORE only, not AI/Web APIs. */
int control_hw_init(void);
int control_hw_arm(void);
void control_hw_stop(void);
int control_hw_apply(const control_motor_output_t *,uint32_t now);
int control_hw_fault(void);
#ifdef __cplusplus
}
#endif
#endif
