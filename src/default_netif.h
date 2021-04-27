#ifndef LWIP_DEFAULT_NETIF_H
#define LWIP_DEFAULT_NETIF_H

#include "lwip/ip_addr.h"

#if LWIP_IPV4
void init_default_netif(const ip4_addr_t *ipaddr, const ip4_addr_t *netmask, const ip4_addr_t *gw);
#else
void init_default_netif(void);
#endif

void default_netif_poll(void);
void default_netif_shutdown(void);

#endif
