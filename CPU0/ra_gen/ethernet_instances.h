/* Additive CPU0 Ethernet generated declarations.
 * Keep these C-only FSP instance types out of the AI/NPU C++ include graph. */
#ifndef CPU0_ETHERNET_INSTANCES_H_
#define CPU0_ETHERNET_INSTANCES_H_

#include "hal_data.h"
#include "r_rmac.h"
#include "r_rmac_phy.h"
#include "r_layer3_switch.h"

extern rmac_instance_ctrl_t g_ether0_ctrl;
extern const ether_cfg_t g_ether0_cfg;
extern const ether_phy_instance_t g_rmac_phy0;
extern rmac_phy_instance_ctrl_t g_rmac_phy0_ctrl;
extern const ether_phy_cfg_t g_rmac_phy0_cfg;
extern const rmac_phy_extended_cfg_t g_rmac_phy0_extended_cfg;
extern const ether_switch_instance_t g_layer3_switch0;
extern layer3_switch_instance_ctrl_t g_layer3_switch0_ctrl;
extern const ether_switch_cfg_t g_layer3_switch0_cfg;

#endif /* CPU0_ETHERNET_INSTANCES_H_ */
