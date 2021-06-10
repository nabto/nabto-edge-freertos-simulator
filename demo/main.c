#include <stdio.h>
#include <time.h>

// FreeRTOS includes
#include <FreeRTOS.h>
#include <task.h>

// Nabto includes
#include <nabto/nabto_device.h>
#include <nabto/nabto_device_test.h>
#include <platform/interfaces/np_dns.h>
#include <platform/interfaces/np_udp.h>
#include <platform/np_types.h>
#include <platform/np_error_code.h>
#include <platform/np_completion_event.h>
#include <platform/np_logging.h>

// Project includes
#include "common.h"
#include "lwipcfg.h"
#include "console.h"
#include "coap.h"
#include "lwip_port_init.h"

#include <time.h>

void NabtoMain(void *arg)
{
    UNUSED(arg);
    nabto_coap();
    vTaskDelete(NULL);
}

void MainLoop(void *arg)
{
    UNUSED(arg);
    console_init();

    lwip_port_init();

    xTaskCreate(NabtoMain, "NabtoMain",
                configMINIMAL_STACK_SIZE, NULL,
                configMAX_PRIORITIES-1, NULL);

    vTaskDelete(NULL);
}

int main(void)
{
    xTaskCreate(MainLoop, "MainLoop",
                configMINIMAL_STACK_SIZE, NULL,
                configMAX_PRIORITIES-1, NULL);

    vTaskStartScheduler();
    return 0;
}
