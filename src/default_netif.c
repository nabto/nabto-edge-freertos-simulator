#include "lwipcfg.h"
#include "lwip/opt.h"
#include "lwip/netif.h"
#include "lwip/ip_addr.h"
#include "lwip/tcpip.h"
#include "netif/tapif.h"
#include "netif/pcapif.h"

#include "default_netif.h"

static struct netif netif;

#if LWIP_IPV4
#define NETIF_ADDRS ipaddr, netmask, gw
void init_default_netif(const ip4_addr_t *ipaddr, const ip4_addr_t *netmask, const ip4_addr_t *gw)
#else
#define NETIF_ADDRS
void init_default_netif(void)
#endif
{
#if USE_TAPIF
    netif_add(&netif, NETIF_ADDRS, NULL, tapif_init, tcpip_input);
#elif USE_PCAPIF
    netif_add(&netif, NETIF_ADDRS, NULL, pcapif_init, tcpip_input);
#else
#error use either TAPIF or PCAPIF
#endif
    netif_set_default(&netif);
}

struct netif* get_default_netif(void)
{
    return &netif;
}

void default_netif_shutdown(void)
{
}
