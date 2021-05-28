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
#include "lwip/dns.h"
#include "lwip/udp.h"
#include "lwip/dhcp.h"
#include "lwip/apps/mdns.h"
#include "netif/ethernet.h"

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
#include "default_netif.h"
#include "lwipcfg.h"
#include "console.h"
#include "coap.h"

#include <time.h>

StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];
uint8_t ucHeap[configTOTAL_HEAP_SIZE];



static struct
{
    TaskHandle_t main_loop;
} globals;

void vApplicationIdleHook()
{
    taskENTER_CRITICAL();
    struct timespec req;
    req.tv_nsec = 1000000; // 1ms
    req.tv_sec = 0;
    nanosleep(&req, NULL);
    taskEXIT_CRITICAL();
}

static void LWIPStatusCallback(struct netif *state_netif)
{
    if (netif_is_up(state_netif))
    {
        console_print("status_callback==UP, local interface IP is %s\n",
                      ip4addr_ntoa(netif_ip4_addr(state_netif)));
    }
    else
    {
        console_print("status_callback==DOWN\n");
    }
}

// static void
// lwip_mdns_report(struct netif* netif, u8_t result)
// {
//   LWIP_PLATFORM_DIAG(("mdns status[netif %d]: %d\n", netif->num, result));
// }

static void LWIPInit(void *arg)
{
    sys_sem_t *init_sem;
    init_sem = (sys_sem_t*)arg;

    srand((unsigned int)time(0));

    ip4_addr_t ipaddr, netmask, gw;

    // @TODO: Allow using DHCP to get an address instead?
    ip4_addr_set_zero(&gw);
    ip4_addr_set_zero(&ipaddr);
    ip4_addr_set_zero(&netmask);
    LWIP_PORT_INIT_GW(&gw);
    LWIP_PORT_INIT_IPADDR(&ipaddr);
    LWIP_PORT_INIT_NETMASK(&netmask);


    init_default_netif(&ipaddr, &netmask, &gw);
    netif_create_ip6_linklocal_address(netif_default, 1);



    console_print("Starting lwIP, local interface IP is %s\n", ip4addr_ntoa(&ipaddr));
    console_print("ip6 linklocal address: %s\n", ip6addr_ntoa(netif_ip6_addr(netif_default, 0)));

    netif_set_status_callback(netif_default, LWIPStatusCallback);
    netif_set_up(netif_default);
    //dhcp_start(netif_default);

    // @TODO: Using google dns for now, should probably be exposed as an option instead.
    ip_addr_t dnsserver;
    IP_ADDR4(&dnsserver, 8,8,8,8);
    dns_setserver(0, &dnsserver);
    IP_ADDR4(&dnsserver, 8,8,4,4);
    dns_setserver(1, &dnsserver);

    //mdns_resp_register_name_result_cb(lwip_mdns_report);
    //mdns_resp_init();
    //mdns_resp_add_netif(netif_default, "lwip", 3600);
    //mdns_resp_announce(netif_default);

    sys_sem_signal(init_sem);
}

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

    {
        sys_sem_t init_sem;
        sys_sem_new(&init_sem, 0);
        tcpip_init(LWIPInit, &init_sem);
        sys_sem_wait(&init_sem);
        sys_sem_free(&init_sem);
    }

    xTaskCreate(NabtoMain, "NabtoMain",
                configMINIMAL_STACK_SIZE, NULL,
                configMAX_PRIORITIES-1, NULL);

    // for (;;)
    // {
    //      struct timespec req;
    //      req.tv_nsec = 0;
    //      req.tv_sec = 1;
    //      nanosleep(&req, NULL);
    //     //vTaskDelay(1000);
    // }
    vTaskDelete(NULL);
}

int main(void)
{
    xTaskCreate(MainLoop, "MainLoop",
                configMINIMAL_STACK_SIZE, NULL,
                configMAX_PRIORITIES-1, &globals.main_loop);

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
