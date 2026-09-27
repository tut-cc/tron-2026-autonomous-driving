#include <stdio.h>
#include <assert.h>
#include <stdint.h>

#include "frame_stream_bmp.h"

static uint32_t read_u32(uint8_t const * data)
{
    return (uint32_t) data[0] | ((uint32_t) data[1] << 8U) |
           ((uint32_t) data[2] << 16U) | ((uint32_t) data[3] << 24U);
}

int main(void)
{
    assert(FRAME_STREAM_BMP_WIDTH == 240U);
    assert(FRAME_STREAM_BMP_HEIGHT == 180U);
    assert(FRAME_STREAM_BMP_PIXEL_OFFSET == 1078U);
    assert(FRAME_STREAM_BMP_IMAGE_SIZE == 43200U);
    assert(FRAME_STREAM_BMP_SIZE == 44278U);
    assert(FRAME_STREAM_BMP_SIZE < (54U + 160U * 120U * 3U));

    /* The three UI overlay colors remain exact in the indexed palette. */
    uint8_t magenta = frame_stream_bmp_rgb332_index(0xF81FU);
    uint8_t cyan = frame_stream_bmp_rgb332_index(0x07FFU);
    uint8_t red = frame_stream_bmp_rgb332_index(0xF800U);
    assert(frame_stream_bmp_palette_red(magenta) == 255U);
    assert(frame_stream_bmp_palette_green(magenta) == 0U);
    assert(frame_stream_bmp_palette_blue(magenta) == 255U);
    assert(frame_stream_bmp_palette_red(cyan) == 0U);
    assert(frame_stream_bmp_palette_green(cyan) == 255U);
    assert(frame_stream_bmp_palette_blue(cyan) == 255U);
    assert(frame_stream_bmp_palette_red(red) == 255U);
    assert(frame_stream_bmp_palette_green(red) == 0U);
    assert(frame_stream_bmp_palette_blue(red) == 0U);

    /* All 256 indices round-trip to unique RGB332 colors. */
    for (uint32_t index = 0U; index < FRAME_STREAM_BMP_PALETTE_ENTRIES; ++index)
    {
        uint8_t value = (uint8_t) index;
        assert(frame_stream_bmp_rgb332_index(
                   (uint16_t) (((uint16_t) (value >> 5U) << 13U) |
                               ((uint16_t) ((value >> 2U) & 0x07U) << 8U) |
                               ((uint16_t) (value & 0x03U) << 3U))) == value);
    }

    static uint8_t source[320U * 240U * 2U];
    static uint8_t bitmap[FRAME_STREAM_BMP_SIZE];
    for (uint32_t i = 0U; i < 320U * 240U; ++i)
    {
        source[i * 2U] = 0x1FU;
        source[i * 2U + 1U] = 0xF8U;
    }
    assert(frame_stream_bmp_encode(bitmap, sizeof(bitmap), source, 320U, 240U));
    assert(bitmap[0] == 'B' && bitmap[1] == 'M');
    assert(read_u32(&bitmap[2]) == FRAME_STREAM_BMP_SIZE);
    assert(read_u32(&bitmap[10]) == FRAME_STREAM_BMP_PIXEL_OFFSET);
    assert(read_u32(&bitmap[18]) == FRAME_STREAM_BMP_WIDTH);
    assert(read_u32(&bitmap[22]) == FRAME_STREAM_BMP_HEIGHT);
    assert(bitmap[28] == FRAME_STREAM_BMP_BITS_PER_PIXEL);
    assert(read_u32(&bitmap[34]) == FRAME_STREAM_BMP_IMAGE_SIZE);
    assert(bitmap[FRAME_STREAM_BMP_PIXEL_OFFSET] == magenta);
    assert(bitmap[FRAME_STREAM_BMP_PIXEL_OFFSET + FRAME_STREAM_BMP_IMAGE_SIZE - 1U] == magenta);
    puts("frame_stream BMP contract tests: PASS");
    return 0;
}
