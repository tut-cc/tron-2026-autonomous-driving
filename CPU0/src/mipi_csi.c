/***********************************************************************************************************************
 * File Name    : mipi_csi.c
 * Description  : Contains data structures and functions used in hal_entry.c.
 **********************************************************************************************************************/
/***********************************************************************************************************************
* Copyright (c) 2025 Renesas Electronics Corporation and/or its affiliates
*
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************************************************************/

#include "mipi_csi.h"
#include "math.h"
#include <string.h>
#if defined(M85_UKERNEL)
#include <tk/tkernel.h>
#endif
#include "autonomy_controller.h"
#include "frame_rotation.h"

/* External variables */
extern sensor_reg_t live_camera;
extern sensor_reg_t color_bar_test_pattern;
extern const uint32_t g_test_pattern_color_one_line_1024x600[];
extern const uint32_t g_test_pattern_color_one_line_vga[];
extern const uint32_t g_test_pattern_color_one_line_qvga[];
extern const camera_config_t camera_profiles[RES_MAX];

extern const vin_extended_cfg_t g_vin_cfg_extend;

extern volatile uint8_t g_vsync_flag;

/* Global variables */
uint8_t * volatile gp_next_buffer;
uint16_t g_image_width = RESET_VALUE;
uint16_t g_image_height = RESET_VALUE;
_Bool g_is_set_resolution = false;

static uint8_t g_pending_stream_resolution = RES_QVGA;
static bool g_start_stream_after_resolution = false;
static bool g_ai_available = false;
/* Read in e2 studio when no USB diagnostic terminal is connected. */
volatile uint32_t g_ai_detector_status_code = OBSTACLE_DETECTOR_STATUS_NOT_STARTED;
static volatile uint32_t g_vin_complete_frames = 0U;
static volatile uint32_t g_capture_timestamp_ms;
/* Two analysis surfaces make the camera-side lifetime explicit.  The surface
 * handed to frame_stream_publish_navigation remains immutable while the USB
 * task owns its lease; the next accepted frame uses the other surface. */
static uint8_t g_navigation_input_snapshot[2][VIN_BYTES_PER_FRAME] BSP_ALIGN_VARIABLE(128)
        BSP_PLACE_IN_SECTION(BSP_UNINIT_SECTION_PREFIX ".sdram_noinit");
static uint8_t g_navigation_snapshot_index = 0U;

/* The camera service is a normal µT-Kernel task in the integrated image.
 * R_BSP_SoftwareDelay() is appropriate during bare-metal bring-up, but a
 * continuously runnable camera loop would starve the lower-priority video
 * consumer and hold its snapshot lease forever.  Keep the non-kernel build
 * behaviour for the original example while yielding in production. */
static void camera_task_delay(uint32_t milliseconds)
{
#if defined(M85_UKERNEL)
    if (milliseconds != 0U)
    {
        (void) tk_dly_tsk((RELTIM) milliseconds);
    }
#else
    R_BSP_SoftwareDelay(milliseconds, BSP_DELAY_UNITS_MILLISECONDS);
#endif
}

/* Per-frame timing of the AI loop, in microseconds (DWT cycle counter).
 * Read in e2 studio's Memory view while running; index = ai_stage_t.
 * "last" is the most recent processed frame, "max" the worst since boot. */
typedef enum
{
    AI_STAGE_COPY = 0,   /* D-cache invalidate + memcpy of the captured frame  */
    AI_STAGE_ROTATE,     /* 180-degree rotation + D-cache clean                 */
    AI_STAGE_DETECT,     /* obstacle detector: pre + NPU + post */
    AI_STAGE_NPU,        /* inference only (detector's own measurement)         */
    AI_STAGE_NAV,        /* road navigation analysis */
    AI_STAGE_SUBMIT,     /* autonomy controller update -> M33 IPC */
    AI_STAGE_PUBLISH,    /* frame_stream_publish_navigation() (USB descriptor)  */
    AI_STAGE_TOTAL,      /* COPY .. PUBLISH                                      */
    AI_STAGE_PERIOD,     /* start of this processed frame - start of previous   */
    AI_STAGE_COUNT
} ai_stage_t;
volatile uint32_t g_ai_stage_us[AI_STAGE_COUNT];
volatile uint32_t g_ai_stage_max_us[AI_STAGE_COUNT];
volatile uint32_t g_ai_stage_frames;

static uint32_t ai_cycles_to_us(uint32_t cycles)
{
    uint32_t per_us = SystemCoreClock / 1000000U;
    return (per_us > 0U) ? (cycles / per_us) : 0U;
}

static void ai_stage_record(ai_stage_t stage, uint32_t us)
{
    g_ai_stage_us[stage] = us;
    if (us > g_ai_stage_max_us[stage])
    {
        g_ai_stage_max_us[stage] = us;
    }
}

vin_extended_cfg_t g_vin_cfg_run_time_extend;
capture_cfg_t g_vin_cfg_run_time;

static fsp_err_t camera_mode_selection (void);
static fsp_err_t camera_serial_stream (void);
static fsp_err_t csi_check_image (uint8_t * const p_buffer, uint32_t width, uint32_t height);
static fsp_err_t vin_resolution_set (void);
static fsp_err_t vin_camera_start(capture_cfg_t const * p_cfg);
static fsp_err_t vin_scale_image(uint16_t new_width, uint16_t new_height);
static uint8_t stream_resolution_from_command(uint8_t command);
static void mipi_csi_receive_status_service(void);
static void mipi_csi_disable_notification_interrupts(void);

