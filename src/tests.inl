void thread_test(void)
{
    NabtoDeviceError ec = nabto_device_test_threads();
    if (ec == NABTO_DEVICE_EC_OK)
    {
        console_print("Threads test passed\n");
    }
    else
    {
        console_print("Threads test failed\n");
    }
}

void create_device_test(void)
{
    NabtoDevice *device = nabto_device_test_new();
    if (device != NULL)
    {
        console_print("Create device test passed\n");
    }
    else
    {
        console_print("Create device test failed\n");
    }

    nabto_device_test_free(device);
}

void future_test(void)
{
    NabtoDevice *device = nabto_device_test_new();
    if (device == NULL)
    {
        console_print("Future test failed: device is NULL\n");
    }

    NabtoDeviceFuture *future = nabto_device_future_new(device);
    if (future == NULL)
    {
        console_print("Future test failed: future is NULL\n");
    }

    nabto_device_test_future_resolve(device, future);
    NabtoDeviceError ec = nabto_device_future_wait(future);

    if (ec == NABTO_DEVICE_EC_OK)
    {
        console_print("Future resolve test has passed\n");
    }
    else
    {
        console_print("Future resolve test has failed.\n");
    }

    nabto_device_test_free(device);
}

void logging_test(void)
{
    NabtoDevice *device = nabto_device_test_new();
    nabto_device_set_log_level(device, "trace");
    nabto_device_set_log_callback(device, log_callback, NULL);
    nabto_device_test_logging(device);
    nabto_device_set_log_level(device, "info");
    console_print("Logging test passed if ERROR, WARN, INFO and TRACE logs were seen in output\n");
    nabto_device_test_free(device);
}

void timestamp_test(void)
{
    console_print("Sleeping for 500ms for timestamp test...\n");
    vTaskDelay(500 / portTICK_PERIOD_MS);
    NabtoDevice *device = nabto_device_test_new();
    uint32_t timestamp;
    nabto_device_test_timestamp(device, &timestamp);
    console_print("Timestamp in ms: %u\n", timestamp);
    console_print("Test passed if timestamp is correct.\n");
    nabto_device_test_free(device);
}

void event_queue_test(void)
{
    NabtoDevice *device = nabto_device_test_new();
    NabtoDeviceFuture *future = nabto_device_future_new(device);
    nabto_device_test_event_queue(device, future);

    NabtoDeviceError ec = nabto_device_future_wait(future);
    if (ec == NABTO_DEVICE_EC_OK)
    {
        console_print("Event queue test has passed\n");
    }
    else
    {
        console_print("Event queue test has failed\n");
    }

    nabto_device_future_free(future);
    nabto_device_test_free(device);
}

