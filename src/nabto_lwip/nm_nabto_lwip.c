#include "nm_nabto_lwip.h"

#include <string.h>

#include <FreeRTOS.h>

#include <lwip/dns.h>
#include <lwip/udp.h>
#include <lwip/tcp.h>
#include <lwip/netif.h>
#include <lwip/apps/mdns.h>

#include <platform/interfaces/np_dns.h>
#include <platform/interfaces/np_udp.h>
#include <platform/interfaces/np_tcp.h>
#include <platform/interfaces/np_mdns.h>

#include <platform/np_types.h>
#include <platform/np_error_code.h>
#include <platform/np_completion_event.h>
#include <platform/np_logging.h>

#include <nn/string_set.h>
#include <nn/string_map.h>

#include "common.h"
#include "default_netif.h"

#define DNS_LOG  NABTO_LOG_MODULE_DNS
#define UDP_LOG  NABTO_LOG_MODULE_UDP
#define TCP_LOG  NABTO_LOG_MODULE_TCP
#define MDNS_LOG NABTO_LOG_MODULE_MDNS

typedef struct
{
    size_t ips_size;
    size_t *ips_resolved;
    struct np_ip_address *ips;
    struct np_completion_event *completion_event;
    int addr_type;
} dns_resolve_event;

struct np_udp_socket
{
    struct udp_pcb *upcb;
    struct pbuf *packet;
    struct np_ip_address addr;
    u16_t port;
    struct np_completion_event *ce;
    bool aborted;
};

struct np_tcp_socket
{
    struct tcp_pcb *pcb;
    struct np_completion_event *connect_ce;
    struct pbuf *packet;
};

// ---------------------
// Utility functions
// ---------------------

static void nplwip_convertip_np_to_lwip(const struct np_ip_address *from, ip_addr_t *to)
{
    memset(to, 0, sizeof(*to));
    if (from->type == NABTO_IPV4)
    {
        to->type = IPADDR_TYPE_V4;
        memcpy(&to->u_addr.ip4.addr, from->ip.v4, sizeof(to->u_addr.ip4.addr));
    }
    else
    {
        to->type = IPADDR_TYPE_V6;
        memcpy(to->u_addr.ip6.addr, from->ip.v6, sizeof(to->u_addr.ip6.addr));
    }
}

static void nplwip_convertip_lwip_to_np(const ip_addr_t *from, struct np_ip_address *to)
{
    memset(to, 0, sizeof(*to));
    if (from->type == IPADDR_TYPE_V4)
    {
        to->type = NABTO_IPV4;
        memcpy(to->ip.v4, &from->u_addr.ip4.addr, sizeof(to->ip.v4));
    }
    else
    {
        to->type = NABTO_IPV6;
        memcpy(to->ip.v6, from->u_addr.ip6.addr, sizeof(to->ip.v6));
    }
}

// ---------------------
// DNS
// ---------------------

static void nplwip_dns_resolve_callback(const char *name, const ip_addr_t *addr, void *arg)
{
    NABTO_LOG_INFO(DNS_LOG, "DNS resolved %s to %s", name, ipaddr_ntoa(addr));
    dns_resolve_event *event = (dns_resolve_event*)arg;
    if (addr && addr->type == event->addr_type && event->ips_size >= 1)
    {
        // @TODO: Currently we only resolve to one ip, which might be incorrect.
        *event->ips_resolved = 1;
        nplwip_convertip_lwip_to_np(addr, &event->ips[0]);
        np_completion_event_resolve(event->completion_event, NABTO_EC_OK);
    }
    else
    {
        np_completion_event_resolve(event->completion_event, NABTO_EC_UNKNOWN);
    }
}

