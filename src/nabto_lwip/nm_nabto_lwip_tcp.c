#include "nm_nabto_lwip.h"
#include "nm_nabto_lwip_util.h"

#include <string.h>

#include <lwip/tcp.h>
#include <lwip/netif.h>

#include <platform/interfaces/np_tcp.h>

#include <platform/np_types.h>
#include <platform/np_error_code.h>
#include <platform/np_completion_event.h>
#include <platform/np_logging.h>

#include <nn/string_set.h>
#include <nn/string_map.h>

#include "common.h"
#include "default_netif.h"

#define TCP_LOG  NABTO_LOG_MODULE_TCP

struct np_tcp_socket
{
    struct tcp_pcb *pcb;
    struct np_completion_event *connect_ce;
    struct np_completion_event *readCompletionEvent;
    void* readBuffer;
    size_t readBufferLength;
    size_t* readLength;
    struct pbuf *inBuffer; // pbuf or chain of pbufs with incoming tcp data.
    uint16_t inBufferOffset; // offset into the head pbuffer where to read from.
};


// ---------------------
// TCP
// ---------------------

static void nplwip_tcp_destroy(struct np_tcp_socket *socket);
static void try_read(struct np_tcp_socket* socket);

static err_t nplwip_tcp_connected_callback(void *arg, struct tcp_pcb *tpcb, err_t err)
{
    UNUSED(tpcb);
    UNUSED(err);
    struct np_tcp_socket *socket = (struct np_tcp_socket*)arg;
    np_completion_event_resolve(socket->connect_ce, NABTO_EC_OK);
    return ERR_OK;
}

static err_t nplwip_tcp_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    UNUSED(tpcb);
    struct np_tcp_socket *socket = (struct np_tcp_socket*)arg;

    if (p == NULL)
    {
        // If this function is called with NULL then the host has closed the connection.
        // nplwip_tcp_destroy(socket);
        return ERR_OK;
    }

    if (err != ERR_OK)
    {
        // @TODO: For some reason, we got a packet that we did not expect.
        return err;
    }
    else
    {
        if (socket->inBuffer == NULL)
        {
            socket->inBuffer = p;
        }
        else
        {
            pbuf_cat(socket->inBuffer, p);
        }
    }

    try_read(socket);

    return ERR_OK;
}

static np_error_code nplwip_tcp_create(struct np_tcp *obj, struct np_tcp_socket **out_socket)
{
    UNUSED(obj);
    struct np_tcp_socket *socket = malloc(sizeof(struct np_tcp_socket));
    if (socket == NULL)
    {
        return NABTO_EC_OUT_OF_MEMORY;
    }

    LOCK_TCPIP_CORE();
    socket->pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    UNLOCK_TCPIP_CORE();

    if (socket->pcb == NULL) {
        free(socket);
        return NABTO_EC_OUT_OF_MEMORY;
    }

    LOCK_TCPIP_CORE();
    tcp_arg(socket->pcb, socket);
    tcp_recv(socket->pcb, nplwip_tcp_recv_callback);
    UNLOCK_TCPIP_CORE();

    socket->connect_ce = NULL;
    socket->inBuffer = NULL;

    // @TODO: Set an error callback with tcp_err()

    *out_socket = socket;
    return NABTO_EC_OK;
}

static void nplwip_tcp_abort(struct np_tcp_socket *socket)
{
    LOCK_TCPIP_CORE();
    tcp_abort(socket->pcb);
    UNLOCK_TCPIP_CORE();
}

static void nplwip_tcp_destroy(struct np_tcp_socket *socket)
{
    if (socket == NULL)
    {
        NABTO_LOG_ERROR(TCP_LOG, "TCP socket destroyed twice.");
        return;
    }

    LOCK_TCPIP_CORE();
    tcp_arg(socket->pcb, NULL);
    tcp_sent(socket->pcb, NULL);
    tcp_recv(socket->pcb, NULL);
    err_t error = tcp_close(socket->pcb);
    UNLOCK_TCPIP_CORE();
    if (error == ERR_MEM)
    {
        NABTO_LOG_ERROR(TCP_LOG, "lwIP failed to close TCP socket due to lack of memory.")
        // @TODO: We need to wait and try to close again in the acknowledgement callback.
    }

    free(socket);
}

static void nplwip_tcp_async_connect(struct np_tcp_socket *socket, struct np_ip_address *addr, uint16_t port,
                                     struct np_completion_event *completion_event)
{
    ip_addr_t ip;
    nplwip_convertip_np_to_lwip(addr, &ip);

    socket->connect_ce = completion_event;

