/* generated vector source file - do not edit */
#include "bsp_api.h"
/* Do not build these data structures if no interrupts are currently allocated because IAR will have build errors. */
#if VECTOR_DATA_IRQ_COUNT > 0
        BSP_DONT_REMOVE const fsp_vector_t g_vector_table[BSP_ICU_VECTOR_NUM_ENTRIES] BSP_PLACE_IN_SECTION(BSP_SECTION_APPLICATION_VECTORS) =
        {
            [1] = rm_ethosu_isr, /* NPU IRQ (NPU IRQ) */
            [2] = usbfs_interrupt_handler, /* USBFS INT (USBFS interrupt) */
            [3] = usbfs_resume_handler, /* USBFS RESUME (USBFS resume interrupt) */
            [4] = usbfs_d0fifo_handler, /* USBFS FIFO 0 (DMA/DTC transfer request 0) */
            [5] = usbfs_d1fifo_handler, /* USBFS FIFO 1 (DMA/DTC transfer request 1) */
            [10] = vin_status_isr, /* VIN IRQ (Interrupt Request) */
            [11] = mipi_csi_rx_isr, /* MIPICSI RX (Receive interrupt) */
            [12] = mipi_csi_dl_isr, /* MIPICSI DL (Data Lane interrupt) */
            [13] = mipi_csi_vc_isr, /* MIPICSI VC (Virtual Channel interrupt) */
            [14] = glcdc_line_detect_isr, /* GLCDC LINE DETECT (Specified line) */
            [15] = glcdc_underflow_1_isr, /* GLCDC UNDERFLOW 1 (Graphic 1 underflow) */
            [16] = glcdc_underflow_2_isr, /* GLCDC UNDERFLOW 2 (Graphic 2 underflow) */
            [17] = sci_b_uart_rxi_isr, /* SCI8 RXI (Receive data full) */
            [18] = sci_b_uart_txi_isr, /* SCI8 TXI (Transmit data empty) */
            [19] = sci_b_uart_tei_isr, /* SCI8 TEI (Transmit end) */
            [20] = sci_b_uart_eri_isr, /* SCI8 ERI (Receive error) */
            [21] = layer3_switch_gwdi_isr, /* ETHER GWDI0 (GWCA data interrupt 0) */
            [22] = layer3_switch_eaei_isr, /* ETHER EAEI0 (ETHA0 error interrupt) */
            [23] = layer3_switch_eaei_isr, /* ETHER EAEI1 (ETHA1 error interrupt) */
        };
        #if BSP_FEATURE_ICU_HAS_IELSR
        const bsp_interrupt_event_t g_interrupt_event_link_select[BSP_ICU_VECTOR_NUM_ENTRIES] =
        {
            [1] = BSP_PRV_VECT_ENUM(EVENT_NPU_IRQ,GROUP1), /* NPU IRQ (NPU IRQ) */
            [2] = BSP_PRV_VECT_ENUM(EVENT_USBFS_INT,GROUP2), /* USBFS INT (USBFS interrupt) */
            [3] = BSP_PRV_VECT_ENUM(EVENT_USBFS_RESUME,GROUP3), /* USBFS RESUME (USBFS resume interrupt) */
            [4] = BSP_PRV_VECT_ENUM(EVENT_USBFS_FIFO_0,GROUP4), /* USBFS FIFO 0 (DMA/DTC transfer request 0) */
            [5] = BSP_PRV_VECT_ENUM(EVENT_USBFS_FIFO_1,GROUP5), /* USBFS FIFO 1 (DMA/DTC transfer request 1) */
            [10] = BSP_PRV_VECT_ENUM(EVENT_VIN_IRQ,GROUP2), /* VIN IRQ (Interrupt Request) */
            [11] = BSP_PRV_VECT_ENUM(EVENT_MIPICSI_RX,GROUP3), /* MIPICSI RX (Receive interrupt) */
            [12] = BSP_PRV_VECT_ENUM(EVENT_MIPICSI_DL,GROUP4), /* MIPICSI DL (Data Lane interrupt) */
            [13] = BSP_PRV_VECT_ENUM(EVENT_MIPICSI_VC,GROUP5), /* MIPICSI VC (Virtual Channel interrupt) */
            [14] = BSP_PRV_VECT_ENUM(EVENT_GLCDC_LINE_DETECT,GROUP6), /* GLCDC LINE DETECT (Specified line) */
            [15] = BSP_PRV_VECT_ENUM(EVENT_GLCDC_UNDERFLOW_1,GROUP7), /* GLCDC UNDERFLOW 1 (Graphic 1 underflow) */
            [16] = BSP_PRV_VECT_ENUM(EVENT_GLCDC_UNDERFLOW_2,GROUP0), /* GLCDC UNDERFLOW 2 (Graphic 2 underflow) */
            [17] = BSP_PRV_VECT_ENUM(EVENT_SCI8_RXI,GROUP1), /* SCI8 RXI (Receive data full) */
            [18] = BSP_PRV_VECT_ENUM(EVENT_SCI8_TXI,GROUP2), /* SCI8 TXI (Transmit data empty) */
            [19] = BSP_PRV_VECT_ENUM(EVENT_SCI8_TEI,GROUP3), /* SCI8 TEI (Transmit end) */
            [20] = BSP_PRV_VECT_ENUM(EVENT_SCI8_ERI,GROUP4), /* SCI8 ERI (Receive error) */
            [21] = BSP_PRV_VECT_ENUM(EVENT_ETHER_GWDI0,GROUP5), /* ETHER GWDI0 (GWCA data interrupt 0) */
            [22] = BSP_PRV_VECT_ENUM(EVENT_ETHER_EAEI0,GROUP6), /* ETHER EAEI0 (ETHA0 error interrupt) */
            [23] = BSP_PRV_VECT_ENUM(EVENT_ETHER_EAEI1,GROUP7), /* ETHER EAEI1 (ETHA1 error interrupt) */
        };
        #endif
        #endif
