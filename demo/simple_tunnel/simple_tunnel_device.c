#include <FreeRTOS.h>
#include <task.h>

#include "console.h"
#include "lwip_port_init.h"

#include <nabto/nabto_device.h>

#include <apps/common/string_file.h>

#include <stdio.h>
#include <stdlib.h>

const char* appName = "simple_tunnel";

// TCP tunnel configuration.
const char* serviceHost = "192.168.100.1";
uint16_t    servicePort = 80;
const char* serviceId   = "http";
const char* serviceType = "http";

#define NEWLINE "\n"

static int tunnelDeviceTask();
static void nabtoTask(void *arg);

NabtoDeviceError load_or_create_private_key();

NabtoDevice* device = NULL;

static const char* productId;
static const char* deviceId;


int main(int argc, char** argv) {
    if (argc != 3) {
        console_print("The example takes 2 arguments %s <product-id> <device-id>\n", argv[0]);
        return 1;
    }

    productId = argv[1];
    deviceId = argv[2];

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
    tunnelDeviceTask();
    vTaskDelete(NULL);
}


int tunnelDeviceTask()
{

    NabtoDeviceError ec;
    NabtoDeviceFuture* future = NULL;
    NabtoDeviceListener* authorizationListener = NULL;
    NabtoDeviceAuthorizationRequest* authorizationRequest = NULL;
    char* deviceFingerprint = NULL;

    console_print("Nabto Embedded SDK Version %s\n", nabto_device_version());

    device = nabto_device_new();
    future = nabto_device_future_new(device);
    authorizationListener = nabto_device_listener_new(device);

    if (device == NULL || future == NULL || authorizationListener == NULL) {
        console_print("Could not allocate resources" NEWLINE);
        goto cleanup;
    }

    ec = load_or_create_private_key();
    if (ec != NABTO_DEVICE_EC_OK) {
        console_print("Could not load or create the private key. %s" NEWLINE, nabto_device_error_get_message(ec));
        goto cleanup;
    }

    ec = nabto_device_set_product_id(device, productId);
    if (ec != NABTO_DEVICE_EC_OK) {
        console_print("Failed to set product id. %s" NEWLINE, nabto_device_error_get_message(ec));
        goto cleanup;
    }

    ec = nabto_device_set_device_id(device, deviceId);
    if (ec != NABTO_DEVICE_EC_OK) {
        console_print("Failed to set device id. %s" NEWLINE, nabto_device_error_get_message(ec));
        goto cleanup;
    }

    ec = nabto_device_set_app_name(device, appName);
    if (ec != NABTO_DEVICE_EC_OK) {
        console_print("Failed to set app name. %s" NEWLINE, nabto_device_error_get_message(ec));
        goto cleanup;
    }

    const char* logLevel = getenv("NABTO_LOG_LEVEL");
    if (logLevel != NULL) {
        ec = nabto_device_set_log_level(device, logLevel);
        if (ec != NABTO_DEVICE_EC_OK) {
            console_print("Could not set log level. %s" NEWLINE,
                          nabto_device_error_get_message(ec));
            goto cleanup;
        }
    }

    nabto_device_enable_mdns(device);

    nabto_device_set_log_std_out_callback(device);

    /**
     * This is the tunnel specific function all the other code is boiler plate code.
     */
    ec = nabto_device_add_tcp_tunnel_service(device, serviceId, serviceType, serviceHost, servicePort);
    if (ec != NABTO_DEVICE_EC_OK) {
        console_print("Failed to add the tunnel service. %s" NEWLINE, nabto_device_error_get_message(ec));
        goto cleanup;
    }

    ec = nabto_device_get_device_fingerprint(device, &deviceFingerprint);
    if (ec != NABTO_DEVICE_EC_OK) {
        console_print("Could not get the fingerprint. %s" NEWLINE, nabto_device_error_get_message(ec));
        goto cleanup;
    }

    console_print("Configuration:" NEWLINE);
    console_print("ProductId:   %s" NEWLINE, productId);
    console_print("DeviceId:    %s" NEWLINE, deviceId);
    console_print("Fingerprint: %s" NEWLINE, deviceFingerprint);

    nabto_device_start(device, future);
    ec = nabto_device_future_wait(future);
    if (ec != NABTO_DEVICE_EC_OK) {
        console_print("could not start the device. %s" NEWLINE, nabto_device_error_get_message(ec));
        goto cleanup;
    }

    // When a tunnel is created an authorization request is made, allow all
    // these authorization requests.
    ec = nabto_device_authorization_request_init_listener(device, authorizationListener);
    if (ec != NABTO_DEVICE_EC_OK) {
        printf("could not init the authorization request listener. %s" NEWLINE, nabto_device_error_get_message(ec));
        goto cleanup;
    }


    while (true) {
        nabto_device_listener_new_authorization_request(authorizationListener, future, &authorizationRequest);
        ec = nabto_device_future_wait(future);
        if (ec != NABTO_DEVICE_EC_OK) {
            break;
        } else {
            nabto_device_authorization_request_verdict(authorizationRequest, true);
            nabto_device_authorization_request_free(authorizationRequest);
            authorizationRequest = NULL;
        }
    }

 cleanup:
    nabto_device_stop(device);

    nabto_device_string_free(deviceFingerprint);
    nabto_device_listener_free(authorizationListener);
    nabto_device_future_free(future);
    nabto_device_free(device);
}


NabtoDeviceError load_or_create_private_key()
{
    NabtoDeviceError ec;
    const char* privateKeyFileName = "device.key";
    if (!string_file_exists(privateKeyFileName)) {
        char* privateKey;
        ec = nabto_device_create_private_key(device, &privateKey);
        if (ec != NABTO_DEVICE_EC_OK) {
            return ec;
        }
        string_file_save(privateKeyFileName, privateKey);
        nabto_device_string_free(privateKey);
    }

    char* privateKey;
    if (!string_file_load(privateKeyFileName, &privateKey)) {
        return NABTO_DEVICE_EC_INVALID_STATE;
    }
    ec = nabto_device_set_private_key(device, privateKey);
    free(privateKey);
    return ec;
}
