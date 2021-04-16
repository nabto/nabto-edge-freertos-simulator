#include <stdio.h>
#include <time.h>

// FreeRTOS includes
#include <FreeRTOS.h>
#include <task.h>

// lwIP includes
#include "lwip/sys.h"
#include "lwip/timeouts.h"
#include "lwip/debug.h"
#include "lwip/stats.h"
#include "lwip/init.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/api.h"
#include "lwip/etharp.h"
#include "netif/ethernet.h"

// Nabto includes
#include <nabto/nabto_device.h>
#include <nabto/nabto_device_test.h>

// Project includes
#include "common.h"
#include "console.h"

StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];
uint8_t ucHeap[configTOTAL_HEAP_SIZE];

static void log_callback(NabtoDeviceLogMessage *msg, void *data)
{
    UNUSED(data);

    console_print("%5s: %s\n",
                  nabto_device_log_severity_as_string(msg->severity),
                  msg->message);
}

// Tests moved to another file since they're temporary.
#include "tests.inl"

void TestNabtoTask(void *parameters)
{
    UNUSED(parameters);

    console_print("FreeRTOS Version %s\n", tskKERNEL_VERSION_NUMBER);
    thread_test();
    create_device_test();
    future_test();
    logging_test();
    timestamp_test();
    event_queue_test();
    vTaskDelete(NULL);
}

void nabto_lwip_init(void *arg)
{
    sys_sem_t *init_sem = (sys_sem_t*)arg;

    sys_sem_signal(init_sem);
}

int main(void)
{
    console_init();

    sys_sem_t init_sem;
    sys_sem_new(&init_sem, 0);
    tcpip_init(nabto_lwip_init, &init_sem);

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

