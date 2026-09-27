/***********************************************************************************************************************
 * File Name    : obstacle_detector.cc
 * Description  : RGB565 preprocessing, Ethos-U55 inference, and YOLO-Fastest postprocessing.
 **********************************************************************************************************************/

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "hal_data.h"
#include "ai/obstacle_detector.h"
#include "ai/model/YoloFastestModel.hpp"
#include "user_config.h"
#include "ai/obstacle_kernels.h"

namespace arm
{
namespace app
{
namespace object_detection
{
uint8_t const * GetModelPointer();
size_t GetModelLen();
} /* namespace object_detection */
} /* namespace app */
} /* namespace arm */

namespace
{
/* The Vela performance build needs more working memory than the model's
 * activation-only estimate. Keep headroom for TFLM allocator metadata. */
constexpr uint32_t TENSOR_ARENA_SIZE = 0x160000U;
constexpr uint32_t MAX_CANDIDATES = 64U;
constexpr uint32_t MODEL_CLASS_COUNT = 80U;
constexpr uint32_t MODEL_BOX_VALUES = 5U;
constexpr uint32_t MODEL_ANCHOR_COUNT = 3U;
constexpr uint32_t MODEL_VALUES_PER_ANCHOR = MODEL_BOX_VALUES + MODEL_CLASS_COUNT;
constexpr uint32_t MODEL_OUTPUT_CHANNELS = MODEL_ANCHOR_COUNT * MODEL_VALUES_PER_ANCHOR;

/* Hot loops live in obstacle_kernels.c (host-tested bit-exact against the
 * previous in-line code); these aliases keep the rest of this file unchanged. */
using Candidate = od_candidate_t;
using Letterbox = od_letterbox_t;
static_assert(MAX_CANDIDATES == OD_MAX_CANDIDATES, "candidate buffer size");
static_assert(MODEL_OUTPUT_CHANNELS == OD_MODEL_OUTPUT_CHANNELS, "YOLO output layout");
static_assert(OBSTACLE_DETECTOR_INPUT_WIDTH == OD_INPUT_WIDTH &&
              OBSTACLE_DETECTOR_INPUT_HEIGHT == OD_INPUT_HEIGHT &&
              OBSTACLE_DETECTOR_INPUT_CHANNELS == OD_INPUT_CHANNELS, "model input");
static_assert(OBSTACLE_DETECTOR_CLASS_COUNT == OD_RETAINED_CLASS_COUNT, "retained classes");

/* TFLM activation memory is diagnostic/AI working state, not control state.
 * Keep it in SDRAM so CPU0's fixed 0x22000000..0x2217ffff RAM partition and
 * the non-cache shared mailbox remain disjoint. */
alignas(16) uint8_t g_tensor_arena[TENSOR_ARENA_SIZE]
    BSP_PLACE_IN_SECTION(BSP_UNINIT_SECTION_PREFIX ".sdram_noinit");
arm::app::YoloFastestModel g_model;
bool g_initialized = false;
obstacle_detector_status_t g_status = OBSTACLE_DETECTOR_STATUS_NOT_STARTED;

bool tensor_has_shape(TfLiteTensor const * p_tensor,
                      int32_t dimension_0,
                      int32_t dimension_1,
                      int32_t dimension_2,
                      int32_t dimension_3)
{
    return (nullptr != p_tensor) &&
           (kTfLiteInt8 == p_tensor->type) &&
           (nullptr != p_tensor->dims) &&
           (4 == p_tensor->dims->size) &&
           (dimension_0 == p_tensor->dims->data[0]) &&
           (dimension_1 == p_tensor->dims->data[1]) &&
           (dimension_2 == p_tensor->dims->data[2]) &&
           (dimension_3 == p_tensor->dims->data[3]) &&
           (nullptr != p_tensor->data.int8) &&
           (p_tensor->params.scale > 0.0F);
}

float clamp_float(float value, float minimum, float maximum)
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

Letterbox calculate_letterbox(uint16_t width, uint16_t height)
{
    return od_letterbox(width, height);
}

float intersection_over_union(Candidate const & a, Candidate const & b)
{
    float left = (a.x > b.x) ? a.x : b.x;
    float top = (a.y > b.y) ? a.y : b.y;
    float right = ((a.x + a.width) < (b.x + b.width)) ?
                  (a.x + a.width) : (b.x + b.width);
    float bottom = ((a.y + a.height) < (b.y + b.height)) ?
                   (a.y + a.height) : (b.y + b.height);
    float intersection_width = right - left;
    float intersection_height = bottom - top;

    if ((intersection_width <= 0.0F) || (intersection_height <= 0.0F))
    {
        return 0.0F;
    }

    float intersection = intersection_width * intersection_height;
    float union_area = (a.width * a.height) + (b.width * b.height) - intersection;
    return (union_area > 0.0F) ? (intersection / union_area) : 0.0F;
}

void sort_candidates(Candidate * p_candidates, uint32_t count)
{
    for (uint32_t i = 1U; i < count; i++)
    {
        Candidate value = p_candidates[i];
        uint32_t position = i;
        while ((position > 0U) && (p_candidates[position - 1U].score < value.score))
        {
            p_candidates[position] = p_candidates[position - 1U];
            position--;
        }
        p_candidates[position] = value;
    }
}

void add_output_candidates(TfLiteTensor const * p_output,
                           Candidate * p_candidates,
                           uint32_t * p_candidate_count)
{
    if ((nullptr == p_output) || (nullptr == p_output->dims) ||
        (p_output->dims->size < 4) || (nullptr == p_output->data.int8))
    {
        return;
    }

    int32_t rows = p_output->dims->data[1];
    int32_t columns = p_output->dims->data[2];
    int32_t channels = p_output->dims->data[3];
    if ((rows <= 0) || (columns <= 0) ||
        (channels != static_cast<int32_t>(MODEL_OUTPUT_CHANNELS)))
    {
        return;
    }

    od_decode_output(p_output->data.int8, rows, columns, p_output->params.scale,
                     p_output->params.zero_point, p_candidates, p_candidate_count);
}

fsp_err_t prepare_input(uint8_t const * p_frame,
                        uint16_t width,
                        uint16_t height,
                        uint16_t stride_pixels,
                        TfLiteTensor * p_input)
{
    constexpr uint32_t REQUIRED_BYTES = OBSTACLE_DETECTOR_INPUT_WIDTH *
                                        OBSTACLE_DETECTOR_INPUT_HEIGHT *
                                        OBSTACLE_DETECTOR_INPUT_CHANNELS;
    if ((nullptr == p_input) || (kTfLiteInt8 != p_input->type) ||
        (p_input->bytes < REQUIRED_BYTES) || (nullptr == p_input->data.int8))
    {
        return FSP_ERR_INVALID_DATA;
    }

    od_prepare_input(reinterpret_cast<uint16_t const *>(p_frame), width, height, stride_pixels,
                     p_input->data.int8, p_input->bytes);
    return FSP_SUCCESS;
}

void map_results(Candidate * p_candidates,
                 uint32_t candidate_count,
                 uint16_t width,
                 uint16_t height,
                 obstacle_detector_result_t * p_result)
{
    Letterbox box = calculate_letterbox(width, height);
    float active_left = static_cast<float>(box.pad_x);
    float active_top = static_cast<float>(box.pad_y);
    float active_right = active_left + static_cast<float>(box.scaled_width);
    float active_bottom = active_top + static_cast<float>(box.scaled_height);

    sort_candidates(p_candidates, candidate_count);

    for (uint32_t i = 0U; i < candidate_count; i++)
    {
        if (p_candidates[i].suppressed)
        {
            continue;
        }

        for (uint32_t j = i + 1U; j < candidate_count; j++)
        {
            if ((!p_candidates[j].suppressed) &&
                (p_candidates[i].class_id == p_candidates[j].class_id) &&
                (intersection_over_union(p_candidates[i], p_candidates[j]) > AI_OBSTACLE_NMS_THRESHOLD))
            {
                p_candidates[j].suppressed = true;
            }
        }

        if (p_result->detection_count >= OBSTACLE_DETECTOR_MAX_DETECTIONS)
        {
            break;
        }

        float left = clamp_float(p_candidates[i].x, active_left, active_right);
        float top = clamp_float(p_candidates[i].y, active_top, active_bottom);
        float right = clamp_float(p_candidates[i].x + p_candidates[i].width, active_left, active_right);
        float bottom = clamp_float(p_candidates[i].y + p_candidates[i].height, active_top, active_bottom);
        if ((right <= left) || (bottom <= top))
        {
            continue;
        }

        uint32_t mapped_x = static_cast<uint32_t>(
            (left - active_left) * static_cast<float>(width) / static_cast<float>(box.scaled_width));
        uint32_t mapped_y = static_cast<uint32_t>(
            (top - active_top) * static_cast<float>(height) / static_cast<float>(box.scaled_height));
        uint32_t mapped_right = static_cast<uint32_t>(
            (right - active_left) * static_cast<float>(width) / static_cast<float>(box.scaled_width));
        uint32_t mapped_bottom = static_cast<uint32_t>(
            (bottom - active_top) * static_cast<float>(height) / static_cast<float>(box.scaled_height));

        if (mapped_right > width)
        {
            mapped_right = width;
        }
        if (mapped_bottom > height)
        {
            mapped_bottom = height;
        }
        if ((mapped_x >= mapped_right) || (mapped_y >= mapped_bottom))
        {
            continue;
        }

        obstacle_detection_t & output = p_result->detections[p_result->detection_count];
        output.x = static_cast<uint16_t>(mapped_x);
        output.y = static_cast<uint16_t>(mapped_y);
        output.width = static_cast<uint16_t>(mapped_right - mapped_x);
        output.height = static_cast<uint16_t>(mapped_bottom - mapped_y);
        output.score_per_mille = static_cast<uint16_t>(
            clamp_float(p_candidates[i].score, 0.0F, 1.0F) * 1000.0F);
        output.class_id = p_candidates[i].class_id;
        p_result->detection_count++;
    }
}

} /* anonymous namespace */

