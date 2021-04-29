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
#include "netif/ethernet.h"

// Nabto includes
#include <nabto/nabto_device.h>
#include <nabto/nabto_device_test.h>
#include <platform/interfaces/np_dns.h>
#include <platform/np_types.h>
#include <platform/np_error_code.h>
#include <platform/np_completion_event.h>

// Project includes
#include "common.h"
#include "default_netif.h"
#include "lwipcfg.h"
#include "console.h"

#ifndef USE_ETHERNET
#define USE_ETHERNET (USE_DEFAULT_ETH_NETIF || PPPOE_SUPPORT)
#endif

StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];
uint8_t ucHeap[configTOTAL_HEAP_SIZE];

static struct
{
    TaskHandle_t main_loop;
} globals;

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
    dns_test();
    vTaskDelete(NULL);
}

static void LWIPStatusCallback(struct netif *state_netif)
{
  if (netif_is_up(state_netif)) {
    console_print("status_callback==UP, local interface IP is %s\n",
           ip4addr_ntoa(netif_ip4_addr(state_netif)));
  } else {
    console_print("status_callback==DOWN\n");
  }
}

typedef struct
{
    size_t ips_size;
    size_t *ips_resolved;
    struct np_ip_address *ips;
    struct np_completion_event *completion_event;
    int addr_type;
} DNSResolveEvent;

static void DNSResolveCallback(const char *name, const ip_addr_t *addr, void *arg)
{
    console_print("DNS Resolve: %s --- %s\n", name, ipaddr_ntoa(addr));
    DNSResolveEvent *event = (DNSResolveEvent*)arg;
    if (addr && addr->type == event->addr_type && event->ips_size >= 1)
    {
        *event->ips_resolved = 1;

        if (event->addr_type == IPADDR_TYPE_V4)
        {
            event->ips[0].type = NABTO_IPV4;
            memcpy(event->ips[0].ip.v4,
                   &addr->u_addr.ip4.addr,
                   sizeof(event->ips[0].ip.v4));
        }
        else
        {
            event->ips[0].type = NABTO_IPV6;
            memcpy(event->ips[0].ip.v6,
                   addr->u_addr.ip6.addr,
                   sizeof(event->ips[0].ip.v6));
        }

        np_completion_event_resolve(event->completion_event, NABTO_EC_OK);
    }
    else
    {
        np_completion_event_resolve(event->completion_event, NABTO_EC_UNKNOWN);
    }
}

void AsyncResolve(struct np_dns *obj, const char *host,
                  struct np_ip_address *ips,
                  size_t ips_size, size_t *ips_resolved,
                  struct np_completion_event *completion_event,
                  int addr_type)
{
    UNUSED(obj);
    DNSResolveEvent *event = pvPortMalloc(sizeof *event);
    event->ips_size = ips_size;
    event->ips_resolved = ips_resolved;
    event->ips = ips;
    event->completion_event = completion_event;
    event->addr_type = addr_type;
    u8_t dns_addrtype = addr_type == IPADDR_TYPE_V4 ? LWIP_DNS_ADDRTYPE_IPV4 : LWIP_DNS_ADDRTYPE_IPV6;

    sys_lock_tcpip_core();
    struct ip_addr resolved;
    err_t Error = dns_gethostbyname_addrtype(host, &resolved,
                                             DNSResolveCallback, event,
                                             dns_addrtype);
    sys_unlock_tcpip_core();

    switch (Error)
    {
        case ERR_OK:
        case ERR_INPROGRESS:
        {
            console_print("DNS resolve for %s\n", host);
        } break;

        default:
        {
            console_print("Failed to send DNS request for %s\n", host);
            np_completion_event_resolve(completion_event, NABTO_EC_UNKNOWN);
            return;
        }
    }
}

void AsyncResolveIPv4(struct np_dns *obj, const char *host,
                      struct np_ip_address *ips,
                      size_t ips_size, size_t *ips_resolved,
                      struct np_completion_event *completion_event)
{
    AsyncResolve(obj, host, ips, ips_size, ips_resolved, completion_event, IPADDR_TYPE_V4);
}

void AsyncResolveIPv6(struct np_dns *obj, const char *host,
                      struct np_ip_address *ips,
                      size_t ips_size, size_t *ips_resolved,
                      struct np_completion_event *completion_event)
{
    AsyncResolve(obj, host, ips, ips_size, ips_resolved, completion_event, IPADDR_TYPE_V6);
}

static void LWIPInit(void * arg)
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

  console_print("Starting lwIP, local interface IP is %s\n", ip4addr_ntoa(&ipaddr));

  init_default_netif(&ipaddr, &netmask, &gw);
  netif_set_status_callback(netif_default, LWIPStatusCallback);

  netif_set_up(netif_default);

  // @TODO: Using google dns for now, should probably be exposed as an option instead.
  ip_addr_t dnsserver;
  IP_ADDR4(&dnsserver, 8,8,8,8);
  dns_setserver(0, &dnsserver);
  IP_ADDR4(&dnsserver, 8,8,4,4);
  dns_setserver(1, &dnsserver);

  sys_sem_signal(init_sem);
}

void MainInit(void)
{
    console_init();
}

void LWIPLoop(void *arg)
{
    UNUSED(arg);
    err_t err;
    sys_sem_t init_sem;

    err = sys_sem_new(&init_sem, 0);
    LWIP_ASSERT("failed to create init_sem", err == ERR_OK);
    LWIP_UNUSED_ARG(err);
    tcpip_init(LWIPInit, &init_sem);
    sys_sem_wait(&init_sem);
    sys_sem_free(&init_sem);

    netconn_thread_init();

    // Let the main thread know LWIP has initialized.
    xTaskNotifyGive(xTaskGetHandle("MainLoop"));

    for(;;) {
      default_netif_poll();
    }

    netconn_thread_cleanup();
    default_netif_shutdown();
    vTaskDelete(NULL);
}

void MainLoop(void *arg)
{
    UNUSED(arg);
    console_init();

    xTaskCreate(LWIPLoop, "LWIP",
                configMINIMAL_STACK_SIZE, NULL,
                configMAX_PRIORITIES-1, NULL);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    xTaskCreate(TestNabtoTask, "NabtoTest",
                configMINIMAL_STACK_SIZE, NULL,
                configMAX_PRIORITIES-1, NULL);

    for (;;)
    {
        vTaskDelay(1000);
    }
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

