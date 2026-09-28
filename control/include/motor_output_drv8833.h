#ifndef MOTOR_OUTPUT_DRV8833_H
#define MOTOR_OUTPUT_DRV8833_H
#include "control_motor.h"

/* Bench PWM: 100 us interrupt, 20 slots -> 500 Hz, 5% steps per period.
 * Commands keep 1/256-slot precision: each period's on-time is chosen so the
 * running average equals the command (error carried to the next period), so
 * steering differences smaller than 5% are not lost (2026-09-28).
 * All non-ISR calls must exclude the PWM ISR (same MCU core).
 * write_pins: AIN1,AIN2,BIN1,BIN2; return 0 on success. Never block/log.
 */
#define MOTOR_PWM_TICK_US 100U
#define MOTOR_PWM_SLOTS 20U
#define MOTOR_WATCHDOG_TICKS 1000U /* 100 ms without apply -> latched brake */
#define MOTOR_REVERSE_TICKS 200U  /* at least 20 ms coast before reversal */
#define MOTOR_DUTY_FRACTION_BITS 8U /* command[] = slots << 8 (signed)     */
typedef int (*motor_write_pins_fn)(void *, unsigned, unsigned, unsigned, unsigned);
typedef struct {
    void *context;
    motor_write_pins_fn write_pins;
    float duty_limit; /* 0.05..1.00; mode-specific cap, not measured speed */
    int invert_left, invert_right; /* vehicle's left / right wheel */
    int swap_sides;   /* 0: AIN = left wheel, 1: AIN = right wheel */
} motor_drv8833_config_t;
typedef struct {
    motor_drv8833_config_t config;
    int command[2], last_direction[2];
    unsigned idle_ticks[2], age_ticks, phase;
    unsigned duty[2], carry[2]; /* this period's on-slots; carried fraction */
    unsigned armed, fault, brake, ready;
} motor_output_drv8833_t;

int motor_output_drv8833_init(motor_output_drv8833_t *, const motor_drv8833_config_t *);
/* Set the wheel commands (motor_enable=0: brake/coast per stop_action).
 * 0 on success; -1 when not ready, faulted, not armed or out of range
 * (an out-of-range command also latches the fault). */
int motor_output_drv8833_apply(motor_output_drv8833_t *, const control_motor_output_t *);
/* Explicit arm after startup/stop. Faults require reinitialization. */
int motor_output_drv8833_arm(motor_output_drv8833_t *);
void motor_output_drv8833_disarm(motor_output_drv8833_t *, motor_stop_action_t);
void motor_output_drv8833_tick_100us(motor_output_drv8833_t *);
#endif
