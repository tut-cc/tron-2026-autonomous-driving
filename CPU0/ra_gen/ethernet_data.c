/* generated HAL source file - do not edit */
#include "ethernet_instances.h"
/* Macros to tie dynamic ELC links to adc_b_trigger_sync_elc option in adc_b_trigger_t. */
#define ADC_B_TRIGGER_ADC_B0        ADC_B_TRIGGER_SYNC_ELC
#define ADC_B_TRIGGER_ADC_B0_B      ADC_B_TRIGGER_SYNC_ELC
#define ADC_B_TRIGGER_ADC_B1        ADC_B_TRIGGER_SYNC_ELC
#define ADC_B_TRIGGER_ADC_B1_B      ADC_B_TRIGGER_SYNC_ELC
rmac_instance_ctrl_t g_ether0_ctrl;
static rmac_buffer_node_t g_ether0_buffer_node_list[128];

uint8_t g_ether0_mac_address[6] =
{ 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 };
layer3_switch_target_port_bitmaps_t g_ether0_target_port_bitmap =
{ .ports =
{ 0U, }, };

__attribute__((section(".ram_nocache"), aligned(32))) layer3_switch_ts_reception_process_descriptor_t g_ether0_ts_descriptor_array0[8];
rmac_queue_info_t g_ether0_ts_queue[1] =
{
{ .queue_cfg =
{ .array_length = 8, .p_descriptor_array = NULL, .p_ts_descriptor_array = g_ether0_ts_descriptor_array0, .ports = (1
        << 1),
  .type = LAYER3_SWITCH_QUEUE_TYPE_TX, .write_back_mode = LAYER3_SWITCH_WRITE_BACK_MODE_FULL, .descriptor_format =
          LAYER3_SWITCH_DISCRIPTOR_FORMTAT_TX_TIMESTAMP,
  .rx_timestamp_storage = LAYER3_SWITCH_RX_TIMESTAMP_STORAGE_DISABLE, } }, };
__attribute__((section(".ram_nocache"), aligned(32))) layer3_switch_descriptor_t g_ether0_tx_descriptor_array0[3 + 1];
__attribute__((section(".ram_nocache"), aligned(32))) layer3_switch_descriptor_t g_ether0_tx_descriptor_array1[3 + 1];
rmac_queue_info_t g_ether0_tx_queue_list[2] =
{
{ .queue_cfg =
{ .array_length = 3 + 1, .p_descriptor_array = g_ether0_tx_descriptor_array0, .p_ts_descriptor_array = NULL, .ports = (1
        << 1),
  .type = LAYER3_SWITCH_QUEUE_TYPE_TX, .write_back_mode = LAYER3_SWITCH_WRITE_BACK_MODE_FULL, .descriptor_format =
          LAYER3_SWITCH_DISCRIPTOR_FORMTAT_EXTENDED,
  .rx_timestamp_storage = LAYER3_SWITCH_RX_TIMESTAMP_STORAGE_DISABLE, } },
  { .queue_cfg =
  { .array_length = 3 + 1, .p_descriptor_array = g_ether0_tx_descriptor_array1, .p_ts_descriptor_array = NULL, .ports =
            (1 << 1),
    .type = LAYER3_SWITCH_QUEUE_TYPE_TX, .write_back_mode = LAYER3_SWITCH_WRITE_BACK_MODE_FULL, .descriptor_format =
            LAYER3_SWITCH_DISCRIPTOR_FORMTAT_EXTENDED,
    .rx_timestamp_storage = LAYER3_SWITCH_RX_TIMESTAMP_STORAGE_DISABLE, } }, };
