/***********************************************************************************************************************
 * File Name    : usb_pcdc_console.c
 * Description  : USB PCDC terminal and RGB565 frame transport helper.
 **********************************************************************************************************************/
/***********************************************************************************************************************
* Copyright (c) 2026
*
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************************************************************/

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "hal_data.h"
#include "r_usb_basic.h"
#include "usb_pcdc_console.h"
#include "user_config.h"

#if (USE_VIRTUAL_COM == 1U)
#include "SERIAL_TERM/serial.h"
#endif

#define USB_PCDC_LINE_CODING_LENGTH     (7U)
#define USB_PCDC_READ_SIZE              (64U)
#define USB_PCDC_RX_RING_SIZE           (256U)
/* One R_USB_Write call produces one short Bulk-IN packet. */
#define USB_PCDC_TX_CHUNK_SIZE          (62U)
#define USB_PCDC_PRINTF_BUFFER_SIZE     (512U)
#define USB_PCDC_NUM_STRING_DESCRIPTOR  (7U)

extern uint8_t g_apl_device[];
extern uint8_t g_apl_configuration[];
extern uint8_t g_apl_hs_configuration[];
extern uint8_t g_apl_qualifier_descriptor[];
extern uint8_t * g_apl_string_table[];

const usb_descriptor_t usb_descriptor =
{
    g_apl_device,
    g_apl_configuration,
    g_apl_hs_configuration,
    g_apl_qualifier_descriptor,
    g_apl_string_table,
    USB_PCDC_NUM_STRING_DESCRIPTOR
};

static usb_status_t g_usb_event;
static usb_pcdc_linecoding_t g_line_coding;
static uint8_t g_rx_packet[USB_PCDC_READ_SIZE] BSP_ALIGN_VARIABLE(4);
static uint8_t g_rx_ring[USB_PCDC_RX_RING_SIZE];
static char g_printf_buffer[USB_PCDC_PRINTF_BUFFER_SIZE];

static volatile bool g_usb_open = false;
static volatile bool g_usb_configured = false;
static volatile bool g_usb_terminal_open = false;
static volatile bool g_usb_write_complete = true;
static volatile bool g_usb_read_pending = false;
static volatile uint32_t g_rx_head = 0U;
static volatile uint32_t g_rx_tail = 0U;

static fsp_err_t usb_pcdc_start_read(void);
static void usb_pcdc_push_rx(uint8_t const * p_data, uint32_t length);
static uint32_t usb_pcdc_pop_rx(uint8_t * p_data, uint32_t length);
static bool usb_pcdc_rx_has_line(void);
static bool usb_pcdc_rx_has_any(void);
static fsp_err_t usb_pcdc_wait_host(void);
static void usb_pcdc_refresh_connection_state(void);

fsp_err_t usb_pcdc_console_init(void)
{
    fsp_err_t err = FSP_SUCCESS;

#if (USE_VIRTUAL_COM == 1U)
    /* Some imported camera helper objects still call serial_printf directly. */
    (void) serial_init();
#endif

    if (!g_usb_open)
    {
        err = R_USB_Open(&g_basic0_ctrl, &g_basic0_cfg);
        if (FSP_SUCCESS != err)
        {
            return err;
        }

        g_usb_open = true;
    }

    /* USB configuration completes asynchronously after Windows enumerates the device. */
    usb_pcdc_console_poll();
    return FSP_SUCCESS;
}

void usb_pcdc_console_deinit(void)
{
    if (g_usb_open)
    {
        (void) R_USB_Close(&g_basic0_ctrl);
        g_usb_open = false;
        g_usb_configured = false;
        g_usb_terminal_open = false;
        g_usb_read_pending = false;
        g_usb_write_complete = true;
    }

#if (USE_VIRTUAL_COM == 1U)
    serial_deinit();
#endif
}

