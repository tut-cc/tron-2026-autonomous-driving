/* generated vector source file - do not edit */
#include "bsp_api.h"
/* Do not build these data structures if no interrupts are currently allocated because IAR will have build errors. */
#if VECTOR_DATA_IRQ_COUNT > 0
        BSP_DONT_REMOVE const fsp_vector_t g_vector_table[BSP_ICU_VECTOR_NUM_ENTRIES] BSP_PLACE_IN_SECTION(BSP_SECTION_APPLICATION_VECTORS) =
        {
                        [0] = gpt_counter_overflow_isr, /* GPT0 COUNTER OVERFLOW (Overflow) */
            [6] = iic_master_rxi_isr, /* IIC1 RXI (Receive data full) */
            [7] = iic_master_txi_isr, /* IIC1 TXI (Transmit data empty) */
            [8] = iic_master_tei_isr, /* IIC1 TEI (Transmit end) */
            [9] = iic_master_eri_isr, /* IIC1 ERI (Transfer error) */
        };
        #if BSP_FEATURE_ICU_HAS_IELSR
        const bsp_interrupt_event_t g_interrupt_event_link_select[BSP_ICU_VECTOR_NUM_ENTRIES] =
        {
            [0] = BSP_PRV_VECT_ENUM(EVENT_GPT0_COUNTER_OVERFLOW,GROUP0), /* GPT0 COUNTER OVERFLOW (Overflow) */
            [6] = BSP_PRV_VECT_ENUM(EVENT_IIC1_RXI,GROUP6), /* IIC1 RXI (Receive data full) */
            [7] = BSP_PRV_VECT_ENUM(EVENT_IIC1_TXI,GROUP7), /* IIC1 TXI (Transmit data empty) */
            [8] = BSP_PRV_VECT_ENUM(EVENT_IIC1_TEI,GROUP0), /* IIC1 TEI (Transmit end) */
            [9] = BSP_PRV_VECT_ENUM(EVENT_IIC1_ERI,GROUP1), /* IIC1 ERI (Transfer error) */
        };
        #endif
        #endif