/***********************************************************************************************************************
 *  Function Name: mipi_csi_ep_entry
 *  Description  : This function is used to start MIPI CSI example operation.
 *  Arguments    : None
 *  Return Value : None
 **********************************************************************************************************************/
void mipi_csi_ep_entry(void)
{
    fsp_pack_version_t  version = {RESET_VALUE};
    fsp_err_t           err     = FSP_SUCCESS;

    /* Initialize the terminal */
    TERM_INIT();
    APP_PRINT("BOOT,stage=PCDC_READY\r\n");

    /* Version get API for FSP pack information */
    R_FSP_VersionGet(&version);

    /* Example Project information printed on the RTT */
    APP_PRINT (BANNER_INFO, EP_VERSION, version.version_id_b.major, version.version_id_b.minor, version.version_id_b.patch);
    APP_PRINT (EP_INFO);

    /* Initialize IIC module to control the switch onboard and the camera sensor */
    err = autonomy_controller_init();
    handle_error(err,"M33 startup/IPC failed\r\n");
    err = i2c_control_init();
    handle_error(err, "i2c_control_init FAILED \r\n");
    APP_PRINT("BOOT,stage=I2C_READY\r\n");

    /* Set the MIPI_SEL_PIN to high for using MIPI CSI */
    err = set_switch_state(MIPI_SEL_PIN, HIGH_STATE);
    handle_error(err, "set_switch_logic FAILED \r\n");
    APP_PRINT("BOOT,stage=SWITCH_READY\r\n");

    /* Setup the camera with MIPI CSI configuration */
    err = camera_open();
    handle_error(err, " ** camera_open FAILED ** \r\n");
    APP_PRINT("BOOT,stage=CAMERA_READY\r\n");

    /* Initialize TFLite Micro and the Ethos-U55 optimized car detection model. */
    err = obstacle_detector_init();
    g_ai_detector_status_code = (uint32_t) obstacle_detector_get_status();
    if (FSP_SUCCESS == err)
    {
        g_ai_available = true;
        APP_PRINT("BOOT,stage=AI_READY,arena_bytes=%lu\r\n", (unsigned long) 0x160000U);
    }
    else
    {
        g_ai_available = false;
        APP_PRINT("ERROR,AI_INIT,status=%u,fsp=0x%x,mode=CAMERA_ONLY_SAFE_STOP\r\n",
                  (unsigned int) obstacle_detector_get_status(),
                  (unsigned int) err);
        err = FSP_SUCCESS;
    }

    err = autonomy_controller_init();
    handle_error(err, " ** autonomy_controller_init FAILED ** \r\n");

    /* Start VIN module with default configuration */
    err = vin_camera_start(&g_vin_cfg);
    handle_error(err, "camera_vin_start FAILED \r\n");
    APP_PRINT("BOOT,stage=VIN_READY\r\n");

    /* The camera/AI task is a product service, not a PC-viewer demo.  Start
     * QVGA unconditionally; USB/PCDC is a low-priority diagnostic consumer
     * and must not gate inference or the M85->M33 heartbeat. */
    g_pending_stream_resolution = RES_QVGA;
    while (true)
    {
        err = vin_resolution_set();
        handle_error(err, " camera_resolution_set FAILED \r\n");

        g_start_stream_after_resolution = false;
        err = camera_serial_stream();
        handle_error(err, " camera_serial_stream FAILED \r\n");

        /* A terminal key may stop the diagnostic stream.  Re-arm the same
         * autonomous QVGA service instead of waiting for a PC menu command. */
        g_pending_stream_resolution = RES_QVGA;
        g_start_stream_after_resolution = true;
        g_is_set_resolution = false;

#if (DISPLAY_OUTPUT == 1U)
            do
            {
                g_vsync_flag = RESET_FLAG;
                /* Wait for a Vsync event */
                while(!g_vsync_flag)
                {
                    camera_task_delay(1U);
                }

                /* Update new frame for GLCDC display */
                err = R_GLCDC_BufferChange(&g_display_ctrl, (uint8_t * const) gp_next_buffer, DISPLAY_FRAME_LAYER_1);
                if (FSP_ERR_INVALID_UPDATE_TIMING != err)
                {
                    handle_error(err, "** R_GLCDC_BufferChange API FAILED **\r\n");
                }
            }
            while (FSP_ERR_INVALID_UPDATE_TIMING == err);
#endif /* DISPLAY_OUTPUT */
    }
}
/***********************************************************************************************************************
* End of function mipi_csi_ep_entry
***********************************************************************************************************************/

/***********************************************************************************************************************
 *  Function Name: csi_check_image
 *  Description  : This function is used to compare the image data with the color bar.
 *  Arguments    : p_buffer       Pointer to an image buffer
 *                 width          Width of image
 *                 height         Height of image
 *  Return Value : FSP_SUCCESS    Upon successful operation
 *                 Any Other Error code apart from FSP_SUCCESS
 **********************************************************************************************************************/
