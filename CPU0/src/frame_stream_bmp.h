/* Indexed BMP layout shared with the host contract test. */
#ifndef FRAME_STREAM_BMP_H_
#define FRAME_STREAM_BMP_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define FRAME_STREAM_BMP_WIDTH             (240U)
#define FRAME_STREAM_BMP_HEIGHT            (180U)
#define FRAME_STREAM_BMP_BITS_PER_PIXEL    (8U)
#define FRAME_STREAM_BMP_PALETTE_ENTRIES   (256U)
#define FRAME_STREAM_BMP_FILE_HEADER_SIZE  (14U)
#define FRAME_STREAM_BMP_INFO_HEADER_SIZE  (40U)
#define FRAME_STREAM_BMP_PALETTE_SIZE      (FRAME_STREAM_BMP_PALETTE_ENTRIES * 4U)
#define FRAME_STREAM_BMP_PIXEL_OFFSET      (FRAME_STREAM_BMP_FILE_HEADER_SIZE + \
                                             FRAME_STREAM_BMP_INFO_HEADER_SIZE + \
                                             FRAME_STREAM_BMP_PALETTE_SIZE)
#define FRAME_STREAM_BMP_ROW_BYTES         (FRAME_STREAM_BMP_WIDTH)
#define FRAME_STREAM_BMP_IMAGE_SIZE        (FRAME_STREAM_BMP_ROW_BYTES * FRAME_STREAM_BMP_HEIGHT)
#define FRAME_STREAM_BMP_SIZE               (FRAME_STREAM_BMP_PIXEL_OFFSET + \
                                             FRAME_STREAM_BMP_IMAGE_SIZE)

static inline uint8_t frame_stream_bmp_rgb332_index(uint16_t rgb565)
{
    return (uint8_t) (((rgb565 >> 13U) & 0x07U) << 5U |
                      ((rgb565 >> 8U) & 0x07U) << 2U |
                      ((rgb565 >> 3U) & 0x03U));
}

static inline uint8_t frame_stream_bmp_rgb332_expand(uint8_t value, uint8_t max_value)
{
    return (uint8_t) (((uint16_t) value * 255U + (max_value / 2U)) / max_value);
}

static inline uint8_t frame_stream_bmp_palette_red(uint8_t index)
{
    return frame_stream_bmp_rgb332_expand((uint8_t) (index >> 5U), 7U);
}

static inline uint8_t frame_stream_bmp_palette_green(uint8_t index)
{
    return frame_stream_bmp_rgb332_expand((uint8_t) ((index >> 2U) & 0x07U), 7U);
}

static inline uint8_t frame_stream_bmp_palette_blue(uint8_t index)
{
    return frame_stream_bmp_rgb332_expand((uint8_t) (index & 0x03U), 3U);
}

static inline void frame_stream_bmp_put_u16(uint8_t * destination, uint16_t value)
{
    destination[0] = (uint8_t) value;
    destination[1] = (uint8_t) (value >> 8U);
}

static inline void frame_stream_bmp_put_u32(uint8_t * destination, uint32_t value)
{
    destination[0] = (uint8_t) value;
    destination[1] = (uint8_t) (value >> 8U);
    destination[2] = (uint8_t) (value >> 16U);
    destination[3] = (uint8_t) (value >> 24U);
}

/* Encode a bottom-up, uncompressed indexed BMP using the same bounded
 * nearest-neighbor RGB565 conversion used by the embedded video endpoint. */
static inline bool frame_stream_bmp_encode(uint8_t * destination,
                                           uint32_t destination_capacity,
                                           uint8_t const * source,
                                           uint16_t source_width,
                                           uint16_t source_height)
{
    if ((destination == NULL) || (destination_capacity < FRAME_STREAM_BMP_SIZE) ||
        (source == NULL) || (source_width == 0U) || (source_height == 0U))
    {
        return false;
    }

    memset(destination, 0, FRAME_STREAM_BMP_PIXEL_OFFSET);
    destination[0] = 'B';
    destination[1] = 'M';
    frame_stream_bmp_put_u32(&destination[2], FRAME_STREAM_BMP_SIZE);
    frame_stream_bmp_put_u32(&destination[10], FRAME_STREAM_BMP_PIXEL_OFFSET);
    frame_stream_bmp_put_u32(&destination[14], FRAME_STREAM_BMP_INFO_HEADER_SIZE);
    frame_stream_bmp_put_u32(&destination[18], FRAME_STREAM_BMP_WIDTH);
    frame_stream_bmp_put_u32(&destination[22], FRAME_STREAM_BMP_HEIGHT);
    frame_stream_bmp_put_u16(&destination[26], 1U);
    frame_stream_bmp_put_u16(&destination[28], FRAME_STREAM_BMP_BITS_PER_PIXEL);
    frame_stream_bmp_put_u32(&destination[34], FRAME_STREAM_BMP_IMAGE_SIZE);
    frame_stream_bmp_put_u32(&destination[38], 2835U);
    frame_stream_bmp_put_u32(&destination[42], 2835U);
    frame_stream_bmp_put_u32(&destination[46], FRAME_STREAM_BMP_PALETTE_ENTRIES);

    for (uint32_t palette_index = 0U;
         palette_index < FRAME_STREAM_BMP_PALETTE_ENTRIES;
         ++palette_index)
    {
        uint8_t * palette_entry = &destination[54U + palette_index * 4U];
        uint8_t index = (uint8_t) palette_index;
        palette_entry[0] = frame_stream_bmp_palette_blue(index);
        palette_entry[1] = frame_stream_bmp_palette_green(index);
        palette_entry[2] = frame_stream_bmp_palette_red(index);
    }

    uint32_t source_row_bytes = (uint32_t) source_width * 2U;
    for (uint32_t out_y = 0U; out_y < FRAME_STREAM_BMP_HEIGHT; ++out_y)
    {
        uint32_t source_y = ((FRAME_STREAM_BMP_HEIGHT - 1U - out_y) * source_height) /
                            FRAME_STREAM_BMP_HEIGHT;
        uint8_t const * source_row = &source[source_y * source_row_bytes];
        uint8_t * destination_row = &destination[FRAME_STREAM_BMP_PIXEL_OFFSET +
                                                 out_y * FRAME_STREAM_BMP_ROW_BYTES];
        for (uint32_t out_x = 0U; out_x < FRAME_STREAM_BMP_WIDTH; ++out_x)
        {
            uint32_t source_x = (out_x * source_width) / FRAME_STREAM_BMP_WIDTH;
            uint32_t source_index = source_x * 2U;
            uint16_t pixel = (uint16_t) source_row[source_index] |
                             ((uint16_t) source_row[source_index + 1U] << 8U);
            destination_row[out_x] = frame_stream_bmp_rgb332_index(pixel);
        }
    }
    return true;
}

#endif /* FRAME_STREAM_BMP_H_ */
