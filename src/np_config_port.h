#include <FreeRTOS.h>
#include <portable.h>
#include <freertos_util/freertos_calloc.h>

#define NP_ALLOCATOR_FREE vPortFree
#define NP_ALLOCATOR_CALLOC pvPortCalloc
