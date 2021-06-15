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

static const char* productId;
static const char* deviceId;

void NabtoMain(void *arg)
{
    UNUSED(arg);
    nabto_coap(productId, deviceId);
    vTaskDelete(NULL);
}

int main(int argc, const char* argv[])
{
    if (argc != 3) {
        printf("usage %s <productid> <deviceid>\n", argv[0]);
        return 1;
    }

    productId = argv[1];
    deviceId = argv[2];

    // init FreeRTOS and LwIP
    console_init();
    lwip_port_init();

    // Create the nabto coap task.
    xTaskCreate(NabtoMain, "NabtoMain",
                configMINIMAL_STACK_SIZE, NULL,
                configMAX_PRIORITIES-1, NULL);

    // Run the freertos scheduler.
    vTaskStartScheduler();
    return 0;
}