static void nplwip_async_resolve(struct np_dns *obj, const char *host,
                                 struct np_ip_address *ips,
                                 size_t ips_size, size_t *ips_resolved,
                                 struct np_completion_event *completion_event,
                                 int addr_type)
{
    UNUSED(obj);
    dns_resolve_event *event = malloc(sizeof *event);
    event->ips_size = ips_size;
    event->ips_resolved = ips_resolved;
    event->ips = ips;
    event->completion_event = completion_event;
    event->addr_type = addr_type;
    u8_t dns_addrtype = addr_type == IPADDR_TYPE_V4 ? LWIP_DNS_ADDRTYPE_IPV4 : LWIP_DNS_ADDRTYPE_IPV6;

    LOCK_TCPIP_CORE();
    struct ip_addr resolved;
    err_t Error = dns_gethostbyname_addrtype(host, &resolved,
                                             nplwip_dns_resolve_callback, event,
                                             dns_addrtype);
    UNLOCK_TCPIP_CORE();

    switch (Error)
    {
        case ERR_OK:
        {
            NABTO_LOG_INFO(DNS_LOG, "DNS resolved %s to %s", host, ipaddr_ntoa(&resolved));
            free(event);
            *ips_resolved = 1;
            nplwip_convertip_lwip_to_np(&resolved, &ips[0]);
            np_completion_event_resolve(completion_event, NABTO_EC_OK);
            break;
        }
        case ERR_INPROGRESS:
        {
            // We can log something here if we want to.
        } break;

        default:
        {
            NABTO_LOG_ERROR(DNS_LOG, "Failed to send DNS request for %s", host);
            np_completion_event_resolve(completion_event, NABTO_EC_UNKNOWN);
            return;
        }
    }
}

static void nplwip_async_resolve_ipv4(struct np_dns *obj, const char *host,
                                      struct np_ip_address *ips,
                                      size_t ips_size, size_t *ips_resolved,
                                      struct np_completion_event *completion_event)
{
    nplwip_async_resolve(obj, host, ips, ips_size, ips_resolved, completion_event, IPADDR_TYPE_V4);
}

static void nplwip_async_resolve_ipv6(struct np_dns *obj, const char *host,
                                      struct np_ip_address *ips,
                                      size_t ips_size, size_t *ips_resolved,
                                      struct np_completion_event *completion_event)
{
    nplwip_async_resolve(obj, host, ips, ips_size, ips_resolved, completion_event, IPADDR_TYPE_V6);
}

// ---------------------
// UDP
// ---------------------

static void nplwip_udp_callback(void *arg, struct udp_pcb *upcb, struct pbuf *p,
                                const ip_addr_t *addr, u16_t port)
{
    UNUSED(upcb);
    NABTO_LOG_INFO(UDP_LOG, "Received UDP packet from %s:%i", ip_ntoa(addr), port);
    struct np_udp_socket *socket = (struct np_udp_socket*)arg;
    if (p != NULL && socket->packet == NULL)
    {
        socket->packet = p;
        nplwip_convertip_lwip_to_np(addr, &socket->addr);
        socket->port = port;
    }
    else
    {
        pbuf_free(p);
    }

    if (socket->ce)
    {
        struct np_completion_event* e = socket->ce;
        socket->ce = NULL;
        np_completion_event_resolve(e, NABTO_EC_OK);
    }
}

static np_error_code nplwip_create_socket(struct np_udp *obj, struct np_udp_socket **out_socket)
{
    UNUSED(obj);

    struct np_udp_socket *socket = malloc(sizeof(struct np_udp_socket));
    if (socket == NULL)
    {
        return NABTO_EC_OUT_OF_MEMORY;
    }

    LOCK_TCPIP_CORE();
    socket->upcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    UNLOCK_TCPIP_CORE();
    if (socket->upcb == NULL) {
        free(socket);
        return NABTO_EC_OUT_OF_MEMORY;
    }

    // @TODO: Check if socket->upcb is valid.

    socket->aborted = false;
    socket->packet = NULL;
    socket->ce = NULL;

    *out_socket = socket;
    return NABTO_EC_OK;
}

static void nplwip_abort_socket(struct np_udp_socket *socket)
{
    socket->aborted = true;
}

static void nplwip_destroy_socket(struct np_udp_socket *socket)
{
    if (socket == NULL)
    {
        NABTO_LOG_ERROR(UDP_LOG, "Socket destroyed twice.");
        return;
    }

    nplwip_abort_socket(socket);

    LOCK_TCPIP_CORE();
    udp_remove(socket->upcb);
    free(socket);
    UNLOCK_TCPIP_CORE();
}

static void nplwip_async_bind_port(struct np_udp_socket *socket, uint16_t port,
                                   struct np_completion_event *completion_event)
{
    np_error_code ec = NABTO_EC_OK;