    LOCK_TCPIP_CORE();
    err_t error = tcp_connect(socket->pcb, &ip, port, nplwip_tcp_connected_callback);
    UNLOCK_TCPIP_CORE();
    if (error != ERR_OK)
    {
        np_error_code ec = NABTO_EC_UNKNOWN;
        if (error == ERR_MEM) {
            ec = NABTO_EC_OUT_OF_MEMORY;
        }
        NABTO_LOG_ERROR(TCP_LOG, "TCP socket could not connect %d", error);
        np_completion_event_resolve(completion_event, ec);
    }
}

// @TODO: Currently calling tcp_output to flush out data immediately for simplicity.
// This may not be optimal (see lwip docs).
static void nplwip_tcp_async_write(struct np_tcp_socket *socket, const void *data, size_t data_len,
                                   struct np_completion_event *completion_event)
{
    err_t error;

    LOCK_TCPIP_CORE();
    error = tcp_write(socket->pcb, data, data_len, 0);
    UNLOCK_TCPIP_CORE();
    if (error != ERR_OK)
    {
        NABTO_LOG_ERROR(TCP_LOG, "tcp_write failed, lwIP error: %i", error);
        np_completion_event_resolve(completion_event, NABTO_EC_UNKNOWN);
        return;
    }

    LOCK_TCPIP_CORE();
    error = tcp_output(socket->pcb);
    UNLOCK_TCPIP_CORE();
    if (error != ERR_OK)
    {
        NABTO_LOG_ERROR(TCP_LOG, "tcp_output failed, lwIP error: %i", error);
        np_completion_event_resolve(completion_event, NABTO_EC_UNKNOWN);
        return;
    }

    np_completion_event_resolve(completion_event, NABTO_EC_OK);
}

static void nplwip_tcp_async_read(struct np_tcp_socket *socket, void *buffer, size_t buffer_len, size_t *read_len,
                                  struct np_completion_event *completionEvent)
{
    if (socket->readCompletionEvent != NULL) {
        np_completion_event_resolve(completionEvent, NABTO_EC_OPERATION_IN_PROGRESS);
        return;
    }

    socket->readBuffer = buffer;
    socket->readBufferLength = buffer_len;
    socket->readLength = read_len;
    socket->readCompletionEvent = completionEvent;
    struct pbuf *ptr;
    size_t read = 0;

    try_read(socket);
}

static void nplwip_tcp_shutdown(struct np_tcp_socket *socket)
{
    LOCK_TCPIP_CORE();
    err_t error = tcp_shutdown(socket->pcb, 0, 1);
    UNLOCK_TCPIP_CORE();
    if (error != ERR_OK)
    {
        NABTO_LOG_ERROR(TCP_LOG, "TCP socket shutdown failed for some reason.");
    }
}

static void try_read(struct np_tcp_socket* socket)
{
    if (socket->inBuffer == NULL) {
        return;
    }
    if (socket->readCompletionEvent == NULL) {
        return;
    }

    uint16_t headInBufferMissingLength = socket->inBuffer->len - socket->inBufferOffset;

    if (socket->readBufferLength < headInBufferMissingLength) {
        memcpy(socket->readBuffer, socket->inBuffer->payload + socket->inBufferOffset, socket->readBufferLength);
        *(socket->readLength) = socket->readBufferLength;
        socket->inBufferOffset += socket->readBufferLength;
    } else {
        uint16_t readLength = socket->inBuffer->len - socket->inBufferOffset;
        memcpy(socket->readBuffer, socket->inBuffer->payload + socket->inBufferOffset, readLength);
        *socket->readLength = readLength;
        socket->inBufferOffset = 0;
        struct pbuf* oldHead = socket->inBuffer;
        socket->inBuffer = socket->inBuffer->next;
        socket->inBufferOffset = 0;
        pbuf_free(oldHead);
    }

    LOCK_TCPIP_CORE();
    tcp_recved(socket->pcb, *socket->readLength);
    UNLOCK_TCPIP_CORE();

    np_completion_event_resolve(socket->readCompletionEvent, NABTO_EC_OK);
    socket->readCompletionEvent = NULL;
}

static struct np_tcp_functions tcp_module = {
    .create = nplwip_tcp_create,
    .destroy = nplwip_tcp_destroy,
    .abort = nplwip_tcp_abort,
    .async_connect = nplwip_tcp_async_connect,
    .async_write = nplwip_tcp_async_write,
    .async_read = nplwip_tcp_async_read,
    .shutdown = nplwip_tcp_shutdown
};

struct np_tcp nplwip_get_tcp_impl()
{
    struct np_tcp obj;
    obj.mptr = &tcp_module;
    obj.data = NULL;
    return obj;
}
