#include "FreeRTOS.h"
#include "cmsis_gcc.h"
#include "task.h"

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    __disable_irq();

    __BKPT(0);

    while(1)
    {

    }
}
