#ifndef _NM_NABTO_LWIP_UTIL_H_
#define _NM_NABTO_LWIP_UTIL_H_

#include <lwip/ip.h>
#include <platform/np_ip_address.h>

void nplwip_convertip_np_to_lwip(const struct np_ip_address *from, ip_addr_t *to);
void nplwip_convertip_lwip_to_np(const ip_addr_t *from, struct np_ip_address *to);

#endif