static fsp_err_t csi_check_image (uint8_t * const p_buffer, uint32_t width, uint32_t height)
{
    fsp_err_t   err                                 = FSP_SUCCESS;

    uint32_t    stride_width                        = RESET_VALUE;
    uint32_t    shift_bytes                         = RESET_VALUE;
    uint32_t    * p_one_line                        = NULL;
    uint32_t    * p_refer                           = NULL;
    uint32_t    * p_check                           = (uint32_t *)p_buffer;
    uint32_t    count                               = RESET_VALUE;
    float       rate                                = 0.0;
    char        str_rate[MATCH_RATE_STRING_LEN_MAX] = {RESET_VALUE};

    /* Calculate the necessary shift bytes when changing the resolution */
    stride_width = g_vin_cfg_extend.input_ctrl.preclip.pixel_end + 1;
    shift_bytes = (stride_width - width) / 2;

    /* Get one line data pointer at 1024x600 resolution */
    if (camera_profiles[RES_1024x600].width == width && camera_profiles[RES_1024x600].height == height)
    {
        p_one_line = (uint32_t *) g_test_pattern_color_one_line_1024x600;
    }
    /* Get one line data pointer at VGA resolution */
    else if (camera_profiles[RES_VGA].width == width && camera_profiles[RES_VGA].height == height)
    {
        p_one_line = (uint32_t *) g_test_pattern_color_one_line_vga;
    }
    /* Get one line data pointer at QVGA resolution */
    else if (camera_profiles[RES_QVGA].width == width && camera_profiles[RES_QVGA].height == height)
    {
        p_one_line = (uint32_t *) g_test_pattern_color_one_line_qvga;
    }
    else
    {
        err = FSP_ERR_INVALID_ARGUMENT;
        APP_ERR_RET(FSP_SUCCESS != err, err, "Resolution of image is not supported\r\n");
    }

    /* Check data by column */
    for(uint32_t y = 0; y < height; y++)
    {
        /* Reset p_refer pointer at start of one line data */
        p_refer = p_one_line;

        /* Check data by row */
        for(uint32_t x = 0; x < (width / 2) ; x ++)
        {
            /* Compare one pixel value pointed to by p_refer and p_check */
            if(*p_refer == *p_check)
            {
                count ++;
            }

            /* Next pixel (2 bytes per pixel) */
            p_check ++;
            p_refer ++;
        }
        /* Skip the padded area at the end of each line in the buffer */
        p_check += shift_bytes;
    }
    /* Calculate accuracy rate */
    rate = (float)count / (float)(width / 2 * height) * 100;

    /* Check accuracy rate of image data */
    if(MATCH_RATE_MIN < rate)
    {
        /* Convert float value to string */
        sprintf(str_rate,"%.2f", rate);
        APP_PRINT("\r\nImage data matches color bars to accuracy ratio: %s%%\r\n", str_rate);
    }
    else
    {
        err = FSP_ERR_INVALID_DATA;
        APP_ERR_RET(FSP_SUCCESS != err, err, "Image data does not match color bars\r\n");
    }
    return err;
}
/***********************************************************************************************************************
* End of function csi_check_image
***********************************************************************************************************************/

/***********************************************************************************************************************
 *  Function Name: vin_callback
 *  Description  : This function is used to get captured image from VIN callback
 *  Arguments    : p_args       Pointer to callback argument
 *  Return Value : None
 **********************************************************************************************************************/
void vin_callback (capture_callback_args_t * p_args)
 {
     vin_module_status_t    module_status    = (vin_module_status_t) p_args->event_status;
     vin_interrupt_status_t interrupt_status = (vin_interrupt_status_t) p_args->interrupt_status;
     FSP_PARAMETER_NOT_USED(module_status);

     switch (p_args->event)
     {
         case VIN_EVENT_NOTIFY:
         {
             mipi_csi_receive_status_service();

             if (interrupt_status.bits.frame_complete)
             {
                 gp_next_buffer = p_args->p_buffer;
                 g_capture_timestamp_ms = autonomy_controller_now_ms();
                 g_vin_complete_frames++;
             }

             break;
         }

         case VIN_EVENT_ERROR:
         {
             break;
         }

         default:
         {
             /* Do nothing */
             break;
         }
     }
 }
/***********************************************************************************************************************
* End of function vin_callback
***********************************************************************************************************************/

/***********************************************************************************************************************
 *  Function Name: mipi_csi0_callback
 *  Description  : This function is used to handle MIPI CSI event
 *  Arguments    : p_args      Pointer to callback argument
 *  Return Value : None
 **********************************************************************************************************************/
 void mipi_csi0_callback (mipi_csi_callback_args_t * p_args)
 {
     switch (p_args->event)
     {
         /*
          * In this project, the application does not rely on any MIPI CSI events.
          * FRAME_DATA event is ignored because VIN automatically handles captured frames.
          * DATA_LANE event status changes are not monitored (stable single-lane setup).
          * VIRTUAL_CHANNEL event is unused as only one channel 0 is active.
          * POWER and SHORT_PACKET_FIFO events are ignored for simplicity.
          */
         case MIPI_CSI_EVENT_DATA_LANE:
         case MIPI_CSI_EVENT_FRAME_DATA:
         case MIPI_CSI_EVENT_POWER:
         case MIPI_CSI_EVENT_SHORT_PACKET_FIFO:
         case MIPI_CSI_EVENT_VIRTUAL_CHANNEL:
             break;

         default:
             break;
     }
 }
 /***********************************************************************************************************************
* End of function mipi_csi0_callback
***********************************************************************************************************************/

