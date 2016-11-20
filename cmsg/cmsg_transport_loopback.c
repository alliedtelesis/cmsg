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
#include "cmsg_client.h"
#include "cmsg_server.h"
#include "cmsg_error.h"


/******************************************************************************
 ******************** Client **************************************************
 *****************************************************************************/
/**
 * API always connects and therefore this should succeed.
 *
 * Returns 0 on success or a negative integer on failure.
 */
static int32_t
cmsg_transport_loopback_connect (cmsg_client *client)
{
    return 0;
}

/**
 * Nothing to do so return CMSG_STATUS_CODE_SERVICE_FAILED
 * shouldn't get here so it needs to be an error.
 */
static cmsg_status_code
cmsg_transport_loopback_oneway_client_recv (cmsg_client *client,
                                            ProtobufCMessage **messagePtPt)
{
    return CMSG_STATUS_CODE_SERVICE_FAILED;
}

/**
 * Nothing to send so return -1, shouldn't get here so it needs to be an error.
 */
static int32_t
cmsg_transport_loopback_client_send (cmsg_client *client, void *buff, int length, int flag)
{
    return -1;
}

/**
 * Nothing to close but this just means nothing to do
 */
static void
cmsg_transport_loopback_client_close (cmsg_client *client)
{
    return;
}

/**
 * We don't want the application listening to a socket (and there is none)
 * so return -1 so it will error.
 */
static int
cmsg_transport_loopback_client_get_socket (cmsg_client *client)
{
    return -1;
}

/**
 * Nothing to destroy
 */
static void
cmsg_transport_loopback_client_destroy (cmsg_client *cmsg_client)
{
    // placeholder to make sure destroy functions are called in the right order
}

/**
 * Loopback can't be congested so return FALSE
 */
uint32_t
cmsg_transport_loopback_is_congested (cmsg_client *client)
{
    return FALSE;
}

/**
 * This isn't supported on loopback at present, shouldn't be called so it
 * needs to be an error.
 */
int32_t
cmsg_transport_loopback_send_called_multi_threads_enable (cmsg_transport *transport,
                                                          uint32_t enable)
{
    // Don't support sending from multiple threads
    return -1;
}

/**
 * Sets the flag but it does nothing for this transport.
 */
int32_t
cmsg_transport_loopback_send_can_block_enable (cmsg_transport *transport,
                                               uint32_t send_can_block)
{
    transport->send_can_block = send_can_block;
    return 0;
}


int32_t
cmsg_transport_loopback_ipfree_bind_enable (cmsg_transport *transport,
                                            cmsg_bool_t use_ipfree_bind)
{
    /* not supported yet */
    return -1;
}


/******************************************************************************
 ******************** Server **************************************************
 *****************************************************************************/
/**
 * Nothing to listen to, so return 0 as the server always attempts to listen.
 */
static int32_t
cmsg_transport_loopback_listen (cmsg_server *server)
{
    return 0;
}

/**
 * Nothing to do - return -1 to make sure loopbacks can't do this.
 *
 */
static int32_t
cmsg_transport_loopback_server_recv (int32_t server_socket, cmsg_server *server)
{
    return -1;
}

/**
 * This is oneway only and so the server shouldn't send anything.
 * Return an error if this is attempted.
 */
static int32_t
cmsg_transport_loopback_oneway_server_send (cmsg_server *server, void *buff, int length,
                                            int flag)
{
    return -1;
}

/**
 * Nothing to close but this just means nothing to do
 */
static void
cmsg_transport_loopback_server_close (cmsg_server *server)
{
    return;
}

/**
 * We don't want the application listening to a socket (and there is none)
 * so return -1 so it will error.
 */
static int
cmsg_transport_loopback_server_get_socket (cmsg_server *server)
{
    return -1;
}

/**
 * Nothing to destroy
 */
static void
cmsg_transport_loopback_server_destroy (cmsg_server *cmsg_server)
{
    // placeholder to make sure destroy functions are called in the right order
}

void
cmsg_transport_oneway_loopback_init (cmsg_transport *transport)
{
    if (transport == NULL)
    {
        return;
    }

    /* No Socket information so don't set anything */

    transport->connect = cmsg_transport_loopback_connect;
    transport->listen = cmsg_transport_loopback_listen;
    transport->server_accept = NULL;
    transport->server_recv = cmsg_transport_loopback_server_recv;
    transport->client_recv = cmsg_transport_loopback_oneway_client_recv;
    transport->client_send = cmsg_transport_loopback_client_send;
    transport->server_send = cmsg_transport_loopback_oneway_server_send;
    transport->closure = cmsg_server_closure_oneway;
    transport->invoke_send = cmsg_client_invoke_send_direct;
    transport->invoke_recv = NULL;
    transport->client_close = cmsg_transport_loopback_client_close;
    transport->server_close = cmsg_transport_loopback_server_close;

    transport->s_socket = cmsg_transport_loopback_server_get_socket;
    transport->c_socket = cmsg_transport_loopback_client_get_socket;

    transport->client_destroy = cmsg_transport_loopback_client_destroy;
    transport->server_destroy = cmsg_transport_loopback_server_destroy;

    transport->is_congested = cmsg_transport_loopback_is_congested;
    transport->send_called_multi_threads_enable =
        cmsg_transport_loopback_send_called_multi_threads_enable;
    transport->send_called_multi_enabled = FALSE;
    transport->send_can_block_enable = cmsg_transport_loopback_send_can_block_enable;
    transport->ipfree_bind_enable = cmsg_transport_loopback_ipfree_bind_enable;

    CMSG_DEBUG (CMSG_INFO, "%s: done\n", __FUNCTION__);
}
