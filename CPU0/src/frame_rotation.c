#include "frame_rotation.h"
int frame_rotate_180_rgb565(uint8_t *frame, uint32_t width, uint32_t height,
                            uint32_t stride, size_t capacity)
{
    if (!frame || !width || !height || stride < width ||
        (uint64_t)stride * height * 2U > capacity) return -1;
    uint64_t pixels = (uint64_t)width * height;
    for (uint64_t i = 0U; i < pixels / 2U; ++i) {
        uint64_t j = pixels - 1U - i;
        size_t a = (size_t)((i / width * stride + i % width) * 2U);
        size_t b = (size_t)((j / width * stride + j % width) * 2U);
        uint8_t lo = frame[a], hi = frame[a + 1U];
        frame[a] = frame[b]; frame[a + 1U] = frame[b + 1U];
        frame[b] = lo; frame[b + 1U] = hi;
    }
    return 0;
}
