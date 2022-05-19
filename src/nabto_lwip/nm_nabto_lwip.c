#include "nm_nabto_lwip.h"
#include "nm_nabto_lwip_util.h"

#include <string.h>

#include <lwip/dns.h>
#include <lwip/udp.h>
#include <lwip/tcp.h>
#include <lwip/netif.h>
#include <lwip/apps/mdns.h>
#include <lwip/tcpip.h>

#include <platform/interfaces/np_dns.h>
#include <platform/interfaces/np_udp.h>
#include <platform/interfaces/np_tcp.h>
#include <platform/interfaces/np_mdns.h>

#include <platform/np_types.h>
#include <platform/np_error_code.h>
#include <platform/np_completion_event.h>
#include <platform/np_logging.h>
#include <platform/np_allocator.h>

#include <nn/string_set.h>
#include <nn/string_map.h>

//#include "common.h"
//#include "default_netif.h"

#define DNS_LOG  NABTO_LOG_MODULE_DNS
#define UDP_LOG  NABTO_LOG_MODULE_UDP
#define TCP_LOG  NABTO_LOG_MODULE_TCP
#define MDNS_LOG NABTO_LOG_MODULE_MDNS

#define UNUSED(x) (void)(x)

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

// ---------------------
// DNS
// ---------------------

static void nm_lwip_dns_resolve_callback(const char *name, const ip_addr_t *addr, void *arg)
{
    NABTO_LOG_INFO(DNS_LOG, "DNS resolved %s to %s", name, ipaddr_ntoa(addr));
    dns_resolve_event *event = (dns_resolve_event*)arg;
    if (addr && addr->type == event->addr_type && event->ips_size >= 1)
    {
        // @TODO: Currently we only resolve to one ip, which might be incorrect.
        *event->ips_resolved = 1;
        nm_lwip_convertip_lwip_to_np(addr, &event->ips[0]);
        np_completion_event_resolve(event->completion_event, NABTO_EC_OK);
    }
    else
    {
        np_completion_event_resolve(event->completion_event, NABTO_EC_UNKNOWN);
    }
}

