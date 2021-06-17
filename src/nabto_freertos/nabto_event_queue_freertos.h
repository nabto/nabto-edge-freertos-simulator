#ifndef _NABTO_EVENT_QUEUE_FREERTOS_H_
#define _NABTO_EVENT_QUEUE_FREERTOS_H_

struct nabto_event_queue_freertos {
    TaskHandle_t task;

};

void nabto_event_queue_freertos_init(struct nabto_event_queue_freertos* queue);
void nabto_event_queue_freertos_deinit(struct nabto_event_queue_freertos* queue);
void nabto_event_queue_freertos_stop(struct nabto_event_queue_freertos* queue);

struct np_event_queue nabto_event_queue_freertos_get_impl();

#endif
