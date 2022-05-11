#include "FreeRTOS.h"
#include "task.h"

#include <platform/interfaces/np_timestamp.h>
#include <api/nabto_device_platform.h>
#include <api/nabto_device_integration.h>
#include <modules/event_queue/thread_event_queue.h>
#include <modules/mdns/nm_mdns_server.h>

#include "common.h"
#include "nabto_lwip/nm_nabto_lwip.h"
#include "nabto_mdns_lwip/nm_mdns_lwip.h"
#include "default_netif.h"


struct platform_data
{
    struct thread_event_queue event_queue;
    struct nm_mdns_lwip mdnsServer;
};

static uint32_t freertos_now_ms(struct np_timestamp *obj);

static struct np_timestamp_functions timestamp_module = {
    .now_ms = &freertos_now_ms
};

np_error_code nabto_device_platform_init(struct nabto_device_context *device,
                                         struct nabto_device_mutex *mutex)
{
    // @TODO: Clear the allocated memory?
    struct platform_data *platform = pvPortMalloc(sizeof(*platform));

    struct np_timestamp ts;
    ts.mptr = &timestamp_module;
    ts.data = NULL;

    struct np_dns dns = nm_lwip_get_dns_impl();
    struct np_udp udp = nm_lwip_get_udp_impl();
    struct np_tcp tcp = nm_lwip_get_tcp_impl();
    struct np_local_ip localip = nm_lwip_get_local_ip_impl(get_default_netif());

    thread_event_queue_init(&platform->event_queue, mutex, &ts);
    thread_event_queue_run(&platform->event_queue);

    struct np_event_queue event_queue_impl = thread_event_queue_get_impl(&platform->event_queue);

    // Create a mdns server
    // the mdns server requires special udp bind functions.
    np_error_code errr = nm_mdns_lwip_init(&platform->mdnsServer, &event_queue_impl, &localip);

    struct netif* defaultNetif = get_default_netif();
    nm_mdns_lwip_add_netif(&platform->mdnsServer, defaultNetif);

    struct np_mdns mdnsImpl = nm_mdns_lwip_get_impl(&platform->mdnsServer);

    nabto_device_integration_set_timestamp_impl(device, &ts);
    nabto_device_integration_set_event_queue_impl(device, &event_queue_impl);
    nabto_device_integration_set_dns_impl(device, &dns);
    nabto_device_integration_set_udp_impl(device, &udp);
    nabto_device_integration_set_tcp_impl(device, &tcp);
    nabto_device_integration_set_local_ip_impl(device, &localip);
    nabto_device_integration_set_mdns_impl(device, &mdnsImpl);

    nabto_device_integration_set_platform_data(device, platform);

    return NABTO_EC_OK;
}

void nabto_device_platform_deinit(struct nabto_device_context *device)
{
    struct platform_data *platform = nabto_device_integration_get_platform_data(device);
    nm_mdns_lwip_deinit(&platform->mdnsServer);
    thread_event_queue_deinit(&platform->event_queue);
}

void nabto_device_platform_stop_blocking(struct nabto_device_context *device)
{
    struct platform_data *platform = nabto_device_integration_get_platform_data(device);
    thread_event_queue_stop_blocking(&platform->event_queue);
}

uint32_t freertos_now_ms(struct np_timestamp *obj)
{
    UNUSED(obj);

    TickType_t tick_count = xTaskGetTickCount();
    return tick_count / portTICK_PERIOD_MS;
}
