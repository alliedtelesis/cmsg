/**
 * The forwarding transport is used in cases where the application wants the client
 * to forward the encoded protobuf message using some supplied function.
 *
 * Copyright 2021, Allied Telesis Labs New Zealand, Ltd
 */
#include "cmsg_error.h"
#include "cmsg_transport_private.h"

struct forwarding_info
{
    void *user_data;
    cmsg_forwarding_transport_send_f send_f;
};

static void
cmsg_transport_forwarding_client_destroy (cmsg_transport *transport)
{
    CMSG_FREE (transport->user_data);
    transport->user_data = NULL;
}

static int
cmsg_transport_forwarding_client_send (cmsg_transport *transport, void *buff, int length,
                                       int flag)
{
    struct forwarding_info *info = transport->user_data;

    if (!info->send_f (info->user_data, buff, length))
    {
        return -1;
    }

    return length;
}

static int
cmsg_transport_forwarding_recv_wrapper (cmsg_transport *transport, int sock, void *msg,
                                        int len, int flags)
{
    struct forwarding_info *info = transport->user_data;
    struct cmsg_forwarding_server_data *recv_data = info->user_data;

    /* Check whether there is any data to read */
    if (recv_data->pos >= recv_data->len)
    {
        return -1;
    }

    if (recv_data->pos + len > recv_data->len)
    {
        len = recv_data->len - recv_data->pos;
    }

    memcpy (msg, &recv_data->msg[recv_data->pos], len);

    /* If we are only peeking at the data then don't increase the buffer position */
    if (!(flags & MSG_PEEK))
    {
        recv_data->pos += len;
    }

    return len;
}

void
cmsg_transport_forwarding_init (cmsg_transport *transport)
{
    if (transport == NULL)
    {
        return;
    }

    transport->tport_funcs.client_send = cmsg_transport_forwarding_client_send;
    transport->tport_funcs.destroy = cmsg_transport_forwarding_client_destroy;

    transport->tport_funcs.server_recv = cmsg_transport_server_recv;
    transport->tport_funcs.recv_wrapper = cmsg_transport_forwarding_recv_wrapper;

    transport->user_data = CMSG_CALLOC (1, sizeof (struct forwarding_info));
}

void
cmsg_transport_forwarding_func_set (cmsg_transport *transport,
                                    cmsg_forwarding_transport_send_f send_func)
{
    struct forwarding_info *info = transport->user_data;
    info->send_f = send_func;
}

void
cmsg_transport_forwarding_user_data_set (cmsg_transport *transport, void *user_data)
{
    struct forwarding_info *info = transport->user_data;
    info->user_data = user_data;
}

void *
cmsg_transport_forwarding_user_data_get (cmsg_transport *transport)
{
    struct forwarding_info *info = transport->user_data;
    return info->user_data;
}