/***********************************************************************************************************************
 *  Function Name: vin_resolution_set
 *  Description  : This function is used to configure the VIN, GLCDC with the selected resolution
 *  Arguments    : None
 *  Return Value : FSP_SUCCESS    Upon successful operation
 *                 Any Other Error code apart from FSP_SUCCESS
 **********************************************************************************************************************/
 static fsp_err_t vin_resolution_set (void)
 {
     fsp_err_t err = FSP_SUCCESS;

     uint8_t input_value = RESET_VALUE;
     uint8_t user_input[TERM_BUFFER_SIZE + 1] = {RESET_VALUE};
     uint8_t stream_resolution = RESET_VALUE;

     if (RESET_VALUE != g_pending_stream_resolution)
     {
         input_value = g_pending_stream_resolution;
         g_pending_stream_resolution = RESET_VALUE;
     }
     else
     {
         APP_PRINT(MAIN_MENU);

        while (!APP_CHECK_DATA)
        {
            mipi_csi_receive_status_service();
            camera_task_delay(1U);
        }

         /* Clean user input buffer */
         memset(user_input, NULL_CHAR, sizeof(user_input));

         /* Read user input data from terminal */
         TERM_READ(user_input, sizeof(user_input));

         stream_resolution = stream_resolution_from_command(user_input[INDEX_CHECK]);
         if (RESET_VALUE != stream_resolution)
         {
             input_value = stream_resolution;
             g_start_stream_after_resolution = true;
         }
         else
         {
             /* Convert numeric terminal input to a resolution. */
             input_value = (uint8_t)atoi((char*) &user_input[0]);
         }
     }

     if (input_value < RES_MAX && input_value > RESET_VALUE)
     {
         /* Update the image size according to the selected resolution */
         g_image_width = camera_profiles[input_value].width;
         g_image_height = camera_profiles[input_value].height;

#if (DISPLAY_OUTPUT == 1U)
         /* Re-Initialize and start the GLCDC module with selected resolution */
         err = glcdc_init();
         APP_ERR_RET(FSP_SUCCESS != err, err, "glcdc_init FAILED \r\n");
#endif /* DISPLAY_OUTPUT */

         /* Clear old images in SDRAM */
         memset(vin_image_buffer_1, RESET_VALUE, VIN_BYTES_PER_FRAME);
         memset(vin_image_buffer_2, RESET_VALUE, VIN_BYTES_PER_FRAME);
         memset(vin_image_buffer_3, RESET_VALUE, VIN_BYTES_PER_FRAME);

         /* Scale the output image via VIN module */
         err = vin_scale_image(g_image_width, g_image_height);
         APP_ERR_RET(FSP_SUCCESS != err, err, "vin_scale_image FAILED \r\n");

         /* Start VIN with run-time configuration */
         err = vin_camera_start(&g_vin_cfg_run_time);
         APP_ERR_RET( FSP_SUCCESS != err, err, " ** vin_camera_start FAILED ** \r\n");

         /* Set the g_is_set_resolution flag to exit the resolution selection and proceed to the camera mode selection. */
         g_is_set_resolution = true;

         APP_PRINT(CAMERA_MODE_SELECTION);

     }
     else
     {
         APP_PRINT("\r\nSelected resolution is invalid !\r\n");
     }

     return err;
 }
 /***********************************************************************************************************************
* End of function vin_resolution_set
***********************************************************************************************************************/

 /***********************************************************************************************************************
 *  Function Name: vin_camera_start
 *  Description  : This function is used to initialize the VIN module with inputed configuration
 *  Arguments    : p_cfg          Pointer to selected VIN configuration
 *  Return Value : FSP_SUCCESS    Upon successful operation
 *                 Any Other Error code apart from FSP_SUCCESS
 **********************************************************************************************************************/
static fsp_err_t vin_camera_start(capture_cfg_t const * p_cfg)
 {
     fsp_err_t err;

     /* Sanity-check the input pointer */
     if (p_cfg == NULL)
     {
         return FSP_ERR_INVALID_ARGUMENT;
     }

     /* Do not transmit a buffer from the previous VIN configuration. */
     gp_next_buffer = NULL;
     g_vin_complete_frames = 0U;

     /* Stop streaming before de-initialing VIN module */
     err = camera_stream_off();
     APP_ERR_RET( FSP_SUCCESS != err, err, " ** camera_stream_off FAILED ** \r\n");

     /* De-Initialize VIN module if it was opened */
     if (MODULE_CLOSE != g_vin_ctrl.open)
     {
         err = R_VIN_Close(&g_vin_ctrl);
         APP_ERR_RET( FSP_SUCCESS != err, err, " ** R_VIN_Close API FAILED ** \r\n");
     }

     /* Power down camera after de-initialing VIN module */
     err = write_reg_16bit(SYS_CTRL0_REG, SYS_CTRL0_SW_PWDN);
     APP_ERR_RET( FSP_SUCCESS != err, err, "write_reg_16bit to power down camera failed\r\n");

     /* Initialize VIN with the provided configuration */
     err = R_VIN_Open(&g_vin_ctrl, p_cfg);
     APP_ERR_RET( FSP_SUCCESS != err, err, " ** R_VIN_Open API FAILED ** \r\n");

     mipi_csi_disable_notification_interrupts();

     /* Start VIN capture engine */
     err = R_VIN_CaptureStart(&g_vin_ctrl, NULL);
     APP_ERR_RET( FSP_SUCCESS != err, err, " ** R_VIN_CaptureStart API FAILED ** \r\n");

     /* Start the camera sensor streaming */
     err = camera_stream_on();
     APP_ERR_RET( FSP_SUCCESS != err, err, " ** camera_stream_on FAILED ** \r\n");

     mipi_csi_receive_status_service();

     return err;
 }
