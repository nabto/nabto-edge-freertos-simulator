/*
 * Copyright (c) 2001-2003 Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 *
 * Author: Adam Dunkels <adam@sics.se>
 *
 */
#include "lwipcfg.h"
#if defined(USE_PCAPIF)

#include "lwip/debug.h"

#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <pcap.h>

#include "lwipcfg.h"
#include "netif/etharp.h"
#include "lwip/ethip6.h"

#include "lwip/stats.h"

#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/sys.h"

#include "lwip/ip.h"


#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <pthread.h>
#include <signal.h>


struct pcapif {
  pcap_t *pd;
  struct eth_addr *ethaddr;
  char errbuf[PCAP_ERRBUF_SIZE];

  // running pcap in the system context
  pthread_t pcapThread;

  // running lwip in the freertos context
  TaskHandle_t lwipThread;

  QueueHandle_t incomingPackets;
};

static void lwip_thread(void *arg);
static void* pcap_thread(void *arg);

/*-----------------------------------------------------------------------------------*/
static err_t
low_level_output(struct netif *netif, struct pbuf *p)
{
  struct pcapif* pcapif = netif->state;

  char buf[1518]; /* max packet size including VLAN excluding CRC */

  if (p->tot_len > sizeof(buf)) {
    //MIB2_STATS_NETIF_INC(netif, ifoutdiscards);
    perror("tapif: packet too large");
    return ERR_IF;
  }

  /* initiate transfer(); */
  pbuf_copy_partial(p, buf, p->tot_len, 0);
  pcap_inject(pcapif->pd, buf, p->tot_len);
}

/*-----------------------------------------------------------------------------------*/
err_t
pcapif_init(struct netif *netif)
{
  struct pcapif *p;

  p = malloc(sizeof(struct pcapif));
  if (p == NULL)
      return ERR_MEM;
  netif->state = p;
  netif->name[0] = 'p';
  netif->name[1] = 'c';
  netif->linkoutput = low_level_output;
  #if LWIP_IPV4
  netif->output = etharp_output;
  #endif /* LWIP_IPV4 */
  #if LWIP_IPV6
  netif->output_ip6 = ethip6_output;
  #endif /* LWIP_IPV6 */

  u8_t my_mac_addr[ETH_HWADDR_LEN] = LWIP_MAC_ADDR_BASE;
  SMEMCPY(&netif->hwaddr, my_mac_addr, ETH_HWADDR_LEN);
  netif->hwaddr_len = 6;

  netif->mtu = 1500;

  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_IGMP;

  const char* dev = PCAPIF;
  int promisciousMode = 1; // 0 == disabled
  p->pd = pcap_open_live(dev, BUFSIZ, promisciousMode, 0, p->errbuf);
  if (p->pd == NULL) {
    printf("pcapif_init: failed %s\n", p->errbuf);
    return ERR_IF;
  }

  p->incomingPackets = xQueueCreate(100, sizeof(struct pbuf*));

  pthread_t isrThread;
  pthread_create(&p->pcapThread, NULL, pcap_thread, netif);
  if (xTaskCreate(lwip_thread, "freertos_lwip_pcapif_thread", DEFAULT_THREAD_STACKSIZE,
                  netif, DEFAULT_THREAD_PRIO, &p->lwipThread) != pdPASS)
  {
    perror("could not create thread freertos_tapif_thread");
  }
  netif_set_link_up(netif);
  return ERR_OK;
}

/*-----------------------------------------------------------------------------------*/

static int
pcaipf_is_tx_packet(struct netif *netif, const void *packet, int packet_len)
{
  const struct eth_addr *src = (const struct eth_addr *)packet + 1;
  if (packet_len >= (ETH_HWADDR_LEN * 2)) {
    /* Don't let feedback packets through (limitation in winpcap?) */
    if(!memcmp(src, netif->hwaddr, ETH_HWADDR_LEN)) {
      return 1;
    }
  }
  return 0;
}

/** low_level_input(): Allocate a pbuf and transfer the bytes of the incoming
 * packet from the interface into the pbuf.
 */
