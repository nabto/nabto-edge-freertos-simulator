#include <FreeRTOS.h>
#include <task.h>

#include <platform/interfaces/np_timestamp.h>
#include <api/nabto_device_platform.h>
#include <api/nabto_device_integration.h>
#include <modules/event_queue/thread_event_queue.h>

#include "common.h"
#include "nabto_lwip.h"

struct platform_data
{
    struct thread_event_queue event_queue;
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

    struct np_dns dns = nplwip_get_dns_impl();
    struct np_udp udp = nplwip_get_udp_impl();
    struct np_tcp tcp = nplwip_get_tcp_impl();
    struct np_local_ip localip = nplwip_get_local_ip_impl();
    struct np_mdns mdns = nplwip_get_mdns_impl();

    thread_event_queue_init(&platform->event_queue, mutex, &ts);
    thread_event_queue_run(&platform->event_queue);

    struct np_event_queue event_queue_impl = thread_event_queue_get_impl(&platform->event_queue);

    nabto_device_integration_set_timestamp_impl(device, &ts);
    nabto_device_integration_set_event_queue_impl(device, &event_queue_impl);
    nabto_device_integration_set_dns_impl(device, &dns);
    nabto_device_integration_set_udp_impl(device, &udp);
    nabto_device_integration_set_tcp_impl(device, &tcp);
    nabto_device_integration_set_local_ip_impl(device, &localip);
    nabto_device_integration_set_mdns_impl(device, &mdns);

    nabto_device_integration_set_platform_data(device, platform);

    return NABTO_EC_OK;
}

void nabto_device_platform_deinit(struct nabto_device_context *device)
{
    struct platform_data *platform = nabto_device_integration_get_platform_data(device);
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

