#include "motor_output_drv8833.h"
#include <stddef.h>

static void pins(motor_output_drv8833_t *d, unsigned a, unsigned b,
                 unsigned c, unsigned e)
{
    if (d->config.write_pins(d->config.context, a, b, c, e) != 0) {
        d->fault = 1U;
        d->armed = 0U;
        d->command[0] = d->command[1] = 0;
        d->brake = 1U;
        /* Best effort only: a physical GPIO fault cannot be repaired here. */
        (void)d->config.write_pins(d->config.context, 1U, 1U, 1U, 1U);
    }
}

void motor_output_drv8833_disarm(motor_output_drv8833_t *d, motor_stop_action_t a)
{
    if (d == NULL || !d->ready) return;
    d->armed = 0U;
    d->command[0] = d->command[1] = 0;
    d->brake = (a == MOTOR_STOP_BRAKE);
    pins(d, d->brake, d->brake, d->brake, d->brake);
}

int motor_output_drv8833_arm(motor_output_drv8833_t *d)
{
    if (d == NULL || !d->ready || d->fault) return -1;
    d->command[0] = d->command[1] = 0;
    d->age_ticks = 0U;
    d->brake = 0U;
    pins(d, 0U, 0U, 0U, 0U);
    if (d->fault) return -1;
    d->armed = 1U;
    return 0;
}

static int to_slots(float v, float scale)
{
    /* Truncation ensures actual duty never exceeds the configured cap. */
    return (int)(v * scale * (float)MOTOR_PWM_SLOTS);
}

int motor_output_drv8833_apply(motor_output_drv8833_t *d, const control_motor_output_t *o)
{
    float l, r, peak, scale;
    if (d == NULL || !d->ready || d->fault) return -1;
    if (o == NULL ||
        !(o->left_command >= -1.0F && o->left_command <= 1.0F) ||
        !(o->right_command >= -1.0F && o->right_command <= 1.0F)) {
        d->fault = 1U;
        motor_output_drv8833_disarm(d, MOTOR_STOP_BRAKE);
        return -1;
    }
    d->age_ticks = 0U;
    if (!o->motor_enable) {
        d->command[0] = d->command[1] = 0;
        d->brake = (o->stop_action == MOTOR_STOP_BRAKE);
        pins(d, d->brake, d->brake, d->brake, d->brake);
        return d->fault ? -1 : 0;
    }
    if (!d->armed) return -1;
    l = o->left_command;
    r = o->right_command;
    peak = l < 0.0F ? -l : l;
    if ((r < 0.0F ? -r : r) > peak) peak = r < 0.0F ? -r : r;
    /* Scale both wheels together to preserve the steering ratio. */
    scale = peak > d->config.duty_limit ? d->config.duty_limit / peak : 1.0F;
    /* command[0] = AIN channel, command[1] = BIN channel. */
    d->command[d->config.swap_sides ? 1 : 0] = to_slots(l, scale) * (d->config.invert_left ? -1 : 1);
    d->command[d->config.swap_sides ? 0 : 1] = to_slots(r, scale) * (d->config.invert_right ? -1 : 1);
    d->brake = 0U;
    return 0;
}

int motor_output_drv8833_init(motor_output_drv8833_t *d, const motor_drv8833_config_t *c)
{
    if (d == NULL || c == NULL || c->write_pins == NULL ||
        !(c->duty_limit >= 0.05F && c->duty_limit <= 1.00F)) return -1;
    *d = (motor_output_drv8833_t){0};
    d->config = *c;
    d->ready = 1U;
    d->idle_ticks[0] = d->idle_ticks[1] = MOTOR_REVERSE_TICKS;
    pins(d, 0U, 0U, 0U, 0U);
    return d->fault ? -1 : 0;
}

static void wheel(motor_output_drv8833_t *d, unsigned i, unsigned *a, unsigned *b)
{
    int q = d->command[i];
    int direction = (q > 0) - (q < 0);
    unsigned duty = (unsigned)(q < 0 ? -q : q);
    *a = *b = 0U;
    if (direction == 0 || (d->last_direction[i] != 0 &&
        direction != d->last_direction[i] && d->idle_ticks[i] < MOTOR_REVERSE_TICKS)) {
        if (d->idle_ticks[i] < MOTOR_REVERSE_TICKS) ++d->idle_ticks[i];
        return;
    }
    d->last_direction[i] = direction;
    d->idle_ticks[i] = 0U;
    if (d->phase < duty) {
        if (direction > 0) *a = 1U;
        else *b = 1U;
    }
}

void motor_output_drv8833_tick_100us(motor_output_drv8833_t *d)
{
    unsigned a, b, c, e;
    if (d == NULL || !d->ready) return;
    if (d->armed && ++d->age_ticks >= MOTOR_WATCHDOG_TICKS) {
        d->fault = 1U;
        motor_output_drv8833_disarm(d, MOTOR_STOP_BRAKE);
    }
    if (!d->armed || d->brake || d->fault) {
        for (unsigned i = 0U; i < 2U; ++i)
            if (d->idle_ticks[i] < MOTOR_REVERSE_TICKS) ++d->idle_ticks[i];
        pins(d, d->brake, d->brake, d->brake, d->brake);
    } else {
        wheel(d, 0U, &a, &b);
        wheel(d, 1U, &c, &e);
        pins(d, a, b, c, e);
    }
    d->phase = (d->phase + 1U) % MOTOR_PWM_SLOTS;
}
