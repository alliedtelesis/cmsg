/**
 * protobuf-c-transport-udt.c
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
 */
#include "protobuf-c-cmsg-transport.h"
#include "protobuf-c-cmsg-client.h"
#include "protobuf-c-cmsg-server.h"

/*
 *
 */
static int32_t
cmsg_transport_oneway_udt_listen (cmsg_server *server)
{
    // Function isn't needed for User Defined so nothing happens.
    return 0;
}



static int32_t
cmsg_transport_oneway_udt_server_recv (int32_t socket, cmsg_server *server)
{
    // Function isn't needed for User Defined so nothing happens.
    return 0;
}


static ProtobufCMessage *
cmsg_transport_oneway_udt_client_recv (cmsg_client *client)
{
    // Function isn't needed for User Defined so nothing happens.
    return 0;
}


static  int32_t
cmsg_transport_oneway_udt_server_send (cmsg_server *server, void *buff, int length, int flag)
{
    // Function isn't needed for User Defined so nothing happens.
    return 0;
}


static void
cmsg_transport_oneway_udt_client_close (cmsg_client *client)
{
    // Function isn't needed for User Defined so nothing happens.
    return;
}


static void
cmsg_transport_oneway_udt_server_close (cmsg_server *server)
{
    // Function isn't needed for User Defined so nothing happens.
    return;
}


static int
cmsg_transport_oneway_udt_server_get_socket (cmsg_server *server)
{
    // Function isn't needed for User Defined so nothing happens.
    return 0;
}


static int
cmsg_transport_oneway_udt_client_get_socket (cmsg_client *client)
{
    // Function isn't needed for User Defined so nothing happens.
    return 0;
}

static void
cmsg_transport_oneway_udt_server_destroy (cmsg_server *server)
{
    // Function isn't needed for User Defined so nothing happens.
    return;
}

static int32_t
cmsg_transport_oneway_udt_client_send (cmsg_client *client, void *buff, int length, int flag)
{

    if (client->transport->config.udt.send)
    {
        return (client->transport->config.udt.send (client->transport->config.udt.udt_data, buff, length, flag));
    }

    // Function isn't defined so just pretend the message was sent.
    return 0;
}


/*
 * Call the user defined transport connect function and change the state of
 * the client connection to connected.
 */
static int32_t
cmsg_transport_oneway_udt_connect (cmsg_client *client)
{
    int32_t ret = 0;
    if (client->transport->config.udt.connect)
    {
        ret = client->transport->config.udt.connect (client);
    }
    client->state = CMSG_CLIENT_STATE_CONNECTED;

    return ret;
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
    transport->invoke = cmsg_client_invoke_oneway;
    transport->client_close = cmsg_transport_oneway_udt_client_close;
    transport->server_close = cmsg_transport_oneway_udt_server_close;

    transport->s_socket = 0;
    transport->c_socket = 0;

    transport->server_destroy = cmsg_transport_oneway_udt_server_destroy;

    DEBUG (CMSG_INFO, "%s: done", __FUNCTION__);
}