/***********************************************************************************************************************
* End of function vin_camera_start
***********************************************************************************************************************/

/***********************************************************************************************************************
 *  Function Name: vin_scale_image
 *  Description  : This function is used to change the VIN parameters according to the inputed size
 *  Arguments    : new_width      Target width of image
 *                 new_height     Target height of image
 *  Return Value : FSP_SUCCESS    Upon successful operation
 *                 Any Other Error code apart from FSP_SUCCESS
 **********************************************************************************************************************/
static fsp_err_t vin_scale_image(uint16_t new_width, uint16_t new_height)
 {
     /* Validate arguments (avoid division by zero later) */
     if ((new_width == 0U) || (new_height == 0U))
     {
         return FSP_ERR_INVALID_ARGUMENT;
     }

     /* Copy the default extended configuration into the runtime copy */
     g_vin_cfg_run_time_extend = g_vin_cfg_extend;

     /* Fetch old output dimensions and masks from the default configuration */
     const uint32_t old_v  = (uint32_t) g_vin_cfg_extend.conversion_data.uds_clipping_bits.cl_vsize;
     const uint32_t old_h  = (uint32_t) g_vin_cfg_extend.conversion_data.uds_clipping_bits.cl_hsize;
     const uint16_t old_vm = (uint16_t) g_vin_cfg_extend.conversion_data.uds_scale_bits.vertical_mask;
     const uint16_t old_hm = (uint16_t) g_vin_cfg_extend.conversion_data.uds_scale_bits.horizontal_mask;

     /* Fetch input dimensions from the default configuration */
     const uint16_t input_height = (uint16_t) g_vin_cfg_extend.input_ctrl.preclip.line_end + 1;
     const uint16_t input_width = (uint16_t) g_vin_cfg_extend.input_ctrl.preclip.pixel_end + 1;

     /* Recalculate the scale masks: new_mask = (old_mask * old_size) / new_size */
     g_vin_cfg_run_time_extend.conversion_data.uds_scale_bits.vertical_mask =
         (uint16_t) ( ((uint32_t) old_vm * old_v) / new_height );

     g_vin_cfg_run_time_extend.conversion_data.uds_scale_bits.horizontal_mask =
         (uint16_t) ( ((uint32_t) old_hm * old_h) / new_width );

     /* Update clipping sizes to match the new target dimensions */
     g_vin_cfg_run_time_extend.conversion_data.uds_clipping_bits.cl_vsize = (uint16_t) new_height;
     g_vin_cfg_run_time_extend.conversion_data.uds_clipping_bits.cl_hsize = (uint16_t) new_width;

     /* Update scaling enable bit */
     if ((input_height != new_height) || (input_width != new_width))
     {
         g_vin_cfg_run_time_extend.input_ctrl.cfg_bits.scaling_enable = true;
     }
     else
     {
         g_vin_cfg_run_time_extend.input_ctrl.cfg_bits.scaling_enable = false;
     }

     /* Copy the default main configuration into the runtime copy */
     g_vin_cfg_run_time = g_vin_cfg;

     /* Point p_extend at the freshly updated runtime-extended configuration */
     g_vin_cfg_run_time.p_extend = &g_vin_cfg_run_time_extend;

     return FSP_SUCCESS;
 }
/***********************************************************************************************************************
* End of function vin_scale_image
***********************************************************************************************************************/

/***********************************************************************************************************************
 *  Function Name: camera_mode_selection
 *  Description  : This function is used to enable or disable test pattern mode of camera sensor
 *  Arguments    : None
 *  Return Value : FSP_SUCCESS    Upon successful operation
 *                 Any Other Error code apart from FSP_SUCCESS
 **********************************************************************************************************************/
