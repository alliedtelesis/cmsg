/**
 * cmsg-transport-udt.c
 *
 * Define a transport mechanism of USER DEFINED.
 * A USER DEFINED transport is defined as one having user functions for the
 * functionality of connecting, sending and receiving.
 *
 * The user can provide functions to perform the connect & send functions.
 * These will take the form:
 * int user_connect (cmsg_client *client)
 * int user_send (cmsg_client *client, void *buff, int length, int flag)
 *
 * The user needs to set these functions prior to using the transport for a client.
 *
 * Reception processing of a received message will be done by calling the
 * msg_processor function.  This is the responsibility of the user receive
 * handling.
 *
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#include "cmsg_private.h"
#include "cmsg_transport.h"
#include "cmsg_transport_private.h"

extern void cmsg_transport_rpc_tcp_funcs_init (cmsg_tport_functions *tport_funcs);
extern void cmsg_transport_oneway_tcp_funcs_init (cmsg_tport_functions *tport_funcs);


static int32_t
cmsg_transport_udt_listen (cmsg_transport *transport)
{
    if (transport->udt_info.functions.listen)
    {
        transport->udt_info.functions.listen (transport);
    }

    return 0;
}


static int32_t
cmsg_transport_udt_server_recv (int32_t server_socket, cmsg_transport *transport,
                                uint8_t **recv_buffer, cmsg_header *processed_header,
                                int *nbytes)
{
    int32_t ret = -1;


    if (transport->udt_info.functions.server_recv)
    {
        ret = transport->udt_info.functions.server_recv (server_socket, transport,
                                                         recv_buffer, processed_header,
                                                         nbytes);
    }

    return ret;
}


int32_t
cmsg_transport_udt_server_accept (int32_t listen_socket, cmsg_transport *transport)
{
    if (transport->udt_info.functions.server_accept)
    {
        return transport->udt_info.functions.server_accept (listen_socket, transport);
    }

    return -1;
}


/**
 * UDT clients do not receive a reply to their messages. This
 * function therefore returns NULL. It should not be called by the client, but
 * it prevents a null pointer exception from occurring if no function is
 * defined
 */
static cmsg_status_code
cmsg_transport_udt_client_recv (cmsg_transport *transport,
                                const ProtobufCServiceDescriptor *descriptor,
                                ProtobufCMessage **messagePtPt)
{
    if (transport->udt_info.functions.client_recv)
    {
        return transport->udt_info.functions.client_recv (transport, descriptor,
                                                          messagePtPt);
    }

    *messagePtPt = NULL;
    return CMSG_STATUS_CODE_SUCCESS;
}


static int32_t
cmsg_transport_udt_server_send (int socket, cmsg_transport *transport, void *buff,
                                int length, int flag)
{
    if (transport->udt_info.functions.server_send)
    {
        return transport->udt_info.functions.server_send (socket, transport, buff, length,
                                                          flag);
    }

    return 0;
}


static void
cmsg_transport_udt_client_close (cmsg_transport *transport)
{
    if (transport->udt_info.functions.client_close)
    {
        transport->udt_info.functions.client_close (transport);
    }
}


static void
cmsg_transport_udt_server_close (cmsg_transport *transport)
{
    if (transport->udt_info.functions.server_close)
    {
        transport->udt_info.functions.server_close (transport);
    }
}


static int
cmsg_transport_udt_get_socket (cmsg_transport *transport)
{
    if (transport->udt_info.functions.get_socket)
    {
        return transport->udt_info.functions.get_socket (transport);
    }

    return 0;
}

static void
cmsg_transport_udt_server_destroy (cmsg_transport *transport)
{
    if (transport->udt_info.functions.server_destroy)
    {
        transport->udt_info.functions.server_destroy (transport);
    }
}

static int32_t
cmsg_transport_udt_client_send (cmsg_transport *transport, void *buff, int length, int flag)
{

    if (transport->udt_info.functions.client_send)
    {
        return transport->udt_info.functions.client_send (transport, buff, length, flag);
    }

    // Function isn't defined so just pretend the message was sent.
    return 0;
}

int
cmsg_transport_udt_recv_wrapper (cmsg_transport *transport, int sock, void *buff, int len,
                                 int flags)
{
    if (transport->udt_info.functions.recv_wrapper)
    {
        return transport->udt_info.functions.recv_wrapper (transport, sock, buff, len,
                                                           flags);
    }

    return 0;
}


/*
 * Call the user defined transport connect function and change the state of
 * the client connection to connected.
 */
static int32_t
cmsg_transport_udt_connect (cmsg_transport *transport, int timeout)
{
    int32_t ret = 0;

    if (transport->udt_info.functions.connect)
    {
        ret = transport->udt_info.functions.connect (transport, timeout);
    }

    return ret;
}


/**
 * Can't work out whether the UDT is congested
 */
bool
cmsg_transport_udt_is_congested (cmsg_transport *transport)
{
    if (transport->udt_info.functions.is_congested)
    {
        return transport->udt_info.functions.is_congested (transport);
    }

    return false;
}


int32_t
cmsg_transport_udt_send_can_block_enable (cmsg_transport *transport,
                                          uint32_t send_can_block)
{
    if (transport->udt_info.functions.send_can_block_enable)
    {
        return transport->udt_info.functions.send_can_block_enable (transport,
                                                                    send_can_block);
    }

    return -1;
}


int32_t
cmsg_transport_udt_ipfree_bind_enable (cmsg_transport *transport,
                                       cmsg_bool_t use_ipfree_bind)
{
    if (transport->udt_info.functions.ipfree_bind_enable)
    {
        return transport->udt_info.functions.ipfree_bind_enable (transport,
                                                                 use_ipfree_bind);
    }

    return -1;
}


/**
 * Initialise the function pointers that userdefined transport type
 * will use.
 */
void
cmsg_transport_udt_init (cmsg_transport *transport)
{
    if (transport == NULL)
    {
        return;
    }

    transport->tport_funcs.recv_wrapper = cmsg_transport_udt_recv_wrapper;
    transport->tport_funcs.connect = cmsg_transport_udt_connect;
    transport->tport_funcs.listen = cmsg_transport_udt_listen;
    transport->tport_funcs.server_accept = cmsg_transport_udt_server_accept;
    transport->tport_funcs.server_send = cmsg_transport_udt_server_send;
    transport->tport_funcs.server_recv = cmsg_transport_udt_server_recv;
    transport->tport_funcs.client_recv = cmsg_transport_udt_client_recv;
    transport->tport_funcs.client_send = cmsg_transport_udt_client_send;
    transport->tport_funcs.client_close = cmsg_transport_udt_client_close;
    transport->tport_funcs.server_close = cmsg_transport_udt_server_close;

    transport->tport_funcs.get_socket = cmsg_transport_udt_get_socket;

    transport->tport_funcs.server_destroy = cmsg_transport_udt_server_destroy;

    transport->tport_funcs.is_congested = cmsg_transport_udt_is_congested;
    transport->tport_funcs.send_can_block_enable = cmsg_transport_udt_send_can_block_enable;
    transport->tport_funcs.ipfree_bind_enable = cmsg_transport_udt_ipfree_bind_enable;
}

void
cmsg_transport_udt_tcp_base_init (cmsg_transport *transport, bool oneway)
{
    if (oneway)
    {
        cmsg_transport_oneway_tcp_funcs_init (&transport->udt_info.base);
    }
    else
    {
        cmsg_transport_rpc_tcp_funcs_init (&transport->udt_info.base);
    }
}
