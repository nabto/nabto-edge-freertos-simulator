#ifndef NABTO_LWIP_H
#define NABTO_LWIP_H

#include <platform/np_platform.h>

struct np_dns nplwip_get_dns_impl();
struct np_udp nplwip_get_udp_impl();
struct np_tcp nplwip_get_tcp_impl();
struct np_local_ip nplwip_get_local_ip_impl();
struct np_mdns nplwip_get_mdns_impl();

#endif /* NABTO_LWIP_H */

