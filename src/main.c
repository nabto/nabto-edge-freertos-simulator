#include <stdio.h>

// FreeRTOS includes
#include <FreeRTOS.h>
#include <task.h>

// Nabto includes
#include <nabto/nabto_device.h>
#include <nabto/nabto_device_test.h>

// Project includes
#include "console.h"

StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];

void vTestTaskFunction(void *Parameters)
{
    for (;;)
    {
        console_print("FreeRTOS Version %s\n", tskKERNEL_VERSION_NUMBER);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL);
}

void TestNabtoTask(void *parameters)
{
    NabtoDeviceError ec = nabto_device_test_threads();
    if (ec == NABTO_DEVICE_EC_OK)
    {
        console_print("Threads test passed\n");
    }
    else
    {
        console_print("Threads test failed\n");
    }
    vTaskDelete(NULL);

}

int main(void)
{
    console_init();
    xTaskCreate(TestNabtoTask, "test",
                configMINIMAL_STACK_SIZE, NULL,
                configMAX_PRIORITIES-1, NULL);
    vTaskStartScheduler();
    return 0;
}

void vAssertCalled(const char *const pcFileName,
                   unsigned long ulLine)
{
    (void)ulLine;
    (void)pcFileName;

    static BaseType_t xPrinted = pdFALSE;
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

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize)
{
    static StaticTask_t xIdleTaskTCB;
    static StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE];

    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t **ppxTimerTaskStackBuffer,
                                    uint32_t *pulTimerTaskStackSize)
{
    static StaticTask_t xTimerTaskTCB;

    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}

