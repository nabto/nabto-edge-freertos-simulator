#ifndef _NM_MDNS_LWIP_H_
#define _NM_MDNS_LWIP_H_

#include <nn/llist.h>

#include <platform/interfaces/np_event_queue.h>
#include <platform/interfaces/np_local_ip.h>

#include <mdns/mdns_server.h>


#define NM_MDNS_LWIP_MAX_LOCAL_IPS 2


struct nm_mdns_lwip_netif {
    struct nn_llist_node listNode;
    struct netif* netif;
};

struct nm_mdns_lwip {
    struct udp_pcb *socket;
    struct nn_llist netifList;
    struct nabto_mdns_server_context mdnsServer;
    struct nn_ip_address localIps[NM_MDNS_LWIP_MAX_LOCAL_IPS];
    size_t localIpsSize;
    struct np_local_ip localIp;
    uint16_t port;
};

np_error_code nm_mdns_lwip_init(struct nm_mdns_lwip* ctx, struct np_event_queue* eq, struct np_local_ip* localIp);
void nm_mdns_lwip_deinit(struct nm_mdns_lwip* ctx);

void nm_mdns_lwip_add_netif(struct nm_mdns_lwip* ctx, struct netif* netif);
void nm_mdns_lwip_remove_netif(struct nm_mdns_lwip* ctx, struct netif* netif);

struct np_mdns nm_mdns_lwip_get_impl(struct nm_mdns_lwip* ctx);



#endif
