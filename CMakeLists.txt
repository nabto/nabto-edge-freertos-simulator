cmake_minimum_required(VERSION 3.2)
project(nabto-freertos)

option(DEVICE_BUILD_EXAMPLES "" OFF)
option(DEVICE_BUILD_TESTS "" OFF)
option(DEVICE_BUILD_APPS "" OFF)

set(KERNEL_DIR FreeRTOS/Kernel)
set(TCP_DIR FreeRTOS/FreeRTOS-Plus-TCP)
set(NABTO_DIR nabto-embedded-sdk)

add_subdirectory(${NABTO_DIR})

include(${NABTO_DIR}/nabto_primary_files.cmake)

set(SOURCE_FILES
    src/main.c
    src/console.c
    src/nabto_device_threads_freertos.c
    src/platform_integration.c
    ${KERNEL_DIR}/croutine.c
    ${KERNEL_DIR}/event_groups.c
    ${KERNEL_DIR}/list.c
    ${KERNEL_DIR}/queue.c
    ${KERNEL_DIR}/stream_buffer.c
    ${KERNEL_DIR}/tasks.c
    ${KERNEL_DIR}/timers.c
    ${KERNEL_DIR}/portable/MemMang/heap_3.c
    ${KERNEL_DIR}/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c
    ${KERNEL_DIR}/portable/ThirdParty/GCC/Posix/port.c)

set(NABTO_SOURCE_FILES
    ${NABTO_DIR}/src/api_test/nabto_device_test_threads.c
    ${NABTO_DIR}/src/api/nabto_device_error.c
    ${ne_required_src}
    ${ne_api_test_src})


set(EXE nabto_demo)
add_executable(${EXE} ${SOURCE_FILES} ${NABTO_SOURCE_FILES})

target_include_directories(${EXE} PUBLIC src)
target_include_directories(${EXE} PUBLIC ${KERNEL_DIR}/include)
target_include_directories(${EXE} PUBLIC ${KERNEL_DIR}/portable/ThirdParty/GCC/Posix)
target_include_directories(${EXE} PUBLIC ${KERNEL_DIR}/portable/ThirdParty/GCC/Posix/utils)
target_include_directories(${EXE} PUBLIC ${TCP_DIR}/portable/NetworkInterface/linux)
target_include_directories(${EXE} PUBLIC ${TCP_DIR}/include)
target_include_directories(${EXE} PUBLIC ${TCP_DIR}/portable/Compiler/GCC)
target_include_directories(${EXE} PUBLIC ${NABTO_DIR}/include)
target_include_directories(${EXE} PUBLIC ${NABTO_DIR}/src)
target_include_directories(${EXE} PRIVATE ${ne_priv_include_dirs})

target_compile_options(${EXE} PUBLIC -ggdb3 -O0 -Wall)
target_link_libraries(${EXE} pthread pcap)
target_compile_definitions(${EXE} PUBLIC projCOVERAGE_TEST=0 _WINDOWS_)
target_compile_definitions(${EXE} PRIVATE -DMBEDTLS_CONFIG_FILE=<nabto_mbedtls_config.h>)

