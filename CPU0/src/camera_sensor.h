/***********************************************************************************************************************
 * File Name    : camera_sensor.h
 * Description  : OV5640 camera sensor declarations used by the MIPI CSI example.
 **********************************************************************************************************************/
/***********************************************************************************************************************
* Copyright (c) 2026
*
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************************************************************/

#ifndef CAMERA_SENSOR_H_
#define CAMERA_SENSOR_H_

#include "common_utils.h"
#include "user_config.h"

typedef struct st_sensor_reg
{
    uint16_t reg;
    uint8_t  val;
} sensor_reg_t;

typedef struct st_camera_config
{
    uint16_t width;
    uint16_t height;
} camera_config_t;

typedef enum e_camera_resolution
{
    RES_1024x600 = 1,
    RES_1024X600 = RES_1024x600,
    RES_VGA      = 2,
    RES_QVGA     = 3,
    RES_MAX      = 4,
} camera_resolution_t;

#define MATCH_RATE_STRING_LEN_MAX       (16U)
#define MATCH_RATE_MIN                  (90.0f)
#define SYS_CTRL0_REG                   (0x3008U)
#define SYS_CTRL0_SW_PWDN               (0x42U)

extern sensor_reg_t live_camera;
extern sensor_reg_t color_bar_test_pattern;
extern const camera_config_t camera_profiles[RES_MAX];
extern const uint32_t g_test_pattern_color_one_line_1024x600[];
extern const uint32_t g_test_pattern_color_one_line_vga[];
extern const uint32_t g_test_pattern_color_one_line_qvga[];

fsp_err_t camera_open(void);
fsp_err_t camera_stream_on(void);
fsp_err_t camera_stream_off(void);
fsp_err_t camera_write_array(sensor_reg_t * p_reg_list);

#endif /* CAMERA_SENSOR_H_ */
