#include <lwip/netif.h>
#include <lwip/tcp.h>
#include <nn/string_map.h>
#include <nn/string_set.h>
#include <platform/interfaces/np_tcp.h>
#include <platform/np_completion_event.h>
#include <platform/np_error_code.h>
#include <platform/np_logging.h>
#include <platform/np_types.h>
#include <string.h>

#include "common.h"
#include "default_netif.h"
#include "nm_nabto_lwip.h"
#include "nm_nabto_lwip_util.h"

#define TCP_LOG NABTO_LOG_MODULE_TCP

struct np_tcp_socket {
    struct tcp_pcb *pcb;
    struct np_completion_event *connectCompletionEvent;
    struct np_completion_event *readCompletionEvent;
    void *readBuffer;
    size_t readBufferLength;
    size_t *readLength;
    struct pbuf *inBuffer;  // pbuf or chain of pbufs with incoming tcp data.
    uint16_t
        inBufferOffset;  // offset into the head pbuffer where to read from.
    bool remoteClosed;
    bool aborted;
};

// ---------------------
// TCP
// ---------------------

static void nm_lwip_tcp_destroy(struct np_tcp_socket *socket);
static void try_read(struct np_tcp_socket *socket);
static void try_connect(struct np_tcp_socket *socket);

static err_t nm_lwip_tcp_connected_callback(void *arg, struct tcp_pcb *tpcb,
                                           err_t err)
{
    NABTO_LOG_TRACE(TCP_LOG, "TCP Connected");
    UNUSED(tpcb);
    UNUSED(err);
    struct np_tcp_socket *socket = (struct np_tcp_socket *)arg;
    try_connect(socket);
    np_completion_event_resolve(socket->connectCompletionEvent, NABTO_EC_OK);

    return ERR_OK;
}

static void nm_lwip_tcp_err_callback(void *arg, err_t err)
{
    struct np_tcp_socket *socket = arg;
    if (err == ERR_RST) {
        NABTO_LOG_TRACE(TCP_LOG, "TCP RST");
        socket->aborted = true;
    } else if (err == ERR_ABRT) {
        NABTO_LOG_TRACE(TCP_LOG, "TCP_ABRT")
        socket->aborted = true;
    } else {
        NABTO_LOG_TRACE(TCP_LOG, "err %d", err);
    }
    try_read(socket);
    try_connect(socket);
}

static err_t nm_lwip_tcp_recv_callback(void *arg, struct tcp_pcb *tpcb,
                                      struct pbuf *p, err_t err)
{
    UNUSED(tpcb);
    struct np_tcp_socket *socket = (struct np_tcp_socket *)arg;

    if (p == NULL) {
        // If this function is called with NULL then the host has closed the
        // connection. nm_lwip_tcp_destroy(socket);
        socket->remoteClosed = true;
        return ERR_OK;
    }

    if (err != ERR_OK) {
        NABTO_LOG_TRACE(TCP_LOG,
                  "tcp recv callback with non OK err %d aborting the socket.",
                  err);
        tcp_abort(tpcb);
        socket->aborted = true;
        return ERR_ABRT;
    }

    if (socket->inBuffer == NULL) {
        socket->inBuffer = p;
    } else {
        pbuf_cat(socket->inBuffer, p);
    }

    try_read(socket);

    return ERR_OK;
}

static np_error_code nm_lwip_tcp_create(struct np_tcp *obj,
                                       struct np_tcp_socket **out_socket)
{
    UNUSED(obj);
    struct np_tcp_socket *socket = calloc(1, sizeof(struct np_tcp_socket));
    if (socket == NULL) {
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
    tcp_recv(socket->pcb, nm_lwip_tcp_recv_callback);
    tcp_err(socket->pcb, nm_lwip_tcp_err_callback);
    UNLOCK_TCPIP_CORE();

    socket->connectCompletionEvent = NULL;
    socket->inBuffer = NULL;

    // @TODO: Set an error callback with tcp_err()

    *out_socket = socket;
    return NABTO_EC_OK;
}

static void nm_lwip_tcp_abort(struct np_tcp_socket *socket)
{
    NABTO_LOG_TRACE(TCP_LOG, "nm_lwip_tcp_abort");
    LOCK_TCPIP_CORE();
    tcp_abort(socket->pcb);
    UNLOCK_TCPIP_CORE();
}

static void nm_lwip_tcp_destroy(struct np_tcp_socket *socket)
{
    NABTO_LOG_TRACE(TCP_LOG, "nm_lwip_tcp_destroy");
    if (socket == NULL) {
        NABTO_LOG_ERROR(TCP_LOG, "TCP socket destroyed twice.");
        return;
    }

    LOCK_TCPIP_CORE();
    tcp_arg(socket->pcb, NULL);
    tcp_sent(socket->pcb, NULL);
    tcp_recv(socket->pcb, NULL);
    err_t error = tcp_close(socket->pcb);
    UNLOCK_TCPIP_CORE();
    if (error == ERR_MEM) {
        NABTO_LOG_ERROR(
            TCP_LOG, "lwIP failed to close TCP socket due to lack of memory.")
        // @TODO: We need to wait and try to close again in the acknowledgement
        // callback.
    }

    free(socket);
}

static void nm_lwip_tcp_async_connect(
    struct np_tcp_socket *socket, struct np_ip_address *addr, uint16_t port,
    struct np_completion_event *completion_event)
{
    NABTO_LOG_TRACE(TCP_LOG, "nm_lwip_tcp_async_connect");
    ip_addr_t ip;
    nm_lwip_convertip_np_to_lwip(addr, &ip);

    socket->connectCompletionEvent = completion_event;

