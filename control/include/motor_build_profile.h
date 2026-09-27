#ifndef MOTOR_BUILD_PROFILE_H
#define MOTOR_BUILD_PROFILE_H

/* The default is the safe, motor-power-off build.  A physical-output image
 * must opt in explicitly from the CPU1 build command line.  Keeping this
 * policy in one header prevents a CPU0 user_config.h value from silently
 * changing the M33 image. */
#ifndef MOTOR_PHYSICAL_OUTPUT_ENABLE
#define MOTOR_PHYSICAL_OUTPUT_ENABLE 0
#endif

#if (MOTOR_PHYSICAL_OUTPUT_ENABLE != 0) && (MOTOR_PHYSICAL_OUTPUT_ENABLE != 1)
#error "MOTOR_PHYSICAL_OUTPUT_ENABLE must be 0 or 1"
#endif

/* PMOD1 polarity is a wiring hypothesis until the wheel-raised check in the
 * runbook is completed. Change these named constants after that check; do not
 * hide polarity in a positional initializer. */
#ifndef MOTOR_LEFT_INVERTED
#define MOTOR_LEFT_INVERTED 0
#endif
#ifndef MOTOR_RIGHT_INVERTED
#define MOTOR_RIGHT_INVERTED 0
#endif
#if (MOTOR_LEFT_INVERTED != 0) && (MOTOR_LEFT_INVERTED != 1)
#error "MOTOR_LEFT_INVERTED must be 0 or 1"
#endif
#if (MOTOR_RIGHT_INVERTED != 0) && (MOTOR_RIGHT_INVERTED != 1)
#error "MOTOR_RIGHT_INVERTED must be 0 or 1"
#endif

/* Which DRV8833 channel drives which wheel.  0: AIN (PMOD1-7/8) = left,
 * BIN (PMOD1-9/10) = right.  1: swapped (AIN = right wheel), as wired on the
 * contest car (2026-09-27: turning drove the wrong wheel).  The invert flags
 * above always refer to the vehicle's left / right wheel. */
#ifndef MOTOR_SWAP_SIDES
#define MOTOR_SWAP_SIDES 1
#endif
#if (MOTOR_SWAP_SIDES != 0) && (MOTOR_SWAP_SIDES != 1)
#error "MOTOR_SWAP_SIDES must be 0 or 1"
#endif

/* Conservative initial tuning point: this is a duty cap, not a measured
 * speed or torque guarantee. Raise only after current/temperature testing. */
#ifndef MOTOR_DUTY_CAP_PERCENT
#define MOTOR_DUTY_CAP_PERCENT 60U
#endif
#if (MOTOR_DUTY_CAP_PERCENT < 5U) || (MOTOR_DUTY_CAP_PERCENT > 100U)
#error "MOTOR_DUTY_CAP_PERCENT must be between 5 and 100"
#endif
#define MOTOR_DUTY_LIMIT ((float)MOTOR_DUTY_CAP_PERCENT / 100.0F)

#define MOTOR_BUILD_PROFILE_NAME \
    (MOTOR_PHYSICAL_OUTPUT_ENABLE ? "vehicle-output" : "dry-run")

#endif
