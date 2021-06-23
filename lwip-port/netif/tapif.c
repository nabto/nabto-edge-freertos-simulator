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

#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/socket.h>

#include "lwip/opt.h"

#include "lwip/debug.h"
#include "lwip/def.h"
#include "lwip/ip.h"
#include "lwip/mem.h"
#include "lwip/stats.h"
#include "lwip/snmp.h"
#include "lwip/pbuf.h"
#include "lwip/sys.h"
#include "lwip/timeouts.h"
#include "netif/etharp.h"
#include "lwip/ethip6.h"

#include "netif/tapif.h"
#include "netif/list.h"

#include <signal.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include <pthread.h>
#include <utils/wait_for_event.h>

#if defined(LWIP_UNIX_LINUX)
#include <sys/ioctl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
/*
 * Creating a tap interface requires special privileges. If the interfaces
 * is created in advance with `tunctl -u <user>` it can be opened as a regular
 * user. The network must already be configured. If DEVTAP_IF is defined it
 * will be opened instead of creating a new tap device.
 *
 * You can also use PRECONFIGURED_TAPIF environment variable to do so.
 */
#ifndef DEVTAP_DEFAULT_IF
#define DEVTAP_DEFAULT_IF "tap0"
#endif
#ifndef DEVTAP
#define DEVTAP "/dev/net/tun"
#endif
#elif defined(LWIP_UNIX_OPENBSD)
#define DEVTAP "/dev/tun0"
#else /* others */
#define DEVTAP "/dev/tap0"
#endif

/* Define those to better describe your network interface. */
#define IFNAME0 't'
#define IFNAME1 'p'

#ifndef TAPIF_DEBUG
#define TAPIF_DEBUG LWIP_DBG_OFF
#endif

struct tapif {
  /* Add whatever per-interface state that is needed here. */
  int fd;
  pthread_t inThread;
  pthread_t outThread;
  struct list* inList;
  struct list* outList;
  TaskHandle_t freeRTOSThread;
  struct event* outEvent;
};

#define MAX_PACKET_LENGTH 1518

struct packet {
  uint8_t data[MAX_PACKET_LENGTH];
  size_t dataLength;
};

/* Forward declarations. */
static void tapif_input(struct netif *netif);

static void freertos_thread(void *arg);
static void* tapif_out_thread(void* arg);
static void* tapif_in_thread(void* arg);

static bool try_send_packet(struct tapif* tapif);

/*-----------------------------------------------------------------------------------*/
static void
low_level_init(struct netif *netif)
{
  struct tapif *tapif;
#if LWIP_IPV4
  int ret;
  char buf[1024];
#endif /* LWIP_IPV4 */

  tapif = (struct tapif *)netif->state;

  /* Obtain MAC address from network interface. */

  /* (We just fake an address...) */
  netif->hwaddr[0] = 0x02;
  netif->hwaddr[1] = 0x12;
  netif->hwaddr[2] = 0x34;
  netif->hwaddr[3] = 0x56;
  netif->hwaddr[4] = 0x78;
  netif->hwaddr[5] = 0xab;
  netif->hwaddr_len = 6;

  /* device capabilities */
  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_IGMP;

  tapif->fd = open(DEVTAP, O_RDWR);
  LWIP_DEBUGF(TAPIF_DEBUG, ("tapif_init: fd %d\n", tapif->fd));
  if (tapif->fd == -1) {
#ifdef LWIP_UNIX_LINUX
    perror("tapif_init: try running \"modprobe tun\" or rebuilding your kernel with CONFIG_TUN; cannot open "DEVTAP);
#else /* LWIP_UNIX_LINUX */
    perror("tapif_init: cannot open "DEVTAP);
#endif /* LWIP_UNIX_LINUX */
    exit(1);
  }

#ifdef LWIP_UNIX_LINUX
  {
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));

    strncpy(ifr.ifr_name, DEVTAP_DEFAULT_IF, sizeof(ifr.ifr_name));
    ifr.ifr_name[sizeof(ifr.ifr_name)-1] = 0; /* ensure \0 termination */

    ifr.ifr_flags = IFF_TAP|IFF_NO_PI;
    if (ioctl(tapif->fd, TUNSETIFF, (void *) &ifr) < 0) {
      perror("tapif_init: "DEVTAP" ioctl TUNSETIFF");
      exit(1);
    }
  }
#endif /* LWIP_UNIX_LINUX */

  netif_set_link_up(netif);

  // create two threads one running select and waiting for data on the tap
  // interface, one thread which reads the data and executes the lwip function.

  // The start condition is that the task is blocked

  tapif->inList = list_new(42);
  tapif->outList = list_new(42);
  tapif->outEvent = event_create();

  pthread_create(&tapif->inThread, NULL, tapif_in_thread, netif);
  pthread_create(&tapif->outThread, NULL, tapif_out_thread, netif);
  if (xTaskCreate(freertos_thread, "freertos_tapif_thread", DEFAULT_THREAD_STACKSIZE,
                  netif, DEFAULT_THREAD_PRIO, &tapif->freeRTOSThread) != pdPASS)
  {
    perror("could not create thread freertos_tapif_thread");
  }
}
/*-----------------------------------------------------------------------------------*/
/*
 * low_level_output():
 *
 * Should do the actual transmission of the packet. The packet is
 * contained in the pbuf that is passed to the function. This pbuf
 * might be chained.
 *
 */
/*-----------------------------------------------------------------------------------*/