static fsp_err_t camera_mode_selection (void)
{
    fsp_err_t err = FSP_SUCCESS;

    uint8_t user_input[TERM_BUFFER_SIZE + 1] = {RESET_VALUE};
    uint8_t stream_resolution = RESET_VALUE;

    mipi_csi_receive_status_service();

    if(APP_CHECK_DATA)
    {
        /* Clean user input buffer */
        memset(user_input, NULL_CHAR, sizeof(user_input));

        /* Read user input data from terminal */
        TERM_READ(user_input, sizeof(user_input));

        stream_resolution = stream_resolution_from_command(user_input[INDEX_CHECK]);

        if (RESET_VALUE != stream_resolution)
        {
            if ((camera_profiles[stream_resolution].width == g_image_width) &&
                (camera_profiles[stream_resolution].height == g_image_height))
            {
                g_start_stream_after_resolution = true;
                return FSP_SUCCESS;
            }

            g_pending_stream_resolution = stream_resolution;
            g_start_stream_after_resolution = true;
            g_is_set_resolution = false;
            return FSP_SUCCESS;
        }
        else if(SELECT_LIVE_CAMERA == user_input[INDEX_CHECK])
        {
            /* Change to live camera mode */
            err = camera_write_array(&live_camera);
            APP_ERR_RET(FSP_SUCCESS != err, err, "Change to live camera mode FAILED \r\n");

            APP_PRINT("\r\nLive camera streaming started\r\n");
        }
        else if(SELECT_TEST_PATTERN == user_input[INDEX_CHECK])
        {
            /* Change to camera test pattern mode */
            err = camera_write_array(&color_bar_test_pattern);
            APP_ERR_RET(FSP_SUCCESS != err, err, "Change to camera test mode FAILED \r\n");

            /* Check image data */
            err = csi_check_image(gp_next_buffer, g_image_width, g_image_height);
            APP_ERR_RET(FSP_SUCCESS != err, err, "csi_check_image FAILED \r\n");
        }
        else if(SELECT_SERIAL_STREAM == user_input[INDEX_CHECK])
        {
            err = camera_serial_stream();
            APP_ERR_RET(FSP_SUCCESS != err, err, "camera_serial_stream FAILED \r\n");
        }
        else if (BACK_TO_MAIN_MENU == user_input[INDEX_CHECK])
        {
            /* Clear g_is_set_resolution flag to return to main menu */
            g_is_set_resolution = false;
            return FSP_SUCCESS;
        }
        /* Selected is invalid */
        else
        {
            APP_PRINT("\r\nSelected mode is invalid !\r\n");
        }
    }

    return err;
}
/***********************************************************************************************************************
* End of function camera_mode_selection
***********************************************************************************************************************/

/***********************************************************************************************************************
 *  Function Name: camera_serial_stream
 *  Description  : This function periodically sends the latest RGB565 image through USB PCDC.
 *  Arguments    : None
 *  Return Value : FSP_SUCCESS    Upon successful operation
 *                 Any Other Error code apart from FSP_SUCCESS
 **********************************************************************************************************************/
