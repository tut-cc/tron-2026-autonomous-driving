#include <stdint.h>
#include <stdio.h>

#include "frame_stream_bmp.h"

#define FIXTURE_SOURCE_WIDTH  (320U)
#define FIXTURE_SOURCE_HEIGHT (240U)

static uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    return (uint16_t) (((uint16_t) (red >> 3U) << 11U) |
                       ((uint16_t) (green >> 2U) << 5U) |
                       (uint16_t) (blue >> 3U));
}

int main(int argc, char ** argv)
{
    static uint8_t source[FIXTURE_SOURCE_WIDTH * FIXTURE_SOURCE_HEIGHT * 2U];
    static uint8_t bitmap[FRAME_STREAM_BMP_SIZE];
    if (argc != 2)
    {
        (void) fprintf(stderr, "usage: frame_stream_bmp_fixture <output.bmp>\n");
        return 2;
    }

    for (uint32_t y = 0U; y < FIXTURE_SOURCE_HEIGHT; ++y)
    {
        for (uint32_t x = 0U; x < FIXTURE_SOURCE_WIDTH; ++x)
        {
            uint8_t red = (uint8_t) ((x * 255U) / (FIXTURE_SOURCE_WIDTH - 1U));
            uint8_t green = (uint8_t) ((y * 255U) / (FIXTURE_SOURCE_HEIGHT - 1U));
            uint8_t blue = (uint8_t) (((x / 40U + y / 30U) & 1U) ? 255U : 0U);
            if ((x < 80U) && (y < 60U))
            {
                red = 255U;
                green = 0U;
                blue = 255U; /* magenta overlay */
            }
            else if ((x >= 80U) && (x < 160U) && (y < 60U))
            {
                red = 0U;
                green = 255U;
                blue = 255U; /* cyan overlay */
            }
            else if ((x >= 160U) && (x < 240U) && (y < 60U))
            {
                red = 255U;
                green = 0U;
                blue = 0U; /* blocked-path overlay */
            }
            uint16_t pixel = rgb565(red, green, blue);
            uint32_t offset = (y * FIXTURE_SOURCE_WIDTH + x) * 2U;
            source[offset] = (uint8_t) pixel;
            source[offset + 1U] = (uint8_t) (pixel >> 8U);
        }
    }

    if (!frame_stream_bmp_encode(bitmap, sizeof(bitmap), source,
                                 FIXTURE_SOURCE_WIDTH, FIXTURE_SOURCE_HEIGHT))
    {
        return 1;
    }
    FILE * output = fopen(argv[1], "wb");
    if (output == NULL)
    {
        (void) fprintf(stderr, "cannot open output BMP\n");
        return 1;
    }
    size_t written = fwrite(bitmap, 1U, sizeof(bitmap), output);
    int close_result = fclose(output);
    return ((written == sizeof(bitmap)) && (close_result == 0)) ? 0 : 1;
}
