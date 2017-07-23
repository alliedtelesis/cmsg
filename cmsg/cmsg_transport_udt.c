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
#include "cmsg_server.h"

/*
 *
 */
static int32_t
cmsg_transport_oneway_udt_listen (cmsg_transport *transport)
{
    // Function isn't needed for User Defined so nothing happens.
    return 0;
}



static int32_t
cmsg_transport_oneway_udt_server_recv (int32_t socket, cmsg_server *server)
{
    cmsg_transport *transport;
    cmsg_recv_func udt_recv;
    void *udt_data;
    int32_t ret = 0;

    transport = server->_transport;
    udt_recv = transport->config.udt.recv;
    udt_data = transport->config.udt.udt_data;

    if (udt_recv == NULL)
    {
        return -1;
    }

    ret = cmsg_transport_server_recv (udt_recv, udt_data, server);

    return ret;
}


/**
 * UDT clients do not receive a reply to their messages. This
 * function therefore returns NULL. It should not be called by the client, but
 * it prevents a null pointer exception from occurring if no function is
 * defined
 */
static cmsg_status_code
cmsg_transport_oneway_udt_client_recv (cmsg_transport *transport,
                                       const ProtobufCServiceDescriptor *descriptor,
                                       ProtobufCMessage **messagePtPt)
{
    // Function isn't needed for User Defined so nothing happens.
    *messagePtPt = NULL;
    return CMSG_STATUS_CODE_SUCCESS;
}


static int32_t
cmsg_transport_oneway_udt_server_send (cmsg_transport *transport, void *buff, int length,
                                       int flag)
{
    // Function isn't needed for User Defined so nothing happens.
    return 0;
}


static void
cmsg_transport_oneway_udt_client_close (cmsg_transport *transport)
{
    // Function isn't needed for User Defined so nothing happens.
    return;
}


static void
cmsg_transport_oneway_udt_server_close (cmsg_transport *transport)
{
    // Function isn't needed for User Defined so nothing happens.
    return;
}


static int
cmsg_transport_oneway_udt_server_get_socket (cmsg_transport *transport)
{
    // Function isn't needed for User Defined so nothing happens.
    return 0;
}


static int
cmsg_transport_oneway_udt_client_get_socket (cmsg_transport *transport)
{
    // Function isn't needed for User Defined so nothing happens.
    return 0;
}

static void
cmsg_transport_oneway_udt_client_destroy (cmsg_transport *transport)
{
    //placeholder to make sure destroy functions are called in the right order
}

static void
cmsg_transport_oneway_udt_server_destroy (cmsg_transport *transport)
{
    // Function isn't needed for User Defined so nothing happens.
    return;
}

static int32_t
cmsg_transport_oneway_udt_client_send (cmsg_transport *transport, void *buff, int length,
                                       int flag)
{

    if (transport->config.udt.send)
    {
        return (transport->config.udt.send (transport->config.udt.udt_data, buff,
                                            length, flag));
    }

    // Function isn't defined so just pretend the message was sent.
    return 0;
}


/*
 * Call the user defined transport connect function and change the state of
 * the client connection to connected.
 */
static int32_t
cmsg_transport_oneway_udt_connect (cmsg_transport *transport, int timeout)
{
    int32_t ret = 0;

    if (transport->config.udt.connect)
    {
        ret = transport->config.udt.connect (transport);
    }

    return ret;
}


/**
 * Can't work out whether the UDT is congested
 */
uint32_t
cmsg_transport_oneway_udt_is_congested (cmsg_transport *transport)
{
    return FALSE;
}


int32_t
cmsg_transport_udt_send_can_block_enable (cmsg_transport *transport,
                                          uint32_t send_can_block)
{
    // Don't support send blocking
    return -1;
}


int32_t
cmsg_transport_udt_ipfree_bind_enable (cmsg_transport *transport,
                                       cmsg_bool_t use_ipfree_bind)
{
    /* not supported yet */
    return -1;
}


/**
 * Initialise the function pointers that oneway userdefined transport type
 * will use.
 */
void
cmsg_transport_oneway_udt_init (cmsg_transport *transport)
{
    if (transport == NULL)
    {
        return;
    }

    transport->config.socket.family = PF_INET;
    transport->config.socket.sockaddr.generic.sa_family = PF_INET;

    memset (&transport->config.udt, 0, sizeof (transport->config.udt));
    transport->connect = cmsg_transport_oneway_udt_connect;
    transport->listen = cmsg_transport_oneway_udt_listen;
    transport->server_recv = cmsg_transport_oneway_udt_server_recv;
    transport->client_recv = cmsg_transport_oneway_udt_client_recv;
    transport->client_send = cmsg_transport_oneway_udt_client_send;
    transport->server_send = cmsg_transport_oneway_udt_server_send;
    transport->closure = cmsg_server_closure_oneway;
    transport->client_close = cmsg_transport_oneway_udt_client_close;
    transport->server_close = cmsg_transport_oneway_udt_server_close;

    transport->s_socket = cmsg_transport_oneway_udt_server_get_socket;
    transport->c_socket = cmsg_transport_oneway_udt_client_get_socket;

    transport->client_destroy = cmsg_transport_oneway_udt_client_destroy;
    transport->server_destroy = cmsg_transport_oneway_udt_server_destroy;

    transport->is_congested = cmsg_transport_oneway_udt_is_congested;
    transport->send_can_block_enable = cmsg_transport_udt_send_can_block_enable;
    transport->ipfree_bind_enable = cmsg_transport_udt_ipfree_bind_enable;

    CMSG_DEBUG (CMSG_INFO, "%s: done", __FUNCTION__);
}