static fsp_err_t camera_serial_stream (void)
{
    fsp_err_t err = FSP_SUCCESS;
    obstacle_detector_result_t detection_result = {0};
    road_navigation_result_t navigation_result = {0};
    uint8_t user_input[TERM_BUFFER_SIZE + 1] = {RESET_VALUE};
    uint8_t stream_resolution = RESET_VALUE;
    uint32_t wait_ms = RESET_VALUE;
    uint32_t last_processed_frame = UINT32_MAX;
    uint32_t previous_frame_start = 0U;
    bool have_previous_frame = false;

    err = camera_write_array(&live_camera);
    APP_ERR_RET(FSP_SUCCESS != err, err, "Change to live camera mode FAILED \r\n");

    APP_PRINT("\r\nEthos-U55 road navigation transfer started (%lu ms interval).\r\n",
              (unsigned long) PERIODIC_IMAGE_INTERVAL_MS);
    APP_PRINT("SIGNAL_FORMAT,seq,state,reason,enable,left_pwm,right_pwm,steer,speed,risk,stop,"
              "tof_mm,tof_valid,yolo_count,person,bicycle,car,motorcycle,"
              "white,brown,ai_seq,tof_seq,physical\r\n");
    APP_PRINT("COLOR_THRESHOLDS,white_r=%u-%u,white_g=%u-%u,white_b=%u-%u,"
              "brown_r=%u-%u,brown_g=%u-%u,brown_b=%u-%u,"
              "white_spread=%u,brown_rg_min=%u,brown_rg_max=%u,brown_gb=%u\r\n",
              (unsigned int) NAVIGATION_WHITE_R_MIN,
              (unsigned int) NAVIGATION_WHITE_R_MAX,
              (unsigned int) NAVIGATION_WHITE_G_MIN,
              (unsigned int) NAVIGATION_WHITE_G_MAX,
              (unsigned int) NAVIGATION_WHITE_B_MIN,
              (unsigned int) NAVIGATION_WHITE_B_MAX,
              (unsigned int) NAVIGATION_BROWN_R_MIN,
              (unsigned int) NAVIGATION_BROWN_R_MAX,
              (unsigned int) NAVIGATION_BROWN_G_MIN,
              (unsigned int) NAVIGATION_BROWN_G_MAX,
              (unsigned int) NAVIGATION_BROWN_B_MIN,
              (unsigned int) NAVIGATION_BROWN_B_MAX,
              (unsigned int) NAVIGATION_WHITE_MAX_CHANNEL_SPREAD,
              (unsigned int) NAVIGATION_BROWN_MIN_RED_OVER_GREEN,
              (unsigned int) NAVIGATION_BROWN_MAX_RED_OVER_GREEN,
              (unsigned int) NAVIGATION_BROWN_MIN_GREEN_OVER_BLUE);

    while (g_vin_complete_frames < 3U)
    {
        mipi_csi_receive_status_service();
        (void) APP_CHECK_KEY;
        camera_task_delay(1U);
    }

    while (!APP_CHECK_KEY)
    {
        mipi_csi_receive_status_service();

        if (NULL != gp_next_buffer)
        {
            /* Select the surface not currently published when the two-sided
             * analysis mailbox is in use.  If USB still owns the other
             * surface, publish returns IN_USE and only the diagnostic frame
             * is dropped; AI and control continue on this surface. */
            uint8_t * analysis_snapshot = g_navigation_input_snapshot[g_navigation_snapshot_index];

            /* Stage 1: acquire one completed frame and its original capture time. */
            uint32_t frame_id = g_vin_complete_frames;
            if (frame_id == last_processed_frame) {
                camera_task_delay(1U);
                continue;
            }
            uint32_t capture_ms = g_capture_timestamp_ms;
            uint32_t t_start = DWT->CYCCNT;
            uint8_t const * p_captured_frame = gp_next_buffer;
            uint32_t captured_bytes = (uint32_t) VIN_CFG_BYTES_PER_LINE * g_image_height;
            if (captured_bytes > VIN_BYTES_PER_FRAME) {
                err = FSP_ERR_INVALID_ARGUMENT;
                goto stream_cleanup;
            }
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
            SCB_InvalidateDCache_by_Addr((void *) p_captured_frame, (int32_t) captured_bytes);
#endif
            memcpy(analysis_snapshot, p_captured_frame, captured_bytes);
            if (frame_id != g_vin_complete_frames)
            {
                camera_task_delay(1U);
                continue;
            }
            last_processed_frame = frame_id;
            uint32_t t_copied = DWT->CYCCNT;
            /* Stage 2: correct the upside-down camera before any analysis. */
            if (frame_rotate_180_rgb565(analysis_snapshot, g_image_width,
                    g_image_height, VIN_CFG_IMAGE_STRIDE,
                    VIN_BYTES_PER_FRAME) != 0) {
                err = FSP_ERR_INVALID_ARGUMENT;
                goto stream_cleanup;
            }
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
            SCB_CleanDCache_by_Addr(analysis_snapshot, (int32_t) captured_bytes);
#endif

            /*
             * VIN continues rotating through its capture buffers while inference and USB
             * transfer run. Use a private snapshot so one analysis never mixes frames.
             */
            uint32_t t_rotated = DWT->CYCCNT;
            /* Stage 3: recognize objects in the rotated image. */
            memset(&detection_result, 0, sizeof(detection_result));
            if (g_ai_available)
            {
                err = obstacle_detector_run_rgb565(analysis_snapshot,
                                                   g_image_width,
                                                   g_image_height,
                                                   VIN_CFG_IMAGE_STRIDE,
                                                   &detection_result);
                g_ai_detector_status_code = (uint32_t) obstacle_detector_get_status();
                if (FSP_SUCCESS != err)
                {
                    APP_PRINT("ERROR,AI_INFERENCE,status=%u,fsp=0x%x,mode=CAMERA_ONLY_SAFE_STOP\r\n",
                              (unsigned int) obstacle_detector_get_status(),
                              (unsigned int) err);
                    memset(&detection_result, 0, sizeof(detection_result));
                    g_ai_available = false;
                    err = FSP_SUCCESS;
                }
            }

            uint32_t t_detected = DWT->CYCCNT;
            /* Stage 4: classify colors and derive the road in the same coordinates. */
            err = road_navigation_analyze_rgb565(analysis_snapshot,
                                                 g_image_width,
                                                 g_image_height,
                                                 VIN_CFG_IMAGE_STRIDE,
                                                 &detection_result,
                                                 &navigation_result);
            if (FSP_SUCCESS != err)
            {
                APP_PRINT("road_navigation_analyze_rgb565 FAILED \r\n");
                goto stream_cleanup;
            }

            uint32_t t_navigated = DWT->CYCCNT;
            if (g_ai_available)
            {
                autonomy_controller_update(&navigation_result,
                                           &detection_result,
                                           g_image_width,
                                           g_image_height, capture_ms);
            }
            else
            {
                autonomy_controller_report_ai_unavailable();
            }
            uint32_t t_submitted = DWT->CYCCNT;
            /* O(1) descriptor publish.  USB copying/transmit happens in its
             * low-priority task; a full/failed diagnostic stream never stops
             * AI, the camera task, or the safety heartbeat. */
            fsp_err_t publish_err = frame_stream_publish_navigation(analysis_snapshot,
                                                   g_image_width,
                                                   g_image_height,
                                                   &detection_result,
                                                   &navigation_result);
            if (FSP_SUCCESS == publish_err)
            {
                g_navigation_snapshot_index ^= 1U;
            }
            uint32_t t_published = DWT->CYCCNT;
            ai_stage_record(AI_STAGE_COPY, ai_cycles_to_us(t_copied - t_start));
            ai_stage_record(AI_STAGE_ROTATE, ai_cycles_to_us(t_rotated - t_copied));
            ai_stage_record(AI_STAGE_DETECT, ai_cycles_to_us(t_detected - t_rotated));
            ai_stage_record(AI_STAGE_NPU, detection_result.inference_time_us);
            ai_stage_record(AI_STAGE_NAV, ai_cycles_to_us(t_navigated - t_detected));
            ai_stage_record(AI_STAGE_SUBMIT, ai_cycles_to_us(t_submitted - t_navigated));
            ai_stage_record(AI_STAGE_PUBLISH, ai_cycles_to_us(t_published - t_submitted));
            ai_stage_record(AI_STAGE_TOTAL, ai_cycles_to_us(t_published - t_start));
            if (have_previous_frame)
            {
                ai_stage_record(AI_STAGE_PERIOD, ai_cycles_to_us(t_start - previous_frame_start));
            }
            previous_frame_start = t_start;
            have_previous_frame = true;
            ++g_ai_stage_frames;
        }

        /* Keep servicing PCDC commands while waiting for the next still image. */
        wait_ms = PERIODIC_IMAGE_INTERVAL_MS;
        while ((wait_ms > 0U) && !APP_CHECK_KEY)
        {
            uint32_t delay_ms = (wait_ms > 10U) ? 10U : wait_ms;
            mipi_csi_receive_status_service();
            camera_task_delay(delay_ms);
            wait_ms -= delay_ms;
        }
    }

stream_cleanup:
    autonomy_controller_report_ai_unavailable();
    (void)R_IIC_MASTER_SlaveAddressSet(&g_i2c_master_for_peripheral_ctrl,0x3c,I2C_MASTER_ADDR_MODE_7BIT);

    if (FSP_SUCCESS != err)
    {
        return err;
    }

    (void) TERM_READ(user_input, sizeof(user_input));
    stream_resolution = stream_resolution_from_command(user_input[INDEX_CHECK]);

    if (RESET_VALUE != stream_resolution)
    {
        g_pending_stream_resolution = stream_resolution;
        g_start_stream_after_resolution = true;
        g_is_set_resolution = false;
        APP_PRINT("\r\nEthos-U55 stream resolution change requested\r\n");
    }
    else
    {
        APP_PRINT("\r\nEthos-U55 road navigation transfer stopped\r\n");
        APP_PRINT(CAMERA_MODE_SELECTION);
    }

    return FSP_SUCCESS;
}
/***********************************************************************************************************************
* End of function camera_serial_stream
***********************************************************************************************************************/