extern "C" char const * obstacle_detector_class_name(uint16_t class_id)
{
    static char const * const names[OBSTACLE_DETECTOR_CLASS_COUNT] =
    {
        "person",
        "bicycle",
        "car",
        "motorcycle",
    };

    return (class_id < OBSTACLE_DETECTOR_CLASS_COUNT) ? names[class_id] : "unknown";
}

extern "C" fsp_err_t obstacle_detector_init(void)
{
    if (g_initialized)
    {
        return FSP_SUCCESS;
    }

    fsp_err_t err = RM_ETHOSU_Open(&g_rm_ethosu0_ctrl, &g_rm_ethosu0_cfg);
    if (FSP_SUCCESS != err)
    {
        g_status = OBSTACLE_DETECTOR_STATUS_ETHOS_OPEN_FAILED;
        return err;
    }

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    if (!g_model.Init(g_tensor_arena,
                      sizeof(g_tensor_arena),
                      arm::app::object_detection::GetModelPointer(),
                      static_cast<uint32_t>(arm::app::object_detection::GetModelLen())))
    {
        g_status = OBSTACLE_DETECTOR_STATUS_MODEL_INIT_FAILED;
        (void) RM_ETHOSU_Close(&g_rm_ethosu0_ctrl);
        return FSP_ERR_NOT_INITIALIZED;
    }

    TfLiteTensor const * p_input = g_model.GetInputTensor(0U);
    if (!tensor_has_shape(p_input,
                          1,
                          static_cast<int32_t>(OBSTACLE_DETECTOR_INPUT_HEIGHT),
                          static_cast<int32_t>(OBSTACLE_DETECTOR_INPUT_WIDTH),
                          static_cast<int32_t>(OBSTACLE_DETECTOR_INPUT_CHANNELS)))
    {
        g_status = OBSTACLE_DETECTOR_STATUS_INPUT_TENSOR_INVALID;
        (void) RM_ETHOSU_Close(&g_rm_ethosu0_ctrl);
        return FSP_ERR_INVALID_DATA;
    }

    if (2U != g_model.GetNumOutputs())
    {
        g_status = OBSTACLE_DETECTOR_STATUS_OUTPUT_COUNT_INVALID;
        (void) RM_ETHOSU_Close(&g_rm_ethosu0_ctrl);
        return FSP_ERR_INVALID_DATA;
    }

    bool has_20_grid = false;
    bool has_10_grid = false;
    for (size_t output_index = 0U; output_index < g_model.GetNumOutputs(); output_index++)
    {
        TfLiteTensor const * p_output = g_model.GetOutputTensor(output_index);
        has_20_grid = has_20_grid || tensor_has_shape(p_output, 1, 20, 20,
                                                       static_cast<int32_t>(MODEL_OUTPUT_CHANNELS));
        has_10_grid = has_10_grid || tensor_has_shape(p_output, 1, 10, 10,
                                                       static_cast<int32_t>(MODEL_OUTPUT_CHANNELS));
    }
    if ((!has_20_grid) || (!has_10_grid))
    {
        g_status = OBSTACLE_DETECTOR_STATUS_OUTPUT_TENSOR_INVALID;
        (void) RM_ETHOSU_Close(&g_rm_ethosu0_ctrl);
        return FSP_ERR_INVALID_DATA;
    }

    g_initialized = true;
    g_status = OBSTACLE_DETECTOR_STATUS_READY;
    return FSP_SUCCESS;
}