static struct pbuf *
pcapif_low_level_input(struct netif *netif, const void *packet, int packet_len)
{
  struct pbuf *p, *q;
  int start;
  int length = packet_len;
  const struct eth_addr *dest = (const struct eth_addr*)packet;
  int unicast;
#if PCAPIF_FILTER_GROUP_ADDRESSES && !PCAPIF_RECEIVE_PROMISCUOUS
  const u8_t bcast[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  const u8_t ipv4mcast[] = {0x01, 0x00, 0x5e};
  const u8_t ipv6mcast[] = {0x33, 0x33};
#endif /* PCAPIF_FILTER_GROUP_ADDRESSES && !PCAPIF_RECEIVE_PROMISCUOUS */

  if (pcaipf_is_tx_packet(netif, packet, packet_len)) {
    /* don't update counters here! */
    return NULL;
  }

  unicast = ((dest->addr[0] & 0x01) == 0);
#if !PCAPIF_RECEIVE_PROMISCUOUS
  /* MAC filter: only let my MAC or non-unicast through (pcap receives loopback traffic, too) */
  if (memcmp(dest, &netif->hwaddr, ETH_HWADDR_LEN) &&
#if PCAPIF_FILTER_GROUP_ADDRESSES
    (memcmp(dest, ipv4mcast, 3) || ((dest->addr[3] & 0x80) != 0)) &&
    memcmp(dest, ipv6mcast, 2) &&
    memcmp(dest, bcast, 6)
#else /* PCAPIF_FILTER_GROUP_ADDRESSES */
     unicast
#endif /* PCAPIF_FILTER_GROUP_ADDRESSES */
    ) {
    /* don't update counters here! */
    return NULL;
  }
#endif /* !PCAPIF_RECEIVE_PROMISCUOUS */

  /* We allocate a pbuf chain of pbufs from the pool. */
  p = pbuf_alloc(PBUF_RAW, (u16_t)length + ETH_PAD_SIZE, PBUF_POOL);
  LWIP_DEBUGF(NETIF_DEBUG, ("netif: recv length %i p->tot_len %i\n", length, (int)p->tot_len));

  if (p != NULL) {
    /* We iterate over the pbuf chain until we have read the entire
       packet into the pbuf. */
    start = 0;
    for (q = p; q != NULL; q = q->next) {
      u16_t copy_len = q->len;
      /* Read enough bytes to fill this pbuf in the chain. The
         available data in the pbuf is given by the q->len
         variable. */
      /* read data into(q->payload, q->len); */
      LWIP_DEBUGF(NETIF_DEBUG, ("netif: recv start %i length %i q->payload %p q->len %i q->next %p\n", start, length, q->payload, (int)q->len, (void*)q->next));
      if (q == p) {
#if ETH_PAD_SIZE
        LWIP_ASSERT("q->len >= ETH_PAD_SIZE", q->len >= ETH_PAD_SIZE);
        copy_len -= ETH_PAD_SIZE;
#endif /* ETH_PAD_SIZE*/
        MEMCPY(&((char*)q->payload)[ETH_PAD_SIZE], &((const char*)packet)[start], copy_len);
      } else {
        MEMCPY(q->payload, &((const char*)packet)[start], copy_len);
      }
      start += copy_len;
      length -= copy_len;
      if (length <= 0) {
        break;
      }
    }
    LINK_STATS_INC(link.recv);
  } else {
  }

  return p;
}

static void
pcapif_input(struct netif* netif, const struct pcap_pkthdr *pkt_header, const u_char *packet)
{
  struct pcapif *pa = (struct pcapif*)netif->state;
  int packet_len = pkt_header->caplen;
  struct pbuf *p;

  /* move received packet into a new pbuf */
  p = pcapif_low_level_input(netif, packet, packet_len);
  /* if no packet could be read, silently ignore this */
  if (p != NULL) {
    /* pass all packets to ethernet_input, which decides what packets it supports */
    BaseType_t xHigherPriorityTaskWoken;
    BaseType_t result = xQueueSendFromISR(pa->incomingPackets, &p, &xHigherPriorityTaskWoken);

    if (result != pdTRUE)
    {
      // queue full
      pbuf_free(p);
    }
  }
}

static void* pcap_thread(void *arg)
{
  struct netif *netif;
  struct pcapif *pcapif;
  netif = arg;
  pcapif = netif->state;

  while (1) {
    sigset_t set;
    sigfillset(&set);

    pthread_sigmask(SIG_SETMASK, &set, NULL);

    struct pcap_pkthdr h;
    const u_char* packet = pcap_next(pcapif->pd, &h);
    if (packet != NULL) {
      pcapif_input(netif, &h, packet);
    }
  }
}


static void lwip_thread(void *arg)
{
  struct netif *netif;
  struct pcapif *pcapif;

  netif = (struct netif *)arg;
  pcapif = (struct pcapif *)netif->state;

  while (1)
  {
    struct pbuf *buf;
    if (xQueueReceive(pcapif->incomingPackets,
                      &buf,
                      portMAX_DELAY) == pdPASS)
    {
      if (netif->input(buf, netif) != ERR_OK)
      {
        LWIP_DEBUGF(NETIF_DEBUG, ("tapif_input: netif input error\n"));
        pbuf_free(buf);
      }
    }
  }
}

#endif //defined(USE_PCAPIF)