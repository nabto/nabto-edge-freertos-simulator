#include "lwip_port_init.h"
#include "lwipcfg.h"
#include "lwipopts.h"

#include "lwip/netif.h"
#include "lwip/sys.h"
#include "lwip/timeouts.h"
#include "lwip/debug.h"
#include "lwip/stats.h"
#include "lwip/init.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/api.h"
#include "lwip/etharp.h"
#include "lwip/dns.h"
#include "lwip/udp.h"
#include "lwip/dhcp.h"
#include "lwip/apps/mdns.h"
#include "netif/ethernet.h"

#include "console.h"
#include "default_netif.h"

#include <stdlib.h>
#include <time.h>

static void LWIPStatusCallback(struct netif *state_netif)
{
    if (netif_is_up(state_netif))
    {
        console_print("status_callback==UP, local interface IP is %s\n",
                      ip4addr_ntoa(netif_ip4_addr(state_netif)));
    }
    else
    {
        console_print("status_callback==DOWN\n");
    }
}

void lwip_port_init()
{
    tcpip_init(NULL, NULL);
    srand((unsigned int)time(0));

    ip4_addr_t ipaddr, netmask, gw;

    // @TODO: Allow using DHCP to get an address instead?
    ip4_addr_set_zero(&gw);
    ip4_addr_set_zero(&ipaddr);
    ip4_addr_set_zero(&netmask);
    LWIP_PORT_INIT_GW(&gw);
    LWIP_PORT_INIT_IPADDR(&ipaddr);
    LWIP_PORT_INIT_NETMASK(&netmask);


    init_default_netif(&ipaddr, &netmask, &gw);
    netif_create_ip6_linklocal_address(netif_default, 1);



    console_print("Starting lwIP, local interface IP is %s\n", ip4addr_ntoa(&ipaddr));
    console_print("ip6 linklocal address: %s\n", ip6addr_ntoa(netif_ip6_addr(netif_default, 0)));

    netif_set_status_callback(netif_default, LWIPStatusCallback);
    netif_set_up(netif_default);
    //dhcp_start(netif_default);

    // @TODO: Using google dns for now, should probably be exposed as an option instead.
    ip_addr_t dnsserver;
    IP_ADDR4(&dnsserver, 8,8,8,8);
    dns_setserver(0, &dnsserver);
    IP_ADDR4(&dnsserver, 8,8,4,4);
    dns_setserver(1, &dnsserver);

    //mdns_resp_register_name_result_cb(lwip_mdns_report);
    //mdns_resp_init();
    //mdns_resp_add_netif(netif_default, "lwip", 3600);
    //mdns_resp_announce(netif_default);
}