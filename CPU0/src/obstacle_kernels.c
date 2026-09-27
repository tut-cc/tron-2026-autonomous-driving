/***********************************************************************************************************************
 * File Name    : obstacle_kernels.c
 * Description  : Optimised pre/post-processing loops for YOLO-Fastest (2026-09-27).
 *
 * Results are intended to be bit-identical to the previous code in obstacle_detector.cc
 * (control/tests/test_obstacle_kernels.c compares both on random data):
 *  - od_prepare_input: only the letterbox padding is filled with 0x80 instead of the whole
 *    307 KB tensor; source x/y are computed once per column/row instead of two divisions per
 *    pixel; RGB565 -> int8 uses 32/64-entry tables holding the same integer expression.
 *  - od_decode_output: sigmoid(dequantize(q)) has only 256 possible values per tensor, so it
 *    is tabulated once per call (256 exp) instead of ~120,000 exp per frame.  Anchors whose
 *    objectness is already below the score threshold are skipped before the 80-class scan;
 *    that is exact because the class score is <= 1.0F, so objectness * class <= objectness.
 **********************************************************************************************************************/
#include <math.h>
#include <string.h>

#include "ai/obstacle_kernels.h"
#include "user_config.h"

static float od_sigmoid(float value)
{
    return 1.0F / (1.0F + expf(-value));
}

static float od_clamp(float value, float minimum, float maximum)
{
    if (value < minimum)
    {
        return minimum;
    }
    if (value > maximum)
    {
        return maximum;
    }
    return value;
}

static float od_dequantize(int8_t value, float scale, int32_t zero_point)
{
    return (float) ((int32_t) value - zero_point) * scale;
}

od_letterbox_t od_letterbox(uint16_t width, uint16_t height)
{
    od_letterbox_t box = {0U, 0U, 0U, 0U};
    if (width >= height)
    {
        box.scaled_width = OD_INPUT_WIDTH;
        box.scaled_height = ((uint32_t) height * OD_INPUT_WIDTH) / (uint32_t) width;
    }
    else
    {
        box.scaled_height = OD_INPUT_HEIGHT;
        box.scaled_width = ((uint32_t) width * OD_INPUT_HEIGHT) / (uint32_t) height;
    }
    if (0U == box.scaled_width)
    {
        box.scaled_width = 1U;
    }
    if (0U == box.scaled_height)
    {
        box.scaled_height = 1U;
    }
    box.pad_x = (OD_INPUT_WIDTH - box.scaled_width) / 2U;
    box.pad_y = (OD_INPUT_HEIGHT - box.scaled_height) / 2U;
    return box;
}

/* (channel * 255 / max) - 128, exactly as the previous per-pixel code. */
static int8_t g_od_red_blue[32];
static int8_t g_od_green[64];
static bool g_od_tables_ready = false;

static void od_build_color_tables(void)
{
    for (uint32_t v = 0U; v < 32U; v++)
    {
        g_od_red_blue[v] = (int8_t) ((int32_t) (uint8_t) (v * 255U / 31U) - 128);
    }
    for (uint32_t v = 0U; v < 64U; v++)
    {
        g_od_green[v] = (int8_t) ((int32_t) (uint8_t) (v * 255U / 63U) - 128);
    }
    g_od_tables_ready = true;
}

void od_prepare_input(uint16_t const * p_pixels,
                      uint16_t width,
                      uint16_t height,
                      uint16_t stride_pixels,
                      int8_t * p_out,
                      uint32_t out_bytes)
{
    const uint32_t row_bytes = OD_INPUT_WIDTH * OD_INPUT_CHANNELS;
    od_letterbox_t box = od_letterbox(width, height);
    uint32_t first_row = box.pad_y;
    uint32_t end_row = box.pad_y + box.scaled_height;
    uint32_t left_bytes = box.pad_x * OD_INPUT_CHANNELS;
    uint32_t active_bytes = box.scaled_width * OD_INPUT_CHANNELS;
    uint32_t right_bytes = row_bytes - left_bytes - active_bytes;
    uint16_t source_x[OD_INPUT_WIDTH];

    if (!g_od_tables_ready)
    {
        od_build_color_tables();
    }

    /* Padding (and anything past the 320x320x3 image) keeps the previous 0x80 fill. */
    memset(p_out, 0x80, first_row * row_bytes);
    memset(p_out + end_row * row_bytes, 0x80, out_bytes - end_row * row_bytes);

    for (uint32_t model_x = 0U; model_x < box.scaled_width; model_x++)
    {
        source_x[model_x] = (uint16_t) ((model_x * (uint32_t) width) / box.scaled_width);
    }

    for (uint32_t model_y = 0U; model_y < box.scaled_height; model_y++)
    {
        uint32_t source_y = (model_y * (uint32_t) height) / box.scaled_height;
        uint16_t const * p_row = p_pixels + source_y * stride_pixels;
        int8_t * p_dst = p_out + (first_row + model_y) * row_bytes;

        if (0U != left_bytes)
        {
            memset(p_dst, 0x80, left_bytes);
        }
        p_dst += left_bytes;
        for (uint32_t model_x = 0U; model_x < box.scaled_width; model_x++)
        {
            uint16_t pixel = p_row[source_x[model_x]];
            p_dst[0] = g_od_red_blue[(pixel >> 11) & 0x1FU];
            p_dst[1] = g_od_green[(pixel >> 5) & 0x3FU];
            p_dst[2] = g_od_red_blue[pixel & 0x1FU];
            p_dst += OD_INPUT_CHANNELS;
        }
        if (0U != right_bytes)
        {
            memset(p_dst, 0x80, right_bytes);
        }
    }
}

