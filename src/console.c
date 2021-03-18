#include <stdarg.h>
#include <stdio.h>

#include <FreeRTOS.h>
#include <semphr.h>

static SemaphoreHandle_t m_stdio_mutex;
static StaticSemaphore_t m_stdio_mutex_buffer;

void console_init(void)
{
    m_stdio_mutex = xSemaphoreCreateMutexStatic(&m_stdio_mutex_buffer);
}

void console_print(const char *fmt, ...)
{
    va_list vargs;
    va_start(vargs, fmt);
    xSemaphoreTake(m_stdio_mutex, portMAX_DELAY);
    vprintf(fmt, vargs);
    xSemaphoreGive(m_stdio_mutex);
    va_end(vargs);
}
