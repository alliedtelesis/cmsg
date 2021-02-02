/**
 * Describes the Loopback transport information.
 *
 * Loopback transport is used in cases where the application stills wants to call the
 * api functions but doesn't want the information to go over a transport.  Cases of use
 * for this are on products that do not support VCStack and therefore to keep the
 * application generic just the initialisation can be changed to allow the same api
 * and impls to be used.
 *
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#include "cmsg_transport.h"
#include "cmsg_error.h"
#include "cmsg_transport_private.h"

struct cmsg_loopback_recv_buffer
{
    uint8_t *msg;
    uint32_t len;
    uint32_t pos;
};

/**
 * Close the socket on the client.
 */
static void
cmsg_transport_loopback_client_close (cmsg_transport *transport)
{
    struct cmsg_loopback_recv_buffer *buffer;

    if (transport->user_data)
    {
        buffer = transport->user_data;
        CMSG_FREE (buffer->msg);
        CMSG_FREE (buffer);
        transport->user_data = NULL;
    }
}

/**
 * Server stores the response on the transport that the client can then read off.
 */
static int32_t
cmsg_transport_loopback_server_send (int socket, cmsg_transport *transport, void *buff,
                                     int length, int flag)
{
    uint8_t *packet_data = NULL;
    struct cmsg_loopback_recv_buffer *buffer_data = NULL;

    buffer_data = CMSG_MALLOC (sizeof (struct cmsg_loopback_recv_buffer));

    packet_data = CMSG_MALLOC (length);
    memcpy (packet_data, buff, length);

    buffer_data->msg = packet_data;
    buffer_data->len = length;
    buffer_data->pos = 0;

    transport->user_data = buffer_data;
    return length;
}

static int
cmsg_transport_loopback_recv_handler (cmsg_transport *transport, int sock, void *msg,
                                      int len, int flags)
{
    struct cmsg_loopback_recv_buffer *buffer;

    buffer = transport->user_data;

    /* Check whether there is any data to read */
    if (buffer->pos >= buffer->len)
    {
        return -1;
    }

    /* If the caller asks to read more data than is actually in the buffer ensure we
     * only return the available data. */
    if (buffer->pos + len > buffer->len)
    {
        len = buffer->len - buffer->pos;
    }

    memcpy (msg, &buffer->msg[buffer->pos], len);

    /* If we are only peeking at the data then don't increase the buffer position */
    if (!(flags & MSG_PEEK))
    {
        buffer->pos += len;
    }

    return len;
}

cmsg_status_code
cmsg_transport_loopback_client_recv (cmsg_transport *transport,
                                     const ProtobufCServiceDescriptor *descriptor,
                                     ProtobufCMessage **messagePtPt)
{
    cmsg_status_code ret;
    struct cmsg_loopback_recv_buffer *buffer;

    ret = cmsg_transport_client_recv (transport, descriptor, messagePtPt);

    buffer = transport->user_data;

    CMSG_FREE (buffer->msg);
    CMSG_FREE (buffer);
    transport->user_data = NULL;
    return ret;
}

void
cmsg_transport_loopback_init (cmsg_transport *transport)
{
    if (transport == NULL)
    {
        return;
    }

    transport->tport_funcs.server_send = cmsg_transport_loopback_server_send;
    transport->tport_funcs.recv_wrapper = cmsg_transport_loopback_recv_handler;
    transport->tport_funcs.client_recv = cmsg_transport_loopback_client_recv;
    transport->tport_funcs.socket_close = cmsg_transport_loopback_client_close;

    CMSG_DEBUG (CMSG_INFO, "%s: done\n", __FUNCTION__);
}
