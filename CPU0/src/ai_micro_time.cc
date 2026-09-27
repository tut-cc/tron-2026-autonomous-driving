/* TensorFlow Lite Micro timing hooks backed by the Cortex-M85 cycle counter. */

#include "hal_data.h"
#include "tensorflow/lite/micro/micro_time.h"

namespace tflite
{
uint32_t ticks_per_second()
{
    return SystemCoreClock;
}

uint32_t GetCurrentTimeTicks()
{
    return DWT->CYCCNT;
}
} /* namespace tflite */

extern "C" void _exit(int status) __attribute__((noreturn));

extern "C" void _exit(int status)
{
    FSP_PARAMETER_NOT_USED(status);
    __disable_irq();
    while (true)
    {
        __WFI();
    }
}
