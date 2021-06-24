cmake_minimum_required(VERSION 3.2)
project(nabto-freertos)

# @TODO: This is for string_file.c, which we probably shouldn't be using since it's platform-specific code.
add_definitions(-DHAVE_UNISTD_H)

option(DEVICE_BUILD_EXAMPLES "" OFF)
option(DEVICE_BUILD_TESTS "" OFF)
option(DEVICE_BUILD_APPS "" OFF)

option(USE_TAPIF "Use a tapif for communication" ON)
option(USE_PCAPIF "Use a pcap interface for communication" OFF)

if (USE_TAPIF AND USE_PCAPIF)
    message("USE_PCAPIF AND USE_TAPIF cannot be used at the same time.")
elseif(NOT USE_TAPIF AND NOT USE_PCAPIF)
    message("One of USE_PCAPIF and USE_TAPIF need to be enabled.")
endif()

set(KERNEL_DIR FreeRTOS/Kernel)
set(LWIP_DIR lwip)
set(NABTO_DIR nabto-embedded-sdk)

add_subdirectory(${NABTO_DIR} EXCLUDE_FROM_ALL)

include(${NABTO_DIR}/nabto_primary_files.cmake)
#include(${LWIP_CONTRIB_DIR}/ports/CMakeCommon.cmake)

set(simple_coap_demo_src
    demo/simple_coap/simple_coap_device.c
)

set(simple_tunnel_demo_src
    demo/simple_tunnel/simple_tunnel_device.c
)

set(port_src
    src/console.c
    src/nabto_device_threads_freertos.c
    src/platform_integration.c
    #src/nabto_lwip.c

    src/default_netif.c
    src/lwip_port_init.c
    src/nabto_mdns_lwip/nm_mdns_lwip.c
    )

set(freertos_kernel_src
    ${KERNEL_DIR}/croutine.c
    ${KERNEL_DIR}/event_groups.c
    ${KERNEL_DIR}/list.c
    ${KERNEL_DIR}/queue.c
    ${KERNEL_DIR}/stream_buffer.c
    ${KERNEL_DIR}/tasks.c
    ${KERNEL_DIR}/timers.c
    ${KERNEL_DIR}/portable/MemMang/heap_4.c
    ${KERNEL_DIR}/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c
    ${KERNEL_DIR}/portable/ThirdParty/GCC/Posix/port.c
    src/freertos_port.c
    )

set(nabto_src
    ${NABTO_DIR}/src/api_test/nabto_device_test_threads.c
    ${NABTO_DIR}/src/api/nabto_device_error.c
    ${NABTO_DIR}/src/modules/event_queue/thread_event_queue.c
    ${NABTO_DIR}/src/modules/event_queue/nm_event_queue.c
    ${NABTO_DIR}/apps/common/string_file.c # @TODO: Probably shouldn't use this? (see todo above)
    ${ne_required_src}
    ${ne_api_test_src}
    ${NABTO_DIR}/src/modules/mdns/nm_mdns_server.c
    ${NABTO_DIR}/nabto-common/components/mdns/src/mdns_server.c
    )

set_property(SOURCE ${NABTO_DIR}/src/core/nc_version.c PROPERTY GENERATED 1)

set(LWIP_INCLUDE_DIRS
    src
    ${LWIP_DIR}/src/include
    lwip-port/include)

include(lwip/src/Filelists.cmake)

set(lwip_port_src
    lwip-port/sys_arch.c
    lwip-port/sys_arch_unix.c
)

set(lwip_netif_src
    lwip-port/netif/list.c
    lwip-port/netif/tapif.c
    lwip-port/netif/pcapif.c
)

if (USE_TAPIF)
    add_definitions(-DUSE_TAPIF)
elseif(USE_PCAPIF)
    add_definitions(-DUSE_PCAPIF)
endif()

set(nabto_lwip_src
    src/nabto_lwip/nm_nabto_lwip.c
    src/nabto_lwip/nm_nabto_lwip_tcp.c
    src/nabto_lwip/nm_nabto_lwip_util.c
)

set(integration_test_src
    integration_test/integration_test.c
    lwip-contrib/apps/udpecho_raw/udpecho_raw.c
    lwip-contrib/apps/tcpecho_raw/tcpecho_raw.c
    #integration_test/lwip_udp_echo_server.c
    #integration_test/lwip_tcp_echo_server.c
)

# If the different integration components are split into libraries, cyclic dependencies occurs. To circumvent cyclic dependencies all library code is compiled to one library.

add_library(nabto_freertos_lwip_simulator ${port_src} ${freertos_kernel_src} ${lwipcore_SRCS} ${nabto_lwip_src} ${nabto_src} ${lwip_netif_src} ${lwipcore4_SRCS} ${lwipcore6_SRCS} ${lwipapi_SRCS} ${lwipnetif_SRCS} ${lwip_port_src})
target_include_directories(nabto_freertos_lwip_simulator PUBLIC src)
target_include_directories(nabto_freertos_lwip_simulator SYSTEM PUBLIC ${KERNEL_DIR}/include)
target_include_directories(nabto_freertos_lwip_simulator SYSTEM PUBLIC ${KERNEL_DIR}/portable/ThirdParty/GCC/Posix)
target_include_directories(nabto_freertos_lwip_simulator SYSTEM PUBLIC ${KERNEL_DIR}/portable/ThirdParty/GCC/Posix/utils)
target_include_directories(nabto_freertos_lwip_simulator PRIVATE ${LWIP_INCLUDE_DIRS})
target_include_directories(nabto_freertos_lwip_simulator SYSTEM PUBLIC ${NABTO_DIR}/include)
target_include_directories(nabto_freertos_lwip_simulator SYSTEM PUBLIC ${NABTO_DIR}/src)
target_include_directories(nabto_freertos_lwip_simulator SYSTEM PRIVATE ${ne_priv_include_dirs})
target_include_directories(nabto_freertos_lwip_simulator SYSTEM PRIVATE ${NABTO_DIR})
target_link_libraries(nabto_freertos_lwip_simulator pthread)
target_compile_definitions(nabto_freertos_lwip_simulator PRIVATE -DMBEDTLS_CONFIG_FILE=<nabto_mbedtls_config.h> ${LWIP_DEFINITIONS} ${LWIP_MBEDTLS_DEFINITIONS})

add_dependencies(nabto_freertos_lwip_simulator GENERATE_VERSION)

add_executable(integration_test ${integration_test_src})
target_link_libraries(integration_test nabto_freertos_lwip_simulator )
target_include_directories(integration_test PRIVATE ${LWIP_INCLUDE_DIRS})
target_include_directories(integration_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lwip-contrib/apps/udpecho_raw)
target_include_directories(integration_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lwip-contrib/apps/tcpecho_raw)

add_executable(simple_coap ${simple_coap_demo_src})
target_link_libraries(simple_coap nabto_freertos_lwip_simulator )
target_include_directories(simple_coap PUBLIC demo)
target_include_directories(simple_coap PUBLIC ${NABTO_DIR})

add_executable(simple_tunnel ${simple_tunnel_demo_src})
target_link_libraries(simple_tunnel nabto_freertos_lwip_simulator )
target_include_directories(simple_tunnel PUBLIC demo)
target_include_directories(simple_tunnel PUBLIC ${NABTO_DIR})