static uint8_t stream_resolution_from_command(uint8_t command)
{
    switch (command)
    {
        case STREAM_RESOLUTION_FULL:
        case 'f':
        {
            return RES_1024X600;
        }

        case STREAM_RESOLUTION_VGA:
        case 'v':
        {
            return RES_VGA;
        }

        case STREAM_RESOLUTION_QVGA:
        case 'q':
        {
            return RES_QVGA;
        }

        default:
        {
            return RESET_VALUE;
        }
    }
}

static void mipi_csi_receive_status_service(void)
{
    if (0U != (R_MIPI_CSI->RXST & R_MIPI_CSI_RXST_RACTDET_Msk))
    {
        R_MIPI_CSI->RXSC = R_MIPI_CSI_RXSC_RACTDETC_Msk;
    }
}

static void mipi_csi_disable_notification_interrupts(void)
{
    uint32_t volatile * p_virtual_channel_enable = (uint32_t volatile *) &R_MIPI_CSI->VCIE0;

    R_MIPI_CSI->RXIE = 0U;
    R_MIPI_CSI->DLIE0 = 0U;
    R_MIPI_CSI->DLIE1 = 0U;
    for (uint32_t i = 0U; i < 16U; i++)
    {
        p_virtual_channel_enable[(0x10U / sizeof(uint32_t)) * i] = 0U;
    }
    R_MIPI_CSI->PMIE = 0U;
    R_MIPI_CSI->GSIE = 0U;
}

/***********************************************************************************************************************
 *  Function Name: handle_error
 *  Description  : Safely stops outputs and keeps reporting a fatal error over PCDC.
 *  Arguments    : err            error code
 *                 err_str        error string
 *  Return Value : None
 **********************************************************************************************************************/
void handle_error (fsp_err_t err, char * err_str)
{
    if(FSP_SUCCESS != err)
    {
        /* Print the error */
        APP_PRINT(err_str);

        autonomy_controller_report_ai_unavailable();

        /* Close opened VIN module*/
        if(0U != g_vin_ctrl.open)
        {
            if(FSP_SUCCESS != R_VIN_Close(&g_vin_ctrl))
            {
                APP_ERR_PRINT("R_VIN_Close FAILED\r\n");
            }
        }

        /* Close opened I2C Master module*/
        if(0U != g_i2c_master_for_peripheral_ctrl.open)
        {
            if(FSP_SUCCESS != R_IIC_MASTER_Close(&g_i2c_master_for_peripheral_ctrl))
            {
                APP_ERR_PRINT("R_IIC_MASTER_Close FAILED\r\n");
            }
        }

        /* Close opened GPT module*/
        if(0U != g_timer_periodic_ctrl.open)
        {
            if(FSP_SUCCESS != R_GPT_Close(&g_timer_periodic_ctrl))
            {
                APP_ERR_PRINT("R_GPT_Close FAILED\r\n");
            }
        }

        /* Do not execute BKPT here. A periodic message remains visible even if
         * Windows opens the PCDC port after the failure occurred. */
        while (true)
        {
            APP_PRINT("ERROR,FATAL,fsp=0x%x,action=CHECK_LAST_BOOT_STAGE\r\n",
                      (unsigned int) err);
            camera_task_delay(1000U);
        }
    }
}
/***********************************************************************************************************************
* End of function handle_error
***********************************************************************************************************************/