static err_t
low_level_output(struct netif *netif, struct pbuf *p)
{
  struct tapif *tapif = (struct tapif *)netif->state;

  struct packet* packet = malloc(sizeof(struct packet));
  if (p->tot_len > MAX_PACKET_LENGTH) {
    return ERR_MEM;
  }
  uint8_t* ptr = packet->data;
  packet->dataLength = p->tot_len;
  while(p != NULL) {
    memcpy(ptr, p->payload, p->len);
    ptr += p->len;
    p = p->next;
  }

  if (list_push(tapif->outList, packet) == 0) {
    free(packet);
    return ERR_MEM;
  } else {
    event_signal(tapif->outEvent);
  }
  return ERR_OK;
}
/*-----------------------------------------------------------------------------------*/
/*
 * low_level_input():
 *
 * Should allocate a pbuf and transfer the bytes of the incoming
 * packet from the interface into the pbuf.
 *
 */
/*-----------------------------------------------------------------------------------*/
static struct packet*
low_level_input(struct netif *netif)
{
  struct packet *p = malloc(sizeof(struct packet));
  if (p == NULL) {
    return p;
  }
  u16_t len;
  ssize_t readlen;
  char buf[1518]; /* max packet size including VLAN excluding CRC */
  struct tapif *tapif = (struct tapif *)netif->state;

  /* Obtain the size of the packet and put it into the "len"
     variable. */
  readlen = read(tapif->fd, p->data, MAX_PACKET_LENGTH);
  if (readlen < 0) {
    perror("read returned -1");
    exit(1);
  }

  p->dataLength = (size_t)readlen;

  return p;
}

/*-----------------------------------------------------------------------------------*/
/*
 * tapif_input():
 *
 * This function should be called when a packet is ready to be read
 * from the interface. It uses the function low_level_input() that
 * should handle the actual reception of bytes from the network
 * interface.
 *
 */
/*-----------------------------------------------------------------------------------*/
static void
tapif_input(struct netif *netif)
{
  struct packet *p = low_level_input(netif);
  struct tapif *tapif = (struct tapif *)netif->state;
  if (p == NULL) {
    LWIP_DEBUGF(TAPIF_DEBUG, ("tapif_input: low_level_input returned NULL\n"));
    return;
  }

  if(list_push(tapif->inList, p) == 0) {
    LWIP_DEBUGF(TAPIF_DEBUG, ("tapif_input: list is full\n"));
    free(p);
  }

}
/*-----------------------------------------------------------------------------------*/
/*
 * tapif_init():
 *
 * Should be called at the beginning of the program to set up the
 * network interface. It calls the function low_level_init() to do the
 * actual setup of the hardware.
 *
 */
/*-----------------------------------------------------------------------------------*/
err_t
tapif_init(struct netif *netif)
{
  struct tapif *tapif = (struct tapif *)mem_malloc(sizeof(struct tapif));

  if (tapif == NULL) {
    LWIP_DEBUGF(NETIF_DEBUG, ("tapif_init: out of memory for tapif\n"));
    return ERR_MEM;
  }
  netif->state = tapif;
  MIB2_INIT_NETIF(netif, snmp_ifType_other, 100000000);

  netif->name[0] = IFNAME0;
  netif->name[1] = IFNAME1;
#if LWIP_IPV4
  netif->output = etharp_output;
#endif /* LWIP_IPV4 */
#if LWIP_IPV6
  netif->output_ip6 = ethip6_output;
#endif /* LWIP_IPV6 */
  netif->linkoutput = low_level_output;
  netif->mtu = 1500;

  low_level_init(netif);

  return ERR_OK;
}

static void
freertos_thread(void *arg)
{
  struct netif *netif;
  struct tapif *tapif;
  fd_set fdset;
  int ret;

  netif = (struct netif *)arg;
  tapif = (struct tapif *)netif->state;

  while (1)
  {
    struct packet* buf;

    buf = list_pop(tapif->inList);
    while(buf != NULL) {

      struct pbuf* pbuf = pbuf_alloc(PBUF_RAW, buf->dataLength, PBUF_RAM);
      pbuf_take(pbuf, buf->data, buf->dataLength);
      free(buf);

      if (netif->input(pbuf, netif) != ERR_OK)
      {
        LWIP_DEBUGF(NETIF_DEBUG, ("tapif_input: netif input error\n"));
        free(buf);
      }
      buf = list_pop(tapif->inList);
    }
    vTaskDelay(1 / portTICK_PERIOD_MS);
  }
}

static void *tapif_in_thread(void *arg)
{
  sigset_t set;

  sigfillset( &set );
  pthread_sigmask( SIG_SETMASK, &set, NULL );

  struct netif *netif;
  struct tapif *tapif;
  fd_set fdset;
  int ret;

  netif = (struct netif *)arg;
  tapif = (struct tapif *)netif->state;

  while (1)
  {
    FD_ZERO(&fdset);
    FD_SET(tapif->fd, &fdset);

    /* Wait for a packet to arrive. */
    ret = select(tapif->fd + 1, &fdset, NULL, NULL, NULL);
    if (ret == 1)
    {
      tapif_input(netif);
    }
    else if (ret == -1)
    {
      perror("tapif_in_thread: select");
    }
  }
}

static void *tapif_out_thread(void *arg)
{
  sigset_t set;

  sigfillset( &set );
  pthread_sigmask( SIG_SETMASK, &set, NULL );

  struct netif *netif;
  struct tapif *tapif;
  fd_set fdset;
  int ret;

  netif = (struct netif *)arg;
  tapif = (struct tapif *)netif->state;

  while (1)
  {
    event_wait(tapif->outEvent);

    bool ok = true;
    while(ok) {
      ok = try_send_packet(tapif);
    }
  }
}

bool try_send_packet(struct tapif* tapif)
{
  struct packet* p = list_pop(tapif->outList);
  if (p == NULL) {
    return false;
  }
  ssize_t written;

  written = write(tapif->fd, p->data, p->dataLength);
  free(p);
  if (written < 0) {
    perror("tapif: write");
  }
  return true;
}
