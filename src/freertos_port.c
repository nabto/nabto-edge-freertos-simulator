#include "FreeRTOSConfig.h"
#include "FreeRTOS.h"
#include "task.h"

#include <stdint.h>

#include <time.h>

uint8_t ucHeap[configTOTAL_HEAP_SIZE];


void vApplicationIdleHook()
{
    taskENTER_CRITICAL();
    struct timespec req;
    req.tv_nsec = 1000000; // 1ms
    req.tv_sec = 0;
    nanosleep(&req, NULL);
    taskEXIT_CRITICAL();
}


void vAssertCalled(const char *const pcFileName,
                   unsigned long ulLine)
{
    (void)ulLine;
    (void)pcFileName;

    volatile uint32_t ulSetToNonZeroInDebuggerToContinue = 0;

    taskENTER_CRITICAL();
    {
        while (ulSetToNonZeroInDebuggerToContinue == 0)
        {
            __asm volatile ( "NOP" );
            __asm volatile ( "NOP" );
        }
    }
    taskEXIT_CRITICAL();
}