__attribute__((section(".ram_nocache"), aligned(32))) layer3_switch_descriptor_t g_ether0_rx_descriptor_array0[31 + 1];
__attribute__((section(".ram_nocache"), aligned(32))) layer3_switch_descriptor_t g_ether0_rx_descriptor_array1[31 + 1];
rmac_queue_info_t g_ether0_rx_queue_list[2] =
{
{ .queue_cfg =
{ .array_length = 31 + 1, .p_descriptor_array = g_ether0_rx_descriptor_array0, .p_ts_descriptor_array = NULL, .ports =
          (1 << 1) | (0x0),
  .type = LAYER3_SWITCH_QUEUE_TYPE_RX, .write_back_mode = LAYER3_SWITCH_WRITE_BACK_MODE_FULL, .descriptor_format =
          LAYER3_SWITCH_DISCRIPTOR_FORMTAT_EXTENDED,
#if LAYER3_SWITCH_CFG_GPTP_ENABLE
.rx_timestamp_storage = LAYER3_SWITCH_RX_TIMESTAMP_STORAGE_ENABLE,
#else
  .rx_timestamp_storage = LAYER3_SWITCH_RX_TIMESTAMP_STORAGE_DISABLE,
#endif
        } },
  { .queue_cfg =
  { .array_length = 31 + 1, .p_descriptor_array = g_ether0_rx_descriptor_array1, .p_ts_descriptor_array = NULL, .ports =
            (1 << 1) | (0x0),
    .type = LAYER3_SWITCH_QUEUE_TYPE_RX, .write_back_mode = LAYER3_SWITCH_WRITE_BACK_MODE_FULL, .descriptor_format =
            LAYER3_SWITCH_DISCRIPTOR_FORMTAT_EXTENDED,
#if LAYER3_SWITCH_CFG_GPTP_ENABLE
.rx_timestamp_storage = LAYER3_SWITCH_RX_TIMESTAMP_STORAGE_ENABLE,
#else
    .rx_timestamp_storage = LAYER3_SWITCH_RX_TIMESTAMP_STORAGE_DISABLE,
#endif
          } }, };