    LOCK_TCPIP_CORE();
    err_t error =
        tcp_connect(socket->pcb, &ip, port, nm_lwip_tcp_connected_callback);
    UNLOCK_TCPIP_CORE();
    if (error != ERR_OK) {
        np_error_code ec = NABTO_EC_UNKNOWN;
        if (error == ERR_MEM) {
            ec = NABTO_EC_OUT_OF_MEMORY;
        }
        NABTO_LOG_ERROR(TCP_LOG, "TCP socket could not connect %d", error);
        np_completion_event_resolve(completion_event, ec);
    }
}

// @TODO: Currently calling tcp_output to flush out data immediately for
// simplicity. This may not be optimal (see lwip docs).
static void nm_lwip_tcp_async_write(struct np_tcp_socket *socket,
                                   const void *data, size_t data_len,
                                   struct np_completion_event *completion_event)
{
    NABTO_LOG_TRACE(TCP_LOG, "nm_lwip_tcp_async_write");
    err_t error;

    LOCK_TCPIP_CORE();
    error = tcp_write(socket->pcb, data, data_len, 0);
    UNLOCK_TCPIP_CORE();
    if (error != ERR_OK) {
        NABTO_LOG_ERROR(TCP_LOG, "tcp_write failed, lwIP error: %i", error);
        np_completion_event_resolve(completion_event, NABTO_EC_UNKNOWN);
        return;
    }

    LOCK_TCPIP_CORE();
    error = tcp_output(socket->pcb);
    UNLOCK_TCPIP_CORE();
    if (error != ERR_OK) {
        NABTO_LOG_ERROR(TCP_LOG, "tcp_output failed, lwIP error: %i", error);
        np_completion_event_resolve(completion_event, NABTO_EC_UNKNOWN);
        return;
    }

    np_completion_event_resolve(completion_event, NABTO_EC_OK);
}

static void nm_lwip_tcp_async_read(struct np_tcp_socket *socket, void *buffer,
                                  size_t buffer_len, size_t *read_len,
                                  struct np_completion_event *completionEvent)
{
    NABTO_LOG_TRACE(TCP_LOG, "nm_lwip_tcp_async_read");
    if (socket->readCompletionEvent != NULL) {
        np_completion_event_resolve(completionEvent,
                                    NABTO_EC_OPERATION_IN_PROGRESS);
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

static void nm_lwip_tcp_shutdown(struct np_tcp_socket *socket)
{
    NABTO_LOG_TRACE(TCP_LOG, "nm_lwip_tcp_shutdown");
    LOCK_TCPIP_CORE();
    err_t error = tcp_shutdown(socket->pcb, 0, 1);
    UNLOCK_TCPIP_CORE();
    if (error != ERR_OK) {
        NABTO_LOG_ERROR(TCP_LOG, "TCP socket shutdown failed for some reason.");
    }
}

static void try_connect(struct np_tcp_socket* socket)
{
    if (socket->connectCompletionEvent == NULL) {
        return;
    }

    np_error_code ec = NABTO_EC_OK;
    if (socket->remoteClosed || socket->aborted) {
        ec = NABTO_EC_ABORTED;
    }
    np_completion_event_resolve(socket->connectCompletionEvent, ec);
}

static void try_read(struct np_tcp_socket *socket)
{
    if (socket->readCompletionEvent == NULL) {
        return;
    }
    if (socket->inBuffer == NULL && socket->remoteClosed == false && socket->aborted == false) {
        return;
    }

    np_error_code ec;

    if (socket->inBuffer != NULL) {
        // return data
        uint16_t headInBufferMissingLength =
            socket->inBuffer->len - socket->inBufferOffset;

        if (socket->readBufferLength < headInBufferMissingLength) {
            memcpy(socket->readBuffer,
                   socket->inBuffer->payload + socket->inBufferOffset,
                   socket->readBufferLength);
            *(socket->readLength) = socket->readBufferLength;
            socket->inBufferOffset += socket->readBufferLength;
        } else {
            uint16_t readLength =
                socket->inBuffer->len - socket->inBufferOffset;
            memcpy(socket->readBuffer,
                   socket->inBuffer->payload + socket->inBufferOffset,
                   readLength);
            *socket->readLength = readLength;
            socket->inBufferOffset = 0;
            struct pbuf *oldHead = socket->inBuffer;
            socket->inBuffer = socket->inBuffer->next;
            socket->inBufferOffset = 0;
            pbuf_free(oldHead);
        }

        LOCK_TCPIP_CORE();
        tcp_recved(socket->pcb, *socket->readLength);
        UNLOCK_TCPIP_CORE();

        ec = NABTO_EC_OK;
    } else if (socket->aborted) {
        ec = NABTO_EC_ABORTED;
    } else if (socket->remoteClosed) {
        ec = NABTO_EC_EOF;
    }
    NABTO_LOG_TRACE(TCP_LOG, "resolve tcp read with ec %s", np_error_code_to_string(ec));
    np_completion_event_resolve(socket->readCompletionEvent, ec);
    socket->readCompletionEvent = NULL;
}

static struct np_tcp_functions tcp_module = {
    .create = nm_lwip_tcp_create,
    .destroy = nm_lwip_tcp_destroy,
    .abort = nm_lwip_tcp_abort,
    .async_connect = nm_lwip_tcp_async_connect,
    .async_write = nm_lwip_tcp_async_write,
    .async_read = nm_lwip_tcp_async_read,
    .shutdown = nm_lwip_tcp_shutdown};

struct np_tcp nm_lwip_get_tcp_impl()
{
    struct np_tcp obj;
    obj.mptr = &tcp_module;
    obj.data = NULL;
    return obj;
}