extern "C" obstacle_detector_status_t obstacle_detector_get_status(void)
{
    return g_status;
}

extern "C" fsp_err_t obstacle_detector_run_rgb565(uint8_t const * p_frame,
                                                   uint16_t width,
                                                   uint16_t height,
                                                   uint16_t stride_pixels,
                                                   obstacle_detector_result_t * p_result)
{
    if ((!g_initialized) || (nullptr == p_frame) || (nullptr == p_result) ||
        (0U == width) || (0U == height) || (stride_pixels < width))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    std::memset(p_result, 0, sizeof(*p_result));

    uint32_t source_length = ((static_cast<uint32_t>(height) - 1U) * stride_pixels + width) * 2U;
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    SCB_InvalidateDCache_by_Addr(const_cast<uint8_t *>(p_frame), static_cast<int32_t>(source_length));
#endif

    TfLiteTensor * p_input = g_model.GetInputTensor(0U);
    fsp_err_t err = prepare_input(p_frame, width, height, stride_pixels, p_input);
    if (FSP_SUCCESS != err)
    {
        g_status = OBSTACLE_DETECTOR_STATUS_PREPROCESS_FAILED;
        return err;
    }

    uint32_t start_cycles = DWT->CYCCNT;
    if (!g_model.RunInference())
    {
        g_status = OBSTACLE_DETECTOR_STATUS_INFERENCE_FAILED;
        return FSP_ERR_ABORTED;
    }
    uint32_t elapsed_cycles = DWT->CYCCNT - start_cycles;
    uint32_t cycles_per_microsecond = SystemCoreClock / 1000000U;
    p_result->inference_time_us = (cycles_per_microsecond > 0U) ?
                                  (elapsed_cycles / cycles_per_microsecond) : 0U;

    Candidate candidates[MAX_CANDIDATES] = {};
    uint32_t candidate_count = 0U;
    for (size_t output_index = 0U; output_index < g_model.GetNumOutputs(); output_index++)
    {
        add_output_candidates(g_model.GetOutputTensor(output_index), candidates, &candidate_count);
    }
    map_results(candidates, candidate_count, width, height, p_result);

    g_status = OBSTACLE_DETECTOR_STATUS_READY;
    return FSP_SUCCESS;
}
