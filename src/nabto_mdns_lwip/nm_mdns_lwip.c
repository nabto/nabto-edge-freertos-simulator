#include "nm_mdns_lwip.h"

#include <platform/np_local_ip_wrapper.h>
#include <platform/interfaces/np_mdns.h>

#include <lwip/igmp.h>
#include <lwip/ip_addr.h>
#include <lwip/prot/dns.h>
#include <lwip/udp.h>

#if LWIP_IPV4
#include "lwip/igmp.h"
/* IPv4 multicast group 224.0.0.251 */
static const ip_addr_t v4group = DNS_MQUERY_IPV4_GROUP_INIT;
#endif

#if LWIP_IPV6
#include "lwip/mld6.h"
/* IPv6 multicast group FF02::FB */
static const ip_addr_t v6group = DNS_MQUERY_IPV6_GROUP_INIT;
#endif

static const uint16_t mdnsPort = 5353;

static void leave_groups(struct netif* netif);
static np_error_code join_groups(struct netif* netif);
static void send_packet(struct nm_mdns_lwip* ctx, uint16_t id, bool unicastResponse, bool goodbye, const ip_addr_t* dstIp, const uint16_t dstPort,
                 struct netif* netif);
static void start_recv(struct nm_mdns_lwip* ctx);
static void update_local_ips(struct nm_mdns_lwip* mdns);

static void publish_service(struct np_mdns* obj, uint16_t port, const char* instanceName, struct nn_string_set* subtypes, struct nn_string_map* txtItems);
static void unpublish_service(struct np_mdns* obj);
static void announce(struct nm_mdns_lwip* ctx, bool goodbye);


static struct np_mdns_functions module = {
    .publish_service = publish_service,
    .unpublish_service = unpublish_service
};

struct np_mdns nm_mdns_lwip_get_impl(struct nm_mdns_lwip* ctx)
{
    struct np_mdns obj;
    obj.mptr = &module;
    obj.data = ctx;
    return obj;
}

np_error_code nm_mdns_lwip_init(struct nm_mdns_lwip* ctx,
                                struct np_event_queue* eq,
                                struct np_local_ip* localIp)
{
    nn_llist_init(&ctx->netifList);
    ctx->port = 0;
    ctx->localIpsSize = 0;
    ctx->localIp = *localIp;
    nabto_mdns_server_init(&ctx->mdnsServer);
    ctx->socket = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (ctx->socket == NULL) {
        return NABTO_EC_OUT_OF_MEMORY;
    }
    err_t err = udp_bind(ctx->socket, IP4_ADDR_ANY, 5353);
    if (err != ERR_OK) {
        return NABTO_EC_UDP_SOCKET_CREATION_ERROR;
    }

    start_recv(ctx);
    return NABTO_EC_OK;
}

void nm_mdns_lwip_add_netif(struct nm_mdns_lwip* ctx, struct netif* netif)
{
    struct nn_llist_node* n =
        (struct nn_llist_node*)calloc(1, sizeof(struct nn_llist_node));

    if (join_groups(netif) == NABTO_EC_OK) {
        nn_llist_append(&ctx->netifList, n, netif);
    } else {
        free(n);
    }
}

void nm_mdns_lwip_remove_netif(struct nm_mdns_lwip* ctx, struct netif* netif)
{
    struct nn_llist_iterator it;
    for (it = nn_llist_begin(&ctx->netifList); !nn_llist_is_end(&it);
         nn_llist_next(&it)) {
        struct netif* n = nn_llist_get_item(&it);
        if (n == netif) {
            struct nn_llist_node* tmp = it.node;
            nn_llist_erase(&it);
            free(tmp);

            leave_groups(netif);
            return;
        }
    }
}

void leave_groups(struct netif* netif)
{
#if LWIP_IPV4
    igmp_leavegroup_netif(netif, ip_2_ip4(&v4group));
#endif
#if LWIP_IPV6
    mld6_leavegroup_netif(netif, ip_2_ip6(&v6group));
#endif
}

