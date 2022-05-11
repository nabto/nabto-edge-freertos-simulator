#ifndef NABTO_LWIP_H
#define NABTO_LWIP_H

#include <platform/np_platform.h>
#include <lwip/netif.h>

struct np_dns nm_lwip_get_dns_impl();
struct np_udp nm_lwip_get_udp_impl();
struct np_tcp nm_lwip_get_tcp_impl();
struct np_local_ip nm_lwip_get_local_ip_impl(struct netif* netif);

#endif /* NABTO_LWIP_H */
