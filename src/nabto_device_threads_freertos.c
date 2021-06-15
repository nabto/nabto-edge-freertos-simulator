#include <api/nabto_device_threads.h>

#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <atomic.h>

struct nabto_device_thread
{
    TaskHandle_t task;
    void *(*function)(void*);
    void *user_data;
    SemaphoreHandle_t join_barrier;
    SemaphoreHandle_t join_mutex;
};

struct nabto_device_mutex
{
    SemaphoreHandle_t mutex;
};

struct nabto_device_condition
{
    BaseType_t is_initialized;
    SemaphoreHandle_t semaphore;
    unsigned waiting_threads;
};

static void NabtoThreadTask(void *data)
{
    struct nabto_device_thread *thread = (struct nabto_device_thread*)data;
    thread->function(thread->user_data);
    xSemaphoreGive(thread->join_barrier);
    // @TODO: A task suspends itself unless it is joined by
    // nabto_device_threads_join. This may mean unless join is always called
    // some dead threads will just lie around suspended.
    vTaskSuspend(NULL);
}

struct nabto_device_thread* nabto_device_threads_create_thread()
{
    struct nabto_device_thread *thread = pvPortMalloc(sizeof(*thread));
    if (thread)
    {
        thread->task = NULL;
    }
    return thread;
}

struct nabto_device_mutex* nabto_device_threads_create_mutex()
{
    struct nabto_device_mutex *mut = pvPortMalloc(sizeof(*mut));
    if (mut)
    {
        mut->mutex = xSemaphoreCreateMutex();
    }

    return mut;
}

struct nabto_device_condition *nabto_device_threads_create_condition()
{
    struct nabto_device_condition *cond = pvPortMalloc(sizeof(*cond));
    if (cond == NULL)
    {
        return NULL;
    }

    cond->is_initialized = pdTRUE;
    cond->semaphore = xSemaphoreCreateCounting(INT_MAX, 0U);
    cond->waiting_threads = 0;
    return cond;
}

void nabto_device_threads_free_thread(struct nabto_device_thread* thread)
{
    vPortFree(thread);
}

void nabto_device_threads_free_mutex(struct nabto_device_mutex *mutex)
{
    vSemaphoreDelete(mutex->mutex);
    vPortFree(mutex);
}

void nabto_device_threads_free_cond(struct nabto_device_condition *cond)
{
    vSemaphoreDelete(cond->semaphore);
    vPortFree(cond);
}

void nabto_device_threads_join(struct nabto_device_thread *thread)
{
    if (xSemaphoreTake((SemaphoreHandle_t)&thread->join_mutex, 0) == pdPASS)
    {
        // @TODO: Check whether a thread is trying to join on itself, which is illegal.

        // Wait for joining thread to finish
        xSemaphoreTake((SemaphoreHandle_t)&thread->join_barrier, portMAX_DELAY);

        // Suspend all tasks while cleaning up this thread.
        vTaskSuspendAll();
        {
            xSemaphoreGive(thread->join_barrier);
            vSemaphoreDelete(thread->join_barrier);

            xSemaphoreGive(thread->join_mutex);
            vSemaphoreDelete(thread->join_mutex);

            vTaskDelete(thread->task);
        }
        xTaskResumeAll();
    }
}

np_error_code nabto_device_threads_run(struct nabto_device_thread* thread, void *(*run_routine)(void*), void *data)
{
    thread->function = run_routine;
    thread->user_data = data;
    thread->join_mutex = xSemaphoreCreateMutex();
    thread->join_barrier = xSemaphoreCreateBinary();

    // @TODO: Give more stack size than configMINIMAL_STACK_SIZE (which could be very small)?
    BaseType_t task_create_error = xTaskCreate(NabtoThreadTask,
                                               NULL,
                                               configMINIMAL_STACK_SIZE,
                                               (void*)thread,
                                               configMAX_PRIORITIES-1,
                                               &thread->task);
    if (task_create_error != pdPASS)
    {
        return NABTO_EC_UNKNOWN;
    }

    return NABTO_EC_OK;
}

void nabto_device_threads_mutex_lock(struct nabto_device_mutex *mutex)
{
    xSemaphoreTake(mutex->mutex, portMAX_DELAY);
}

void nabto_device_threads_mutex_unlock(struct nabto_device_mutex *mutex)
{
    xSemaphoreGive(mutex->mutex);
}

void nabto_device_threads_cond_signal(struct nabto_device_condition *cond)
{
    unsigned local_waiting_threads = cond->waiting_threads;
    while (local_waiting_threads > 0)
    {
        if (ATOMIC_COMPARE_AND_SWAP_SUCCESS ==
            Atomic_CompareAndSwap_u32((uint32_t*)&cond->waiting_threads,
                                      (uint32_t)local_waiting_threads-1,
                                      (uint32_t)local_waiting_threads))
        {
            xSemaphoreGive(cond->semaphore);
            break;
        }

        local_waiting_threads = cond->waiting_threads;
    }
}

void nabto_device_threads_cond_timed_wait(struct nabto_device_condition *cond,
                                          struct nabto_device_mutex *mut,
                                          uint32_t ms)
{
    unsigned local_waiting_threads;
    TickType_t delay = portMAX_DELAY;

    if (ms > 0)
    {
        delay = pdMS_TO_TICKS(ms);
    }

    local_waiting_threads = Atomic_Increment_u32((uint32_t*)&cond->waiting_threads);
    nabto_device_threads_mutex_unlock(mut);

    if (xSemaphoreTake(cond->semaphore, delay) == pdPASS)
    {
        nabto_device_threads_mutex_lock(mut);
    }
    else
    {
        nabto_device_threads_mutex_lock(mut);

        // Try to decrement cond->waiting_threads
        while (local_waiting_threads > 0)
        {
            if (ATOMIC_COMPARE_AND_SWAP_SUCCESS ==
                Atomic_CompareAndSwap_u32((uint32_t*)&cond->waiting_threads,
                                          (uint32_t)local_waiting_threads-1,
                                          (uint32_t)local_waiting_threads))
            {
                break;
            }

            local_waiting_threads = cond->waiting_threads;
        }
    }
}

void nabto_device_threads_cond_wait(struct nabto_device_condition *cond,
                                    struct nabto_device_mutex* mut)
{
    nabto_device_threads_cond_timed_wait(cond, mut, 0);
}
