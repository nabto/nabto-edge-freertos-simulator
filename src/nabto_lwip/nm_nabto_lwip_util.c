#include "nm_nabto_lwip_util.h"

#include <string.h>

void nm_lwip_convertip_np_to_lwip(const struct np_ip_address *from, ip_addr_t *to)
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

void nm_lwip_convertip_lwip_to_np(const ip_addr_t *from, struct np_ip_address *to)
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
