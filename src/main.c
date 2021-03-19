#include <stdio.h>
#include <time.h>

// FreeRTOS includes
#include <FreeRTOS.h>
#include <task.h>
#include <FreeRTOS_IP.h>
#include <FreeRTOS_Sockets.h>

// Nabto includes
#include <nabto/nabto_device.h>
#include <nabto/nabto_device_test.h>

// Project includes
#include "common.h"
#include "console.h"

StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];
uint8_t ucHeap[configTOTAL_HEAP_SIZE];

static const uint8_t ucIPAddress[]        = { 10,  10,  10, 200};
static const uint8_t ucNetMask[]          = {255,   0,   0,   0};
static const uint8_t ucGatewayAddress[]   = { 10,  10,  10,   1};
static const uint8_t ucDNSServerAddress[] = {208,  67, 222, 222};

const uint8_t ucMACAddress[] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 };
static UBaseType_t next_rand;

static void log_callback(NabtoDeviceLogMessage *msg, void *data)
{
    UNUSED(data);

    console_print("%5s: %s\n",
                  nabto_device_log_severity_as_string(msg->severity),
                  msg->message);
}

// @TODO: Better RNG algorithm.
UBaseType_t uxRand(void)
{
    uint32_t mul = 0x015a4e35UL;
    uint32_t inc = 1UL;

    next_rand = (mul * next_rand) + inc;
    return (int)(next_rand >> 16UL) & 0x7fffUL;
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

int main(void)
{
    // @TODO: Initialization should be done in a task?

    // @TODO: Random seeding uses C time.h
    // It should probably be platform-defined?
    time_t now;
    time(&now);
    next_rand = (uint32_t)now;

    console_init();
    xTaskCreate(TestNabtoTask, "test",
                configMINIMAL_STACK_SIZE, NULL,
                configMAX_PRIORITIES-1, NULL);

    FreeRTOS_IPInit(ucIPAddress,
                    ucNetMask,
                    ucGatewayAddress,
                    ucDNSServerAddress,
                    ucMACAddress);
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

void vApplicationIPNetworkEventHook(eIPCallbackEvent_t network_event)
{
    uint32_t ip_address;
    uint32_t net_mask;
    uint32_t gateway_address;
    uint32_t dns_server_address;
    char buffer[16] = {0};

    if (network_event == eNetworkUp)
    {
        FreeRTOS_GetAddressConfiguration(&ip_address, &net_mask, &gateway_address, &dns_server_address);
        FreeRTOS_inet_ntoa(ip_address, buffer);
        console_print("\r\n\r\nIP Address: %s\r\n", buffer);

        FreeRTOS_inet_ntoa(net_mask, buffer);
        console_print("Subnet Mask: %s\r\n", buffer);

        FreeRTOS_inet_ntoa(gateway_address, buffer);
        console_print("Gateway Address: %s\r\n", buffer);

        FreeRTOS_inet_ntoa(dns_server_address, buffer);
        console_print("DNS Server Address: %s\r\n\r\n\r\n", buffer);
    }
    else
    {
        console_print("Application idle hook network down\n");
    }
}

const char* pcApplicationHostnameHook(void)
{
    return "FreeRTOS";
}

BaseType_t xApplicationDNSQueryHook(const char *name)
{
    BaseType_t result = pdPASS;
    if (strcasecmp(name, pcApplicationHostnameHook()) != 0)
    {
        result = pdFAIL;
    }
    return result;
}

// @TODO: The following two functions are dummy implementations
// that just return random numbers.
uint32_t ulApplicationGetNextSequenceNumber(uint32_t source_address,
                                            uint16_t source_port,
                                            uint32_t destination_address,
                                            uint16_t destination_port)
{
    UNUSED(source_address);
    UNUSED(source_port);
    UNUSED(destination_address);
    UNUSED(destination_port);

    return uxRand();
}

// Supplies a random number to FreeRTOS+TCP stack.
BaseType_t xApplicationGetRandomNumber(uint32_t *number)
{
    *number = uxRand();
    return pdTRUE;
}

