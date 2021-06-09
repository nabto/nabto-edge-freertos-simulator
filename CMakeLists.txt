cmake_minimum_required(VERSION 3.2)
project(nabto-freertos)

# @TODO: This is for string_file.c, which we probably shouldn't be using since it's platform-specific code.
add_definitions(-DHAVE_UNISTD_H)

option(DEVICE_BUILD_EXAMPLES "" OFF)
option(DEVICE_BUILD_TESTS "" OFF)
option(DEVICE_BUILD_APPS "" OFF)

set(KERNEL_DIR FreeRTOS/Kernel)
set(LWIP_DIR lwip)
set(NABTO_DIR nabto-embedded-sdk)

add_subdirectory(${NABTO_DIR})

include(${NABTO_DIR}/nabto_primary_files.cmake)
#include(${LWIP_CONTRIB_DIR}/ports/CMakeCommon.cmake)

set(SOURCE_FILES
    src/main.c
    src/console.c
    src/coap.c
    src/nabto_device_threads_freertos.c
    #${NABTO_DIR}/src/modules/threads/unix/nabto_device_threads_unix.c
    src/platform_integration.c
    src/nabto_lwip.c
    src/default_netif.c)

set(KERNEL_SOURCE_FILES
    ${KERNEL_DIR}/croutine.c
    ${KERNEL_DIR}/event_groups.c
    ${KERNEL_DIR}/list.c
    ${KERNEL_DIR}/queue.c
    ${KERNEL_DIR}/stream_buffer.c
    ${KERNEL_DIR}/tasks.c
    ${KERNEL_DIR}/timers.c
    ${KERNEL_DIR}/portable/MemMang/heap_4.c
    ${KERNEL_DIR}/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c
    ${KERNEL_DIR}/portable/ThirdParty/GCC/Posix/port.c)

set(NABTO_SOURCE_FILES
    ${NABTO_DIR}/src/api_test/nabto_device_test_threads.c
    ${NABTO_DIR}/src/api/nabto_device_error.c
    ${NABTO_DIR}/src/modules/event_queue/thread_event_queue.c
    ${NABTO_DIR}/src/modules/event_queue/nm_event_queue.c
    ${NABTO_DIR}/apps/common/string_file.c # @TODO: Probably shouldn't use this? (see todo above)
    ${ne_required_src}
    ${ne_api_test_src})

set_property(SOURCE ${SOURCE_FILES} PROPERTY COMPILE_FLAGS "-Wall -Wextra")
set_property(SOURCE ${NABTO_DIR}/src/core/nc_version.c PROPERTY GENERATED 1)

set(LWIP_INCLUDE_DIRS
    src
    ${LWIP_DIR}/src/include
    lwip-port/include)

include(lwip/src/Filelists.cmake)

set(lwip_port_src
    lwip-port/sys_arch.c
    lwip-port/sys_arch_unix.c
    lwip-port/netif/tapif.c
    lwip-port/netif/pcapif.c
)

set(EXE nabto_demo)
add_executable(${EXE}
    ${SOURCE_FILES}
    ${KERNEL_SOURCE_FILES}
    ${NABTO_SOURCE_FILES}
    ${lwip_port_src})

target_include_directories(${EXE} PUBLIC src)
target_include_directories(${EXE} SYSTEM PUBLIC ${KERNEL_DIR}/include)
target_include_directories(${EXE} SYSTEM PUBLIC ${KERNEL_DIR}/portable/ThirdParty/GCC/Posix)
target_include_directories(${EXE} SYSTEM PUBLIC ${KERNEL_DIR}/portable/ThirdParty/GCC/Posix/utils)
target_include_directories(${EXE} PRIVATE ${LWIP_INCLUDE_DIRS})
target_include_directories(${EXE} SYSTEM PUBLIC ${NABTO_DIR}/include)
target_include_directories(${EXE} SYSTEM PUBLIC ${NABTO_DIR}/src)
target_include_directories(${EXE} SYSTEM PRIVATE ${ne_priv_include_dirs})
target_include_directories(${EXE} SYSTEM PRIVATE ${NABTO_DIR})

add_dependencies(${EXE} GENERATE_VERSION)
target_compile_options(${EXE} PUBLIC -ggdb3 -O0)
target_compile_definitions(${EXE} PUBLIC projCOVERAGE_TEST=0 _WINDOWS_)
target_compile_definitions(${EXE} PRIVATE -DMBEDTLS_CONFIG_FILE=<nabto_mbedtls_config.h> ${LWIP_DEFINITIONS} ${LWIP_MBEDTLS_DEFINITIONS})
target_link_libraries(${EXE} pthread ${LWIP_SANITIZER_LIBS} lwipallapps lwipcore pcap)