fsp_err_t usb_pcdc_console_write(uint8_t const * p_data, uint32_t length)
{
    fsp_err_t err = FSP_SUCCESS;
    uint32_t offset = 0U;

    if ((NULL == p_data) && (0U != length))
    {
        return FSP_ERR_ASSERTION;
    }

    while (offset < length)
    {
        uint32_t chunk = length - offset;
        if (chunk > USB_PCDC_TX_CHUNK_SIZE)
        {
            chunk = USB_PCDC_TX_CHUNK_SIZE;
        }

        while (true)
        {
            err = usb_pcdc_wait_host();
            if (FSP_SUCCESS != err)
            {
                return err;
            }

            while (!g_usb_write_complete)
            {
                usb_pcdc_console_poll();
            }

            if (!g_usb_configured)
            {
                continue;
            }

            g_usb_write_complete = false;
            err = R_USB_Write(&g_basic0_ctrl, &p_data[offset], chunk, USB_CLASS_PCDC);
            if (FSP_ERR_USB_BUSY == err)
            {
                g_usb_write_complete = true;
                usb_pcdc_console_poll();
                continue;
            }

            if (FSP_SUCCESS != err)
            {
                g_usb_write_complete = true;
                return err;
            }

            break;
        }

        while (!g_usb_write_complete)
        {
            usb_pcdc_console_poll();
        }

        if (!g_usb_configured)
        {
            continue;
        }

        offset += chunk;
    }

    return FSP_SUCCESS;
}

uint32_t usb_pcdc_console_printf(char const * p_format, ...)
{
    va_list args;
    int length = 0;

    if (NULL == p_format)
    {
        return (uint32_t) FSP_ERR_ASSERTION;
    }

    /* Do not let informational text block camera startup before the host is ready. */
    usb_pcdc_console_poll();
    if ((!g_usb_configured) || (!g_usb_terminal_open))
    {
        return 0U;
    }

    va_start(args, p_format);
    length = vsnprintf(g_printf_buffer, sizeof(g_printf_buffer), p_format, args);
    va_end(args);

    if (length < 0)
    {
        return (uint32_t) FSP_ERR_ABORTED;
    }

    if ((uint32_t) length >= sizeof(g_printf_buffer))
    {
        length = (int) sizeof(g_printf_buffer) - 1;
    }

    return (uint32_t) usb_pcdc_console_write((uint8_t const *) g_printf_buffer, (uint32_t) length);
}

uint32_t usb_pcdc_console_read(void * const p_buffer, uint32_t buffer_size)
{
    if ((NULL == p_buffer) || (0U == buffer_size))
    {
        return 0U;
    }

    usb_pcdc_console_poll();

    return usb_pcdc_pop_rx((uint8_t *) p_buffer, buffer_size);
}

uint32_t usb_pcdc_console_has_data(void)
{
    usb_pcdc_console_poll();
    return usb_pcdc_rx_has_line() ? 1U : 0U;
}

uint32_t usb_pcdc_console_has_key(void)
{
    usb_pcdc_console_poll();
    return usb_pcdc_rx_has_any() ? 1U : 0U;
}

void usb_pcdc_console_poll(void)
{
    usb_event_info_t event_info = {0};
    fsp_err_t err = FSP_SUCCESS;

    if (!g_usb_open)
    {
        return;
    }

    err = R_USB_EventGet(&event_info, &g_usb_event);
    if (FSP_SUCCESS != err)
    {
        return;
    }

    switch (g_usb_event)
    {
        case USB_STATUS_CONFIGURED:
        case USB_STATUS_RESUME:
        {
            g_usb_configured = true;
            g_usb_write_complete = true;
            (void) usb_pcdc_start_read();
            break;
        }

        case USB_STATUS_READ_COMPLETE:
        {
            if (USB_CLASS_PCDC != event_info.type)
            {
                break;
            }

            uint32_t size = event_info.data_size;
            if (size > USB_PCDC_READ_SIZE)
            {
                size = USB_PCDC_READ_SIZE;
            }

            g_usb_read_pending = false;
            if (size > 0U)
            {
                /* Receiving a command also proves that a host application owns the port. */
                g_usb_terminal_open = true;
            }
            usb_pcdc_push_rx(g_rx_packet, size);
            memset(g_rx_packet, 0, sizeof(g_rx_packet));

            if (g_usb_configured)
            {
                (void) usb_pcdc_start_read();
            }
            break;
        }

        case USB_STATUS_REQUEST:
        {
            uint16_t request = event_info.setup.request_type & USB_BREQUEST;

            if (USB_PCDC_SET_LINE_CODING == request)
            {
                (void) R_USB_PeriControlDataGet(&g_basic0_ctrl,
                                                (uint8_t *) &g_line_coding,
                                                USB_PCDC_LINE_CODING_LENGTH);
            }
            else if (USB_PCDC_GET_LINE_CODING == request)
            {
                (void) R_USB_PeriControlDataSet(&g_basic0_ctrl,
                                                (uint8_t *) &g_line_coding,
                                                USB_PCDC_LINE_CODING_LENGTH);
            }
            else if (USB_PCDC_SET_CONTROL_LINE_STATE == request)
            {
                g_usb_terminal_open = (0U != (event_info.setup.request_value & 0x0001U));
                (void) R_USB_PeriControlStatusSet(&g_basic0_ctrl, USB_SETUP_STATUS_ACK);
            }
            else
            {
                (void) R_USB_PeriControlStatusSet(&g_basic0_ctrl, USB_SETUP_STATUS_ACK);
            }
            break;
        }

        case USB_STATUS_WRITE_COMPLETE:
        {
            g_usb_write_complete = true;
            break;
        }

        case USB_STATUS_DETACH:
        case USB_STATUS_SUSPEND:
        {
            g_usb_configured = false;
            g_usb_terminal_open = false;
            g_usb_read_pending = false;
            g_usb_write_complete = true;
            break;
        }

        default:
        {
            break;
        }
    }

    usb_pcdc_refresh_connection_state();

    /* Retry a receive request if it was temporarily unavailable at configuration time. */
    if (g_usb_configured && !g_usb_read_pending)
    {
        (void) usb_pcdc_start_read();
    }
}

