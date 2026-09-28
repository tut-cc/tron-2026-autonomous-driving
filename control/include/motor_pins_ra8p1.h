#ifndef MOTOR_PINS_RA8P1_H
#define MOTOR_PINS_RA8P1_H
/* DRV8833 inputs on PMOD1 (EK-RA8P1 J26): AIN1=P006 (pin 7), AIN2=P402
 * (pin 8, the Pmod RESET line), BIN1=P412 (pin 9), BIN2=P413 (pin 10).
 * Owned by CPU1 (CPU1/ra_gen/pin_data.c, control_hw_ra8p1.c).  CPU0 only
 * drives them LOW once at boot, before it starts CPU1, so the motors cannot
 * twitch while CPU1 is still held in reset (2026-09-28). */
#define MOTOR_PINS_RA8P1 \
    { BSP_IO_PORT_00_PIN_06, BSP_IO_PORT_04_PIN_02, BSP_IO_PORT_04_PIN_12, BSP_IO_PORT_04_PIN_13 }
#endif
