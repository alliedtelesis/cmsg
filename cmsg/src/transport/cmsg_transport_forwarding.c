/**
 * The forwarding transport is used in cases where the application wants the client
 * to forward the encoded protobuf message using some supplied function.
 *
 * Copyright 2021, Allied Telesis Labs New Zealand, Ltd
 */
#include "cmsg_transport.h"
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
    CMSG_FREE (transport->udt_info.data);
    transport->udt_info.data = NULL;
}

static int
cmsg_transport_forwarding_client_send (cmsg_transport *transport, void *buff, int length,
                                       int flag)
{
    struct forwarding_info *info = transport->udt_info.data;

    if (info->send_f (info->user_data, buff, length) < 0)
    {
        return -1;
    }

    return length;
}

void
cmsg_transport_forwarding_init (cmsg_transport *transport)
{
    if (transport == NULL)
    {
        return;
    }

    cmsg_transport_udt_init (transport);

    transport->udt_info.functions.client_send = cmsg_transport_forwarding_client_send;
    transport->udt_info.functions.destroy = cmsg_transport_forwarding_client_destroy;

    transport->udt_info.data = CMSG_CALLOC (1, sizeof (struct forwarding_info));
}

void
cmsg_transport_forwarding_func_set (cmsg_transport *transport,
                                    cmsg_forwarding_transport_send_f send_func)
{
    struct forwarding_info *info = transport->udt_info.data;
    info->send_f = send_func;
}

void
cmsg_transport_forwarding_user_data_set (cmsg_transport *transport, void *user_data)
{
    struct forwarding_info *info = transport->udt_info.data;
    info->user_data = user_data;
}
