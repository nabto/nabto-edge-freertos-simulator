
#include "lwip/debug.h"
#include "lwip/def.h"
#include "lwip/sys.h"
#include "lwip/mem.h"
#include "lwip/stats.h"
#include "lwip/tcpip.h"

#include <stdlib.h>

u32_t
lwip_port_rand(void)
{
  return (u32_t)rand();
}