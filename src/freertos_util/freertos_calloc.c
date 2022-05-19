#include "freertos_calloc.h"

#include <FreeRTOS.h>
#include <string.h>

void* pvPortCalloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    void* ptr = pvPortMalloc(total);
    if (ptr == NULL) {
        return ptr;
    }
    memset(ptr, 0, total);
    return ptr;
}