np_error_code join_groups(struct netif* netif)
{
    int res;
#if LWIP_IPV4

    res = igmp_joingroup_netif(netif, ip_2_ip4(&v4group));
    if (res != ERR_OK) {
        return NABTO_EC_UNKNOWN;
    }
#endif
#if LWIP_IPV6
    res = mld6_joingroup_netif(netif, ip_2_ip6(&v6group));
    if (res != ERR_OK) {
        return NABTO_EC_UNKNOWN;
    }
#endif

    return NABTO_EC_OK;
}

void packet_received(void* userData, struct udp_pcb* pcb, struct pbuf* p,
                     const ip_addr_t* addr, u16_t port)
{
    struct nm_mdns_lwip* ctx = userData;
    struct netif* recv_netif = ip_current_input_netif();
    uint16_t id;

    uint8_t buffer[1500];

    uint16_t copied = pbuf_copy_partial(p, buffer, 1500, 0);
    if (copied > 0) {
        if (nabto_mdns_server_handle_packet(&ctx->mdnsServer, buffer, copied,
                                            &id)) {
            bool unicastResponse = false;
            if (port != 5353) {
                unicastResponse = true;
            }
            send_packet(ctx, id, unicastResponse, false /*goodbye*/, addr, port, recv_netif);
        }
    }
    start_recv(ctx);
}

void send_packet(struct nm_mdns_lwip* ctx, uint16_t id, bool unicastResponse, bool goodbye, const ip_addr_t* dstIp, const uint16_t dstPort,
                 struct netif* netif)
{
    uint16_t port = ctx->port;
    update_local_ips(ctx);

    struct pbuf* buf = NULL;

    if (port > 0) {
        uint8_t buffer[1500];
        size_t written;
        if (nabto_mdns_server_build_packet(
                &ctx->mdnsServer, id, unicastResponse, false, ctx->localIps,
                ctx->localIpsSize, port, buffer, 1500, &written)) {
            buf = pbuf_alloc(PBUF_TRANSPORT, written, PBUF_RAM);
            if (buf != NULL) {
                if (pbuf_take(buf, buffer, written) == ERR_OK) {
                    udp_sendto_if(ctx->socket, buf, dstIp, dstPort, netif);
                }
            }
        }
    }
    if (buf != NULL) {
        pbuf_free(buf);
    }
}

void start_recv(struct nm_mdns_lwip* ctx)
{
    udp_recv(ctx->socket, packet_received, ctx);
}

void update_local_ips(struct nm_mdns_lwip* mdns)
{
    struct np_ip_address ips[NM_MDNS_LWIP_MAX_LOCAL_IPS];
    size_t ipsFound = np_local_ip_get_local_ips(&mdns->localIp, ips, NM_MDNS_LWIP_MAX_LOCAL_IPS);

    mdns->localIpsSize = ipsFound;
    for(int i = 0; i < ipsFound; i++) {
        struct np_ip_address* ip = &ips[i];
        struct nabto_mdns_ip_address* mdnsIp = &mdns->localIps[i];
        if (ip->type == NABTO_IPV4) {
            mdnsIp->type = NABTO_MDNS_IPV4;
            memcpy(mdnsIp->v4.addr, ip->ip.v4, 4);
        } else {
            mdnsIp->type = NABTO_MDNS_IPV6;
            memcpy(mdnsIp->v6.addr, ip->ip.v6, 16);
        }
    }
}

void publish_service(struct np_mdns* obj, uint16_t port, const char* instanceName, struct nn_string_set* subtypes, struct nn_string_map* txtItems)
{
    struct nm_mdns_lwip* ctx = obj->data;

    ctx->port = port;
    nabto_mdns_server_update_info(&ctx->mdnsServer, instanceName, subtypes, txtItems);
    announce(ctx, false);
}

void unpublish_service(struct np_mdns* obj)
{
    // do nothing
}

void announce(struct nm_mdns_lwip* ctx, bool goodbye)
{
    struct netif* netif;
    NN_LLIST_FOREACH(netif, &ctx->netifList) {
        send_packet(ctx, 0, false /*unicast response*/, goodbye, &v4group, mdnsPort, netif);
        send_packet(ctx, 0, false /*unicast response*/, goodbye, &v6group, mdnsPort, netif);
    }
}