    if (socket->aborted)
    {
        NABTO_LOG_ERROR(UDP_LOG, "bind called on an aborted socket.");
        ec = NABTO_EC_ABORTED;
    }
    else
    {
        LOCK_TCPIP_CORE();
        err_t error = udp_bind(socket->upcb, IP4_ADDR_ANY, port);
        if (error == ERR_OK)
        {
            udp_recv(socket->upcb, nplwip_udp_callback, socket);
        }
        else
        {
            NABTO_LOG_ERROR(UDP_LOG, "lwip udp_bind() failed with error: %i", error);
            ec = NABTO_EC_UNKNOWN;
        }
        UNLOCK_TCPIP_CORE();
    }

    np_completion_event_resolve(completion_event, ec);
}

static np_error_code nplwip_async_sendto_ec(struct np_udp_socket *socket, struct np_udp_endpoint *ep,
                                   uint8_t *buffer, uint16_t buffer_size)
{
    np_error_code ec = NABTO_EC_OK;

    if (socket->aborted)
    {
        NABTO_LOG_ERROR(UDP_LOG, "sendto called on an aborted socket.");
        return NABTO_EC_ABORTED;
    }

    struct pbuf *packet = pbuf_alloc(PBUF_TRANSPORT, buffer_size, PBUF_RAM);
    if (packet == NULL) {
        return NABTO_EC_OUT_OF_MEMORY;
    }
    memcpy(packet->payload, buffer, buffer_size);

    ip_addr_t ip;
    nplwip_convertip_np_to_lwip(&ep->ip, &ip);

    LOCK_TCPIP_CORE();
    err_t lwip_err = udp_sendto(socket->upcb, packet, &ip, ep->port);
    UNLOCK_TCPIP_CORE();
    pbuf_free(packet);

    if (lwip_err == ERR_VAL)
    {
        // probably because we are sending an ipv6 packet etc
        return NABTO_EC_OK;
    }
    else if (lwip_err != ERR_OK)
    {
        NABTO_LOG_ERROR(UDP_LOG, "Unknown lwIP error in udp_sendto().");
        return NABTO_EC_UNKNOWN;
    }
    return NABTO_EC_OK;
}

static void nplwip_async_sendto(struct np_udp_socket *socket, struct np_udp_endpoint *ep,
                                uint8_t *buffer, uint16_t buffer_size,
                                struct np_completion_event *completion_event)
{
    UNUSED(completion_event);
    np_error_code ec = nplwip_async_sendto_ec(socket, ep, buffer, buffer_size);
    np_completion_event_resolve(completion_event, ec);
}

static void nplwip_async_recv_wait(struct np_udp_socket *socket, struct np_completion_event *completion_event)
{
    if (socket->aborted)
    {
        NABTO_LOG_ERROR(UDP_LOG, "async_recv_wait called on an aborted socket.");
        np_completion_event_resolve(completion_event, NABTO_EC_ABORTED);
    }

    if (socket->ce == NULL)
    {
        socket->ce = completion_event;
    }
    else
    {
        NABTO_LOG_ERROR(UDP_LOG, "async_recv_wait called but there's already a waiting recv.");
        np_completion_event_resolve(completion_event, NABTO_EC_UDP_SOCKET_ERROR);
    }
}

static np_error_code nplwip_recv_from(struct np_udp_socket *socket, struct np_udp_endpoint *ep,
                                      uint8_t *buffer, size_t buffer_size, size_t *recv_size)
{
    if (socket->aborted)
    {
        NABTO_LOG_ERROR(UDP_LOG, "recv_from called on an aborted socket.");
        // @TODO: Should NABTO_EC_ABORTED be returned here?
        return NABTO_EC_EOF;
    }

    if (socket->packet)
    {
        struct pbuf *packet = socket->packet;
        socket->packet = NULL;
        ep->ip = socket->addr;
        ep->port = socket->port;

        if (packet->len > buffer_size)
        {
            *recv_size = buffer_size;
            memcpy(buffer, packet->payload, buffer_size);
        }
        else
        {
            *recv_size = packet->len;
            memcpy(buffer, packet->payload, packet->len);
        }

        pbuf_free(packet);
        return NABTO_EC_OK;
    }
    else
    {
        return NABTO_EC_AGAIN;
    }
}

static uint16_t nplwip_get_local_port(struct np_udp_socket *socket)
{
    return socket->upcb->local_port;
}

// ---------------------
// TCP
// ---------------------

static void nplwip_tcp_destroy(struct np_tcp_socket *socket);

static err_t nplwip_tcp_connected_callback(void *arg, struct tcp_pcb *tpcb, err_t err)
{
    UNUSED(tpcb);
    UNUSED(err);
    struct np_tcp_socket *socket = (struct np_tcp_socket*)arg;
    np_completion_event_resolve(socket->connect_ce, NABTO_EC_OK);
    return ERR_OK;
}

static err_t nplwip_tcp_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    UNUSED(tpcb);
    struct np_tcp_socket *socket = (struct np_tcp_socket*)arg;

    if (p == NULL)
    {
        // If this function is called with NULL then the host has closed the connection.
        // nplwip_tcp_destroy(socket);
        return ERR_OK;
    }

    if (err != ERR_OK)
    {
        // @TODO: For some reason, we got a packet that we did not expect.
        return err;
    }
    else
    {
        if (socket->packet == NULL)
        {
            socket->packet = p;
        }
        else
        {
            pbuf_cat(socket->packet, p);
        }
    }

    return ERR_OK;
}

static np_error_code nplwip_tcp_create(struct np_tcp *obj, struct np_tcp_socket **out_socket)
{
    UNUSED(obj);
    struct np_tcp_socket *socket = malloc(sizeof(struct np_tcp_socket));
    if (socket == NULL)
    {
        return NABTO_EC_OUT_OF_MEMORY;
    }

    LOCK_TCPIP_CORE();
    socket->pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    tcp_arg(socket->pcb, socket);
    tcp_recv(socket->pcb, nplwip_tcp_recv_callback);
    UNLOCK_TCPIP_CORE();

    socket->connect_ce = NULL;
    socket->packet = NULL;

    // @TODO: Check if socket->pcb is valid.
    // @TODO: Set an error callback with tcp_err()

    *out_socket = socket;
    return NABTO_EC_OK;
}

static void nplwip_tcp_abort(struct np_tcp_socket *socket)
{
    LOCK_TCPIP_CORE();
    tcp_abort(socket->pcb);
    UNLOCK_TCPIP_CORE();
}

static void nplwip_tcp_destroy(struct np_tcp_socket *socket)
{
    if (socket == NULL)
    {
        NABTO_LOG_ERROR(TCP_LOG, "TCP socket destroyed twice.");
        return;
    }

    LOCK_TCPIP_CORE();
    tcp_arg(socket->pcb, NULL);
    tcp_sent(socket->pcb, NULL);
    tcp_recv(socket->pcb, NULL);
    err_t error = tcp_close(socket->pcb);
    UNLOCK_TCPIP_CORE();
    if (error == ERR_MEM)
    {
        NABTO_LOG_ERROR(TCP_LOG, "lwIP failed to close TCP socket due to lack of memory.")
        // @TODO: We need to wait and try to close again in the acknowledgement callback.
    }

    free(socket);
}

static void nplwip_tcp_async_connect(struct np_tcp_socket *socket, struct np_ip_address *addr, uint16_t port,
                                     struct np_completion_event *completion_event)
{
    ip_addr_t ip;
    nplwip_convertip_np_to_lwip(addr, &ip);

    socket->connect_ce = completion_event;

    LOCK_TCPIP_CORE();
    err_t error = tcp_connect(socket->pcb, &ip, port, nplwip_tcp_connected_callback);
    UNLOCK_TCPIP_CORE();
    if (error == ERR_MEM)
    {
        NABTO_LOG_ERROR(TCP_LOG, "TCP socket could not connect due to lack of memory.");
        np_completion_event_resolve(completion_event, NABTO_EC_UNKNOWN);
    }
}

// @TODO: Currently calling tcp_output to flush out data immediately for simplicity.
// This may not be optimal (see lwip docs).
static void nplwip_tcp_async_write(struct np_tcp_socket *socket, const void *data, size_t data_len,
                                   struct np_completion_event *completion_event)
{
    LOCK_TCPIP_CORE();
    err_t error;

    error = tcp_write(socket->pcb, data, data_len, 0);
    if (error != ERR_OK)
    {
        NABTO_LOG_ERROR(TCP_LOG, "tcp_write failed, lwIP error: %i", error);
        np_completion_event_resolve(completion_event, NABTO_EC_UNKNOWN);
    }

