#ifndef _LWIP_UDP_ECHO_SERVER_H_
#define _LWIP_UDP_ECHO_SERVER_H_

#include <lwip/udp.h>
#include <stdbool.h>

struct lwip_udp_echo_server
{
    bool stopped;
    struct udp_pcb* pcb;
};

bool lwip_udp_echo_server_init(struct lwip_udp_echo_server* ctx, int16_t port);
void lwip_udp_echo_server_deinit(struct lwip_udp_echo_server* ctx);
void lwip_udp_echo_server_stop(struct lwip_udp_echo_server* ctx);

#endif