const rmac_extended_cfg_t g_ether0_extended_cfg_t =
{ .p_ether_switch = &g_layer3_switch0, .tx_queue_num = 2, .rx_queue_num = 2,

.p_ts_queue = g_ether0_ts_queue,
  .p_tx_queue_list = g_ether0_tx_queue_list, .p_rx_queue_list = g_ether0_rx_queue_list,
#if defined(VECTOR_NUMBER_ETHER_RMPI1)
                .rmpi_irq                = VECTOR_NUMBER_ETHER_RMPI1,
#else
  .rmpi_irq = FSP_INVALID_VECTOR,
#endif
  .rmpi_ipl = (BSP_IRQ_DISABLED),
  .p_buffer_node_list = g_ether0_buffer_node_list, .buffer_node_num = 128, .transmission_descriptor_format =
          RMAC_TRANSMISSION_DESCRIPTOR_FORMAT_DIRECT,
  .p_link_monitored_ports = &g_ether0_target_port_bitmap, .link_detection = RMAC_LINK_DETECTION_DEFAULT_PORTS_UP, };
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer0[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer1[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer2[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer3[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer4[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer5[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer6[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer7[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer8[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer9[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer10[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer11[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer12[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer13[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer14[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer15[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer16[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer17[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer18[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer19[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer20[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer21[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer22[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer23[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer24[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer25[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer26[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer27[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer28[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer29[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer30[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer31[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer32[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer33[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer34[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer35[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer36[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer37[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer38[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer39[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer40[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer41[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer42[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer43[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer44[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer45[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer46[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer47[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer48[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer49[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer50[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer51[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer52[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer53[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer54[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer55[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer56[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer57[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer58[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer59[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer60[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer61[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer62[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer63[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer64[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer65[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer66[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer67[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer68[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer69[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer70[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer71[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer72[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer73[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer74[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer75[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer76[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer77[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer78[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer79[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer80[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer81[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer82[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer83[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer84[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer85[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer86[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer87[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer88[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer89[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer90[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer91[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer92[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer93[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer94[1536];
__attribute__((section(".ram_nocache"), aligned(32))) uint8_t g_ether0_ether_buffer95[1536];

uint8_t *pp_g_ether0_ether_buffers[96] =
{ (uint8_t*) &g_ether0_ether_buffer0[0],
  (uint8_t*) &g_ether0_ether_buffer1[0],
  (uint8_t*) &g_ether0_ether_buffer2[0],
  (uint8_t*) &g_ether0_ether_buffer3[0],
  (uint8_t*) &g_ether0_ether_buffer4[0],
  (uint8_t*) &g_ether0_ether_buffer5[0],
  (uint8_t*) &g_ether0_ether_buffer6[0],
  (uint8_t*) &g_ether0_ether_buffer7[0],
  (uint8_t*) &g_ether0_ether_buffer8[0],
  (uint8_t*) &g_ether0_ether_buffer9[0],
  (uint8_t*) &g_ether0_ether_buffer10[0],
  (uint8_t*) &g_ether0_ether_buffer11[0],
  (uint8_t*) &g_ether0_ether_buffer12[0],
  (uint8_t*) &g_ether0_ether_buffer13[0],
  (uint8_t*) &g_ether0_ether_buffer14[0],
  (uint8_t*) &g_ether0_ether_buffer15[0],
  (uint8_t*) &g_ether0_ether_buffer16[0],
  (uint8_t*) &g_ether0_ether_buffer17[0],
  (uint8_t*) &g_ether0_ether_buffer18[0],
  (uint8_t*) &g_ether0_ether_buffer19[0],
  (uint8_t*) &g_ether0_ether_buffer20[0],
  (uint8_t*) &g_ether0_ether_buffer21[0],
  (uint8_t*) &g_ether0_ether_buffer22[0],
  (uint8_t*) &g_ether0_ether_buffer23[0],
  (uint8_t*) &g_ether0_ether_buffer24[0],
  (uint8_t*) &g_ether0_ether_buffer25[0],
  (uint8_t*) &g_ether0_ether_buffer26[0],
  (uint8_t*) &g_ether0_ether_buffer27[0],
  (uint8_t*) &g_ether0_ether_buffer28[0],
  (uint8_t*) &g_ether0_ether_buffer29[0],
  (uint8_t*) &g_ether0_ether_buffer30[0],
  (uint8_t*) &g_ether0_ether_buffer31[0],
  (uint8_t*) &g_ether0_ether_buffer32[0],
  (uint8_t*) &g_ether0_ether_buffer33[0],
  (uint8_t*) &g_ether0_ether_buffer34[0],
  (uint8_t*) &g_ether0_ether_buffer35[0],
  (uint8_t*) &g_ether0_ether_buffer36[0],
  (uint8_t*) &g_ether0_ether_buffer37[0],
  (uint8_t*) &g_ether0_ether_buffer38[0],
  (uint8_t*) &g_ether0_ether_buffer39[0],
  (uint8_t*) &g_ether0_ether_buffer40[0],
  (uint8_t*) &g_ether0_ether_buffer41[0],
  (uint8_t*) &g_ether0_ether_buffer42[0],
  (uint8_t*) &g_ether0_ether_buffer43[0],
  (uint8_t*) &g_ether0_ether_buffer44[0],
  (uint8_t*) &g_ether0_ether_buffer45[0],
  (uint8_t*) &g_ether0_ether_buffer46[0],
  (uint8_t*) &g_ether0_ether_buffer47[0],
  (uint8_t*) &g_ether0_ether_buffer48[0],
  (uint8_t*) &g_ether0_ether_buffer49[0],
  (uint8_t*) &g_ether0_ether_buffer50[0],
  (uint8_t*) &g_ether0_ether_buffer51[0],
  (uint8_t*) &g_ether0_ether_buffer52[0],
  (uint8_t*) &g_ether0_ether_buffer53[0],
  (uint8_t*) &g_ether0_ether_buffer54[0],
  (uint8_t*) &g_ether0_ether_buffer55[0],
  (uint8_t*) &g_ether0_ether_buffer56[0],
  (uint8_t*) &g_ether0_ether_buffer57[0],
  (uint8_t*) &g_ether0_ether_buffer58[0],
  (uint8_t*) &g_ether0_ether_buffer59[0],
  (uint8_t*) &g_ether0_ether_buffer60[0],
  (uint8_t*) &g_ether0_ether_buffer61[0],
  (uint8_t*) &g_ether0_ether_buffer62[0],
  (uint8_t*) &g_ether0_ether_buffer63[0],
  (uint8_t*) &g_ether0_ether_buffer64[0],
  (uint8_t*) &g_ether0_ether_buffer65[0],
  (uint8_t*) &g_ether0_ether_buffer66[0],
  (uint8_t*) &g_ether0_ether_buffer67[0],
  (uint8_t*) &g_ether0_ether_buffer68[0],
  (uint8_t*) &g_ether0_ether_buffer69[0],
  (uint8_t*) &g_ether0_ether_buffer70[0],
  (uint8_t*) &g_ether0_ether_buffer71[0],
  (uint8_t*) &g_ether0_ether_buffer72[0],
  (uint8_t*) &g_ether0_ether_buffer73[0],
  (uint8_t*) &g_ether0_ether_buffer74[0],
  (uint8_t*) &g_ether0_ether_buffer75[0],
  (uint8_t*) &g_ether0_ether_buffer76[0],
  (uint8_t*) &g_ether0_ether_buffer77[0],
  (uint8_t*) &g_ether0_ether_buffer78[0],
  (uint8_t*) &g_ether0_ether_buffer79[0],
  (uint8_t*) &g_ether0_ether_buffer80[0],
  (uint8_t*) &g_ether0_ether_buffer81[0],
  (uint8_t*) &g_ether0_ether_buffer82[0],
  (uint8_t*) &g_ether0_ether_buffer83[0],
  (uint8_t*) &g_ether0_ether_buffer84[0],
  (uint8_t*) &g_ether0_ether_buffer85[0],
  (uint8_t*) &g_ether0_ether_buffer86[0],
  (uint8_t*) &g_ether0_ether_buffer87[0],
  (uint8_t*) &g_ether0_ether_buffer88[0],
  (uint8_t*) &g_ether0_ether_buffer89[0],
  (uint8_t*) &g_ether0_ether_buffer90[0],
  (uint8_t*) &g_ether0_ether_buffer91[0],
  (uint8_t*) &g_ether0_ether_buffer92[0],
  (uint8_t*) &g_ether0_ether_buffer93[0],
  (uint8_t*) &g_ether0_ether_buffer94[0],
  (uint8_t*) &g_ether0_ether_buffer95[0], };
const ether_cfg_t g_ether0_cfg =
{ .channel = 1, .zerocopy = ETHER_ZEROCOPY_ENABLE, .multicast = ETHER_MULTICAST_ENABLE, .promiscuous =
          ETHER_PROMISCUOUS_DISABLE,
  .flow_control = ETHER_FLOW_CONTROL_ENABLE, .padding = ETHER_PADDING_DISABLE, .padding_offset = 0, .broadcast_filter =
          0,
  .p_mac_address = g_ether0_mac_address,

  .num_tx_descriptors = 32,
  .num_rx_descriptors = 96,

  .pp_ether_buffers = pp_g_ether0_ether_buffers,

  .ether_buffer_size = 1536,

  .irq = FSP_INVALID_VECTOR,

  .p_callback = NULL,
  .p_context = NULL, .p_extend = &g_ether0_extended_cfg_t, };

/* Instance structure to use this module. */
const ether_instance_t g_ether0 =
{ .p_ctrl = &g_ether0_ctrl, .p_cfg = &g_ether0_cfg, .p_api = &g_ether_on_rmac, };
