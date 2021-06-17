#include "lwip_udp_echo_server.h"
#include "stdbool.h"

static void start_recv(struct lwip_udp_echo_server* ctx);

bool lwip_udp_echo_server_init(struct lwip_udp_echo_server* ctx, int16_t port)
{
    ctx->stopped = false;
    LOCK_TCPIP_CORE();
    ctx->pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    UNLOCK_TCPIP_CORE();
    if (ctx->pcb == NULL) {
        return false;
    }

    LOCK_TCPIP_CORE();
    udp_bind(ctx->pcb, IP_ANY_TYPE, port);
    UNLOCK_TCPIP_CORE();

    start_recv(ctx);
    return true;
}

void lwip_udp_echo_server_deinit(struct lwip_udp_echo_server* ctx)
{
    ctx->stopped = true;
    LOCK_TCPIP_CORE();
    udp_remove(ctx->pcb);
    UNLOCK_TCPIP_CORE();
}

void lwip_udp_echo_server_stop(struct lwip_udp_echo_server* ctx)
{
    ctx->stopped = true;
}

void packet_received(void *arg, struct udp_pcb *pcb, struct pbuf *p,
    const ip_addr_t *addr, u16_t port)
{
    struct lwip_udp_echo_server* ctx = arg;
    LOCK_TCPIP_CORE();
    udp_sendto(pcb, p, addr, port);
    UNLOCK_TCPIP_CORE();
    start_recv(ctx);
}

void start_recv(struct lwip_udp_echo_server* ctx)
{
    if (ctx->stopped) {
        return;
    }
    LOCK_TCPIP_CORE();
    udp_recv(ctx->pcb, packet_received, ctx);
    UNLOCK_TCPIP_CORE();
}