    error = tcp_output(socket->pcb);
    if (error != ERR_OK)
    {
        NABTO_LOG_ERROR(TCP_LOG, "tcp_output failed, lwIP error: %i", error);
        np_completion_event_resolve(completion_event, NABTO_EC_UNKNOWN);
    }

    UNLOCK_TCPIP_CORE();

    if (error == ERR_OK)
    {
        np_completion_event_resolve(completion_event, NABTO_EC_OK);
    }
}

static void nplwip_tcp_async_read(struct np_tcp_socket *socket, void *buffer, size_t buffer_len, size_t *read_len,
                                  struct np_completion_event *completion_event)
{
    struct pbuf *ptr;
    size_t read = 0;

    LOCK_TCPIP_CORE();
    while (socket->packet != NULL && (read + socket->packet->len) <= buffer_len)
    {
        ptr = socket->packet;
        u16_t plen = ptr->len;
        read += plen;

        memcpy(buffer, ptr->payload, plen);
        socket->packet = ptr->next;
        pbuf_free(ptr);

        tcp_recved(socket->pcb, plen);
    }
    UNLOCK_TCPIP_CORE();

    *read_len = read;
    np_completion_event_resolve(completion_event, NABTO_EC_OK);
}

static void nplwip_tcp_shutdown(struct np_tcp_socket *socket)
{
    LOCK_TCPIP_CORE();
    err_t error = tcp_shutdown(socket->pcb, 0, 1);
    UNLOCK_TCPIP_CORE();
    if (error != ERR_OK)
    {
        NABTO_LOG_ERROR(TCP_LOG, "TCP socket shutdown failed for some reason.");
    }
}

// ---------------------
// Local IP
// ---------------------

static size_t nplwip_get_local_ips(struct np_local_ip *obj, struct np_ip_address *addrs, size_t addrs_size)
{
    UNUSED(obj);

    if (addrs_size > 1)
    {
        struct netif *netif = get_default_netif();
        const ip_addr_t *ip = netif_ip_addr4(netif);
        const ip_addr_t *ipv6 = netif_ip_addr6(netif, 0);
        nplwip_convertip_lwip_to_np(ip, addrs);
        return 1;
    }
    else
    {
        return 0;
    }
}


// ---------------------
// Public interface
// ---------------------

static struct np_dns_functions dns_module = {
    .async_resolve_v4 = nplwip_async_resolve_ipv4,
    .async_resolve_v6 = nplwip_async_resolve_ipv6
};

static struct np_udp_functions udp_module = {
    .create = nplwip_create_socket,
    .destroy = nplwip_destroy_socket,
    .abort = nplwip_abort_socket,
    .async_bind_port = nplwip_async_bind_port,
    .async_send_to = nplwip_async_sendto,
    .async_recv_wait = nplwip_async_recv_wait,
    .recv_from = nplwip_recv_from,
    .get_local_port = nplwip_get_local_port
};

static struct np_tcp_functions tcp_module = {
    .create = nplwip_tcp_create,
    .destroy = nplwip_tcp_destroy,
    .abort = nplwip_tcp_abort,
    .async_connect = nplwip_tcp_async_connect,
    .async_write = nplwip_tcp_async_write,
    .async_read = nplwip_tcp_async_read,
    .shutdown = nplwip_tcp_shutdown
};

static struct np_local_ip_functions local_ip_module = {
    .get_local_ips = nplwip_get_local_ips
};

struct np_dns nplwip_get_dns_impl()
{
    struct np_dns obj;
    obj.mptr = &dns_module;
    obj.data = NULL;
    return obj;
}

struct np_udp nplwip_get_udp_impl()
{
    struct np_udp obj;
    obj.mptr = &udp_module;
    obj.data = NULL;
    return obj;
}

struct np_tcp nplwip_get_tcp_impl()
{
    struct np_tcp obj;
    obj.mptr = &tcp_module;
    obj.data = NULL;
    return obj;
}

struct np_local_ip nplwip_get_local_ip_impl()
{
    struct np_local_ip obj;
    obj.mptr = &local_ip_module;
    obj.data = NULL;
    return obj;
}
