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
    StaticSemaphore_t join_barrier;
    StaticSemaphore_t join_mutex;
};

struct nabto_device_mutex
{
    StaticSemaphore_t mutex;
};

struct nabto_device_condition
{
    BaseType_t is_initialized;
    StaticSemaphore_t semaphore;
    unsigned waiting_threads;
};

static void NabtoThreadTask(void *data)
{
    struct nabto_device_thread *thread = (struct nabto_device_thread*)data;
    thread->function(thread->user_data);
    xSemaphoreGive((SemaphoreHandle_t)&thread->join_barrier);
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
        xSemaphoreCreateMutexStatic(&mut->mutex); 
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
    xSemaphoreCreateCountingStatic(INT_MAX, 0U, &cond->semaphore);
    cond->waiting_threads = 0;
    return cond;
}

void nabto_device_threads_free_thread(struct nabto_device_thread* thread)
{
    vPortFree(thread);
}

void nabto_device_threads_free_mutex(struct nabto_device_mutex *mutex)
{
    vSemaphoreDelete((SemaphoreHandle_t)&mutex->mutex);
    vPortFree(mutex);
}

void nabto_device_threads_free_cond(struct nabto_device_condition *cond)
{
    vSemaphoreDelete((SemaphoreHandle_t)&cond->semaphore);
    vPortFree(cond);
}

void nabto_device_threads_join(struct nabto_device_thread *thread)
{
    if (xSemaphoreTake((SemaphoreHandle_t)&thread->join_mutex, 0) == pdPASS)
    {
        // @TODO: Check whether a thread is trying to join on itself, which is illegal.

        // Wait for joining thread to finish
        xSemaphoreTake((SemaphoreHandle_t)&thread->join_barrier, portMAX_DELAY);
        
        vTaskSuspendAll();
        {
            xSemaphoreGive((SemaphoreHandle_t)&thread->join_barrier);
            vSemaphoreDelete((SemaphoreHandle_t)&thread->join_barrier);
            
            xSemaphoreGive((SemaphoreHandle_t)&thread->join_mutex);
            vSemaphoreDelete((SemaphoreHandle_t)&thread->join_mutex);

            vTaskDelete(thread->task);
        }
        xTaskResumeAll();
    }
}

np_error_code nabto_device_threads_run(struct nabto_device_thread* thread, void *(*run_routine)(void*), void *data)
{
    thread->function = run_routine;
    thread->user_data = data;
    xSemaphoreCreateMutexStatic(&thread->join_mutex);
    xSemaphoreCreateBinaryStatic(&thread->join_barrier);

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
    xSemaphoreTake((SemaphoreHandle_t)&mutex->mutex, portMAX_DELAY);
}

void nabto_device_threads_mutex_unlock(struct nabto_device_mutex *mutex)
{
    xSemaphoreGive((SemaphoreHandle_t)&mutex->mutex);
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
            xSemaphoreGive((SemaphoreHandle_t)&cond->semaphore);
            break;
        }

        local_waiting_threads = cond->waiting_threads;
    }
}

static void TestAndDecrement(struct nabto_device_condition *cond,
                             unsigned local_waiting_threads)
{
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

    if (xSemaphoreTake((SemaphoreHandle_t)&cond->semaphore, delay) == pdPASS)
    {
        nabto_device_threads_mutex_lock(mut);
    }
    else
    {
        TestAndDecrement(cond, local_waiting_threads+1);
    }
}

void nabto_device_threads_cond_wait(struct nabto_device_condition *cond,
                                    struct nabto_device_mutex* mut)
{
    nabto_device_threads_cond_timed_wait(cond, mut, 0);
}