static void nm_lwip_async_resolve(struct np_dns *obj, const char *host,
                                 struct np_ip_address *ips,
                                 size_t ips_size, size_t *ips_resolved,
                                 struct np_completion_event *completion_event,
                                 int addr_type)
{
    UNUSED(obj);
    dns_resolve_event *event = np_calloc(1, sizeof(dns_resolve_event));
    event->ips_size = ips_size;
    event->ips_resolved = ips_resolved;
    event->ips = ips;
    event->completion_event = completion_event;
    event->addr_type = addr_type;
    u8_t dns_addrtype = addr_type == IPADDR_TYPE_V4 ? LWIP_DNS_ADDRTYPE_IPV4 : LWIP_DNS_ADDRTYPE_IPV6;

    LOCK_TCPIP_CORE();
    struct ip_addr resolved;
    err_t Error = dns_gethostbyname_addrtype(host, &resolved,
                                             nm_lwip_dns_resolve_callback, event,
                                             dns_addrtype);
    UNLOCK_TCPIP_CORE();

    switch (Error)
    {
        case ERR_OK:
        {
            NABTO_LOG_INFO(DNS_LOG, "DNS resolved %s to %s", host, ipaddr_ntoa(&resolved));
            np_free(event);
            *ips_resolved = 1;
            nm_lwip_convertip_lwip_to_np(&resolved, &ips[0]);
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

static void nm_lwip_async_resolve_ipv4(struct np_dns *obj, const char *host,
                                      struct np_ip_address *ips,
                                      size_t ips_size, size_t *ips_resolved,
                                      struct np_completion_event *completion_event)
{
    nm_lwip_async_resolve(obj, host, ips, ips_size, ips_resolved, completion_event, IPADDR_TYPE_V4);
}

static void nm_lwip_async_resolve_ipv6(struct np_dns *obj, const char *host,
                                      struct np_ip_address *ips,
                                      size_t ips_size, size_t *ips_resolved,
                                      struct np_completion_event *completion_event)
{
    nm_lwip_async_resolve(obj, host, ips, ips_size, ips_resolved, completion_event, IPADDR_TYPE_V6);
}

// ---------------------
// UDP
// ---------------------

static void nm_lwip_udp_callback(void *arg, struct udp_pcb *upcb, struct pbuf *p,
                                const ip_addr_t *addr, u16_t port)
{
    UNUSED(upcb);
    NABTO_LOG_INFO(UDP_LOG, "Received UDP packet from %s:%i size %d", ip_ntoa(addr), port, p->tot_len);
    struct np_udp_socket *socket = (struct np_udp_socket*)arg;
    if (p != NULL && socket->packet == NULL)
    {
        socket->packet = p;
        nm_lwip_convertip_lwip_to_np(addr, &socket->addr);
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

static np_error_code nm_lwip_create_socket(struct np_udp *obj, struct np_udp_socket **out_socket)
{
    UNUSED(obj);

    struct np_udp_socket *socket = np_calloc(1, sizeof(struct np_udp_socket));
    if (socket == NULL)
    {
        return NABTO_EC_OUT_OF_MEMORY;
    }

    LOCK_TCPIP_CORE();
    socket->upcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    UNLOCK_TCPIP_CORE();
    if (socket->upcb == NULL) {
        np_free(socket);
        return NABTO_EC_OUT_OF_MEMORY;
    }

    // @TODO: Check if socket->upcb is valid.

    socket->aborted = false;
    socket->packet = NULL;
    socket->ce = NULL;

    *out_socket = socket;
    return NABTO_EC_OK;
}

static void nm_lwip_abort_socket(struct np_udp_socket *socket)
{
    socket->aborted = true;
}

static void nm_lwip_destroy_socket(struct np_udp_socket *socket)
{
    if (socket == NULL)
    {
        NABTO_LOG_ERROR(UDP_LOG, "Socket destroyed twice.");
        return;
    }

    nm_lwip_abort_socket(socket);

    LOCK_TCPIP_CORE();
    udp_remove(socket->upcb);
    np_free(socket);
    UNLOCK_TCPIP_CORE();
}

static void nm_lwip_async_bind_port(struct np_udp_socket *socket, uint16_t port,
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
            udp_recv(socket->upcb, nm_lwip_udp_callback, socket);
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

static np_error_code nm_lwip_async_sendto_ec(struct np_udp_socket *socket, struct np_udp_endpoint *ep,
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
    nm_lwip_convertip_np_to_lwip(&ep->ip, &ip);

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

static void nm_lwip_async_sendto(struct np_udp_socket *socket, struct np_udp_endpoint *ep,
                                uint8_t *buffer, uint16_t buffer_size,
                                struct np_completion_event *completion_event)
{
    UNUSED(completion_event);
    np_error_code ec = nm_lwip_async_sendto_ec(socket, ep, buffer, buffer_size);
    np_completion_event_resolve(completion_event, ec);
}

static void nm_lwip_async_recv_wait(struct np_udp_socket *socket, struct np_completion_event *completion_event)
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

static np_error_code nm_lwip_recv_from(struct np_udp_socket *socket, struct np_udp_endpoint *ep,
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

static uint16_t nm_lwip_get_local_port(struct np_udp_socket *socket)
{
    return socket->upcb->local_port;
}

// ---------------------
// Local IP
// ---------------------

static size_t nm_lwip_get_local_ips(struct np_local_ip *obj, struct np_ip_address *addrs, size_t addrs_size)
{
    if (addrs_size > 1)
    {
        struct netif *netif = (struct netif*)(obj->data);
        const ip_addr_t *ip = netif_ip_addr4(netif);
        const ip_addr_t *ipv6 = netif_ip_addr6(netif, 0);
        nm_lwip_convertip_lwip_to_np(ip, addrs);
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
    .async_resolve_v4 = nm_lwip_async_resolve_ipv4,
    .async_resolve_v6 = nm_lwip_async_resolve_ipv6
};

static struct np_udp_functions udp_module = {
    .create = nm_lwip_create_socket,
    .destroy = nm_lwip_destroy_socket,
    .abort = nm_lwip_abort_socket,
    .async_bind_port = nm_lwip_async_bind_port,
    .async_send_to = nm_lwip_async_sendto,
    .async_recv_wait = nm_lwip_async_recv_wait,
    .recv_from = nm_lwip_recv_from,
    .get_local_port = nm_lwip_get_local_port
};

static struct np_local_ip_functions local_ip_module = {
    .get_local_ips = nm_lwip_get_local_ips
};

struct np_dns nm_lwip_get_dns_impl()
{
    struct np_dns obj;
    obj.mptr = &dns_module;
    obj.data = NULL;
    return obj;
}

struct np_udp nm_lwip_get_udp_impl()
{
    struct np_udp obj;
    obj.mptr = &udp_module;
    obj.data = NULL;
    return obj;
}

struct np_local_ip nm_lwip_get_local_ip_impl(struct netif* netif)
{
    struct np_local_ip obj;
    obj.mptr = &local_ip_module;
    obj.data = netif;
    return obj;
}
