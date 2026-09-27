#ifndef BUS_OWNER_H
#define BUS_OWNER_H
#include "control_runtime.h"
int bus_owner_start(void);
int bus_tof_poll(tof_safety_result_t *sample,uint32_t now);
#endif
