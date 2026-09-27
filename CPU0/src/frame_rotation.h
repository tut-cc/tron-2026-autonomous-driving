#ifndef FRAME_ROTATION_H
#define FRAME_ROTATION_H
#include <stdint.h>
#include <stddef.h>
/* In-place RGB565 rotation. Stride is in pixels; padding bytes stay untouched. */
int frame_rotate_180_rgb565(uint8_t *frame, uint32_t width, uint32_t height,
                            uint32_t stride, size_t capacity);
#endif