void od_add_candidate(od_candidate_t const * p_candidate,
                      od_candidate_t * p_candidates,
                      uint32_t * p_candidate_count)
{
    if (*p_candidate_count < OD_MAX_CANDIDATES)
    {
        p_candidates[*p_candidate_count] = *p_candidate;
        (*p_candidate_count)++;
        return;
    }

    uint32_t lowest = 0U;
    for (uint32_t i = 1U; i < OD_MAX_CANDIDATES; i++)
    {
        if (p_candidates[i].score < p_candidates[lowest].score)
        {
            lowest = i;
        }
    }

    if (p_candidate->score > p_candidates[lowest].score)
    {
        p_candidates[lowest] = *p_candidate;
    }
}

void od_decode_output(int8_t const * p_data,
                      int32_t rows,
                      int32_t columns,
                      float scale,
                      int32_t zero_point,
                      od_candidate_t * p_candidates,
                      uint32_t * p_candidate_count)
{
    static const float small_anchors[6] = {12.0F, 18.0F, 37.0F, 49.0F, 52.0F, 132.0F};
    static const float large_anchors[6] = {115.0F, 73.0F, 119.0F, 199.0F, 242.0F, 238.0F};
    const float model_size = 320.0F;
    const int32_t channels = (int32_t) OD_MODEL_OUTPUT_CHANNELS;
    float sigmoid_of[256];   /* index = q + 128 */
    float value_of[256];

    for (int32_t q = -128; q <= 127; q++)
    {
        value_of[q + 128] = od_dequantize((int8_t) q, scale, zero_point);
        sigmoid_of[q + 128] = od_sigmoid(value_of[q + 128]);
    }

    float const * p_anchors = (rows >= 20) ? small_anchors : large_anchors;
    float stride_x = model_size / (float) columns;
    float stride_y = model_size / (float) rows;

    for (int32_t row = 0; row < rows; row++)
    {
        for (int32_t column = 0; column < columns; column++)
        {
            for (int32_t anchor = 0; anchor < (int32_t) OD_MODEL_ANCHOR_COUNT; anchor++)
            {
                int8_t const * p_anchor = p_data + ((row * columns + column) * channels) +
                                          anchor * (int32_t) OD_MODEL_VALUES_PER_ANCHOR;
                float objectness = sigmoid_of[p_anchor[4] + 128];
                if (objectness < AI_OBSTACLE_SCORE_THRESHOLD)
                {
                    continue; /* score = objectness * class (<= 1) cannot reach the threshold */
                }
                float best_class_score = 0.0F;
                uint16_t best_class_id = 0U;
                /* Select the actual best COCO class before filtering (strict '>' keeps the
                 * first of equal scores, as before). */
                for (uint16_t class_id = 0U; class_id < OD_MODEL_CLASS_COUNT; class_id++)
                {
                    float class_score = sigmoid_of[p_anchor[OD_MODEL_BOX_VALUES + class_id] + 128];
                    if (class_score > best_class_score)
                    {
                        best_class_score = class_score;
                        best_class_id = class_id;
                    }
                }

                float score = objectness * best_class_score;
                if ((best_class_id >= OD_RETAINED_CLASS_COUNT) ||
                    (score < AI_OBSTACLE_SCORE_THRESHOLD))
                {
                    continue;
                }

                float tx = sigmoid_of[p_anchor[0] + 128];
                float ty = sigmoid_of[p_anchor[1] + 128];
                float tw = od_clamp(value_of[p_anchor[2] + 128], -10.0F, 10.0F);
                float th = od_clamp(value_of[p_anchor[3] + 128], -10.0F, 10.0F);
                float center_x = (tx + (float) column) * stride_x;
                float center_y = (ty + (float) row) * stride_y;
                float box_width = expf(tw) * p_anchors[anchor * 2];
                float box_height = expf(th) * p_anchors[anchor * 2 + 1];

                od_candidate_t candidate;
                memset(&candidate, 0, sizeof(candidate));
                candidate.x = od_clamp(center_x - box_width * 0.5F, 0.0F, model_size);
                candidate.y = od_clamp(center_y - box_height * 0.5F, 0.0F, model_size);
                candidate.width = od_clamp(box_width, 0.0F, model_size - candidate.x);
                candidate.height = od_clamp(box_height, 0.0F, model_size - candidate.y);
                candidate.score = score;
                candidate.class_id = best_class_id;
                candidate.suppressed = false;
                od_add_candidate(&candidate, p_candidates, p_candidate_count);
            }
        }
    }
}
