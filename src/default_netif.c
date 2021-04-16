#include "lwip/opt.h"
#include "lwip/netif.h"
#include "lwip/ip_addr.h"
#include "lwip/tcpip.h"
#include "netif/tapif.h"

static struct netif netif;

#if LWIP_IPV4
#define NETIF_ADDRS ipaddr, netmask, gw
void init_default_netif(const ip4_addr_t *ipaddr, const ip4_addr_t *netmask, const ip4_addr_t *gw)
#else
#define NETIF_ADDRS
void init_default_netif(void)
#endif
{
#if NO_SYS
    netif_add(&netif, NETIF_ADDRS, NULL, tapif_init, netif_input);
#else
    netif_add(&netif, NETIF_ADDRS, NULL, tapif_init, tcpip_input);
#endif
    netif_set_default(&netif);
}

void default_netif_poll(void)
{
    tapif_poll(&netif);
}

void default_netif_shutdown(void)
{
}