static fsp_err_t usb_pcdc_start_read(void)
{
    fsp_err_t err = FSP_SUCCESS;

    if ((!g_usb_configured) || g_usb_read_pending)
    {
        return FSP_SUCCESS;
    }

    err = R_USB_Read(&g_basic0_ctrl, g_rx_packet, USB_PCDC_READ_SIZE, USB_CLASS_PCDC);
    if (FSP_SUCCESS == err)
    {
        g_usb_read_pending = true;
    }

    return err;
}

static void usb_pcdc_push_rx(uint8_t const * p_data, uint32_t length)
{
    for (uint32_t i = 0U; i < length; i++)
    {
        uint32_t next = (g_rx_head + 1U) % USB_PCDC_RX_RING_SIZE;

        if (next == g_rx_tail)
        {
            g_rx_tail = (g_rx_tail + 1U) % USB_PCDC_RX_RING_SIZE;
        }

        g_rx_ring[g_rx_head] = p_data[i];
        g_rx_head = next;
    }
}

static uint32_t usb_pcdc_pop_rx(uint8_t * p_data, uint32_t length)
{
    uint32_t count = 0U;

    while ((count < length) && (g_rx_tail != g_rx_head))
    {
        p_data[count] = g_rx_ring[g_rx_tail];
        g_rx_tail = (g_rx_tail + 1U) % USB_PCDC_RX_RING_SIZE;
        count++;

        if ((p_data[count - 1U] == '\r') || (p_data[count - 1U] == '\n'))
        {
            break;
        }
    }

    return count;
}

static bool usb_pcdc_rx_has_line(void)
{
    uint32_t index = g_rx_tail;

    while (index != g_rx_head)
    {
        if ((g_rx_ring[index] == '\r') || (g_rx_ring[index] == '\n'))
        {
            return true;
        }

        index = (index + 1U) % USB_PCDC_RX_RING_SIZE;
    }

    return false;
}

static bool usb_pcdc_rx_has_any(void)
{
    return (g_rx_tail != g_rx_head);
}

static fsp_err_t usb_pcdc_wait_host(void)
{
    if (!g_usb_open)
    {
        return FSP_ERR_NOT_OPEN;
    }

    while ((!g_usb_configured) || (!g_usb_terminal_open))
    {
        usb_pcdc_console_poll();
    }

    return FSP_SUCCESS;
}

static void usb_pcdc_refresh_connection_state(void)
{
    usb_info_t info = {0};
    bool was_configured = g_usb_configured;

    if (FSP_SUCCESS != R_USB_InfoGet(&g_basic0_ctrl, &info, USB_CLASS_PCDC))
    {
        return;
    }

    if (USB_STATUS_CONFIGURED == info.device_status)
    {
        g_usb_configured = true;
        if (!was_configured)
        {
            g_usb_write_complete = true;
        }
    }
    else if ((USB_STATUS_DETACH == info.device_status) ||
             (USB_STATUS_SUSPEND == info.device_status) ||
             (USB_STATUS_DEFAULT == info.device_status) ||
             (USB_STATUS_ADDRESS == info.device_status))
    {
        g_usb_configured = false;
        g_usb_terminal_open = false;
        g_usb_read_pending = false;
        g_usb_write_complete = true;
    }
}
