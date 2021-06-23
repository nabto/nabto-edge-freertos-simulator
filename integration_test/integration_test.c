#include <FreeRTOS.h>
#include <task.h>

#include "console.h"
#include "lwip_port_init.h"

#include <nabto/nabto_device.h>
#include <nabto/nabto_device_test.h>

#include <stdio.h>
#include <stdlib.h>

#include "udpecho_raw.h"
#include "tcpecho_raw.h"

#define NEWLINE "\n"

static int integrationTestTask();
static void nabtoTask(void *arg);

int main(int argc, char** argv) {

    // init FreeRTOS and LwIP
    console_init();
    lwip_port_init();

    // Create the nabto coap task.
    xTaskCreate(nabtoTask, "NabtoMain",
                configMINIMAL_STACK_SIZE, NULL,
                configMAX_PRIORITIES-1, NULL);

    // Run the freertos scheduler.
    vTaskStartScheduler();
    return 0;
}

static void nabtoTask(void *arg) {
    integrationTestTask();
    vTaskDelete(NULL);
}

void create_device_test()
{
    // instead of calling nabto_device_new, we call
    // nabto_device_test_new to get a test instance
    NabtoDevice* device = nabto_device_test_new();
    if (device != NULL) {
        printf("Create device test passed\n");
    } else {
        printf("Create device test failed, could not create device.\n");
    }
    nabto_device_test_free(device);
}

void future_test()
{
    NabtoDevice* device = nabto_device_test_new();
    if (device == NULL) {
        printf("Test failed, device is NULL\n");
        return;
    }

    NabtoDeviceFuture* future = nabto_device_future_new(device);
    if (future == NULL) {
        printf("Test failed future is NULL\n");
        return;
    }

    nabto_device_test_future_resolve(device, future);

    // This call blocks until the future is resolved. If the test is
    // not blocking forever and returning NABTO_DEVICE_EC_OK, then the
    // test has passed.
    NabtoDeviceError ec = nabto_device_future_wait(future);

    if (ec == NABTO_DEVICE_EC_OK) {
        printf("Future resolve test has passed\n");
    } else {
        printf("Future resolve test has failed\n");
    }

    nabto_device_test_free(device);
}

void event_queue_test()
{
    // instead of calling nabto_device_new, we call
    // nabto_device_test_new to get a test instance with limited
    // functionality.
    NabtoDevice* device = nabto_device_test_new();

    NabtoDeviceFuture* future = nabto_device_future_new(device);

    // Run the event queue test. The test passes if the future
    // resolves and the status of the future is NABTO_DEVICE_EC_OK
    nabto_device_test_event_queue(device, future);

    NabtoDeviceError ec = nabto_device_future_wait(future);
    if (ec == NABTO_DEVICE_EC_OK) {
        printf("Event queue test has passed\n");
    } else {
        printf("Event queue test has failed\n");
    }

    nabto_device_future_free(future);
    nabto_device_test_free(device);
}

void dns_test()
{
    // instead of calling nabto_device_new, we call
    // nabto_device_test_new to get a test instance with limited
    // functionality.
    NabtoDevice* device = nabto_device_test_new();

    NabtoDeviceFuture* future = nabto_device_future_new(device);

    // Run the DNS test. The test passes if the future resolves and
    // the status of the future is NABTO_DEVICE_EC_OK
    nabto_device_test_dns(device, future);

    NabtoDeviceError ec = nabto_device_future_wait(future);
    if (ec == NABTO_DEVICE_EC_OK) {
        printf("DNS test has passed\n");
    } else {
        printf("DNS test has failed\n");
    }

    nabto_device_future_free(future);
    nabto_device_test_free(device);
}


void udp_test(const char* testServerHost, uint16_t testServerPort)
{
    NabtoDevice* device = nabto_device_test_new();
    NabtoDeviceFuture* future = nabto_device_future_new(device);

    // Run the UDP test. The test passes if the future resolves and
    // the status of the future is NABTO_DEVICE_EC_OK
    nabto_device_test_udp(device, testServerHost, testServerPort, future);

    NabtoDeviceError ec = nabto_device_future_wait(future);
    if (ec == NABTO_DEVICE_EC_OK) {
        printf("UDP test has passed\n");
    } else {
        printf("UDP test has failed\n");
    }

    nabto_device_future_free(future);
    nabto_device_test_free(device);

    // lwip_udp_echo_server_stop(&echoServer);
    // lwip_udp_echo_server_deinit(&echoServer);
}


void tcp_test(const char* testServerHost, uint16_t testServerPort) {
    NabtoDevice* device = nabto_device_test_new();
    NabtoDeviceFuture* future = nabto_device_future_new(device);

    // Run the TCP test. The test passes if the future resolves and
    // the status of the future is NABTO_DEVICE_EC_OK
    nabto_device_test_tcp(device, testServerHost, testServerPort, future);

    NabtoDeviceError ec = nabto_device_future_wait(future);
    if (ec == NABTO_DEVICE_EC_OK) {
        printf("TCP test has passed\n");
    } else {
        printf("TCP test has failed\n");
    }

    nabto_device_future_free(future);
    nabto_device_test_free(device);
}


int integrationTestTask()
{
    udpecho_raw_init();
    tcpecho_raw_init();
    const char* testServerHost = "192.168.100.200";
    uint16_t testServerPort = 7;
    create_device_test();
    future_test();
    event_queue_test();
    dns_test(testServerHost, testServerPort);
    udp_test(testServerHost, testServerPort);
    tcp_test(testServerHost, testServerPort);
    vTaskDelay(500/portTICK_PERIOD_MS);
    exit(0);

}
