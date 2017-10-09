/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#include "cmsg_private.h"
#include "cmsg_transport.h"
#include "cmsg_server.h"
#include "cmsg_error.h"


/**
 * Creates the connectionless socket used to send messages using tipc.
 * Returns 0 on success or a negative integer on failure.
 */
static int32_t
cmsg_transport_tipc_broadcast_connect (cmsg_transport *transport, int timeout)
{
    int ret;

    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] cmsg_transport_tipc_broadcast_connect\n");

    transport->connection.sockets.client_socket = socket (transport->config.socket.family,
                                                          SOCK_RDM, 0);

    if (transport->connection.sockets.client_socket < 0)
    {
        ret = -errno;
        CMSG_LOG_TRANSPORT_ERROR (transport, "Unable to create socket. Error:%s",
                                  strerror (errno));
        return ret;
    }

    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] successfully connected\n");

    return 0;
}


/**
 * Creates the connectionless socket used to receive tipc messages
 */
static int32_t
cmsg_transport_tipc_broadcast_listen (cmsg_transport *transport)
{
    int32_t listening_socket = -1;
    int32_t addrlen = 0;

    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] Creating listen socket\n");

    listening_socket = socket (transport->config.socket.family, SOCK_RDM, 0);
    if (listening_socket == -1)
    {
        CMSG_LOG_TRANSPORT_ERROR (transport, "Failed to create socket. Error:%s",
                                  strerror (errno));

        return -1;
    }

    //TODO: stk_tipc.c adds the addressing information here

    addrlen = sizeof (transport->config.socket.sockaddr.tipc);
    /* bind the socket address (publishes the TIPC port name) */
    if (bind (listening_socket,
              (struct sockaddr *) &transport->config.socket.sockaddr.tipc, addrlen) != 0)
    {
        CMSG_LOG_TRANSPORT_ERROR (transport, "TIPC port could not be created");
        return -1;
    }

    //TODO: Do we need a listen?
    transport->connection.sockets.listening_socket = listening_socket;
    //TODO: Add debug
    return 0;
}


/* Wrapper function to call "recv" on a TIPC broadcast socket */
int
cmsg_transport_tipc_broadcast_recv (void *handle, void *buff, int len, int flags)
{
    uint32_t addrlen = 0;
    cmsg_server *server;
    cmsg_transport *transport;
    int nbytes;

    server = (cmsg_server *) handle;
    addrlen = sizeof (struct sockaddr_tipc);
    transport = server->_transport;

    nbytes =
        recvfrom (server->_transport->connection.sockets.listening_socket, buff, len, flags,
                  (struct sockaddr *) &transport->config.socket.sockaddr.tipc, &addrlen);
    return nbytes;
}

/**
 * Receive a message sent by the client. The data is then passed to the server
 * for processing.
 */
static int32_t
cmsg_transport_tipc_broadcast_server_recv (int32_t socket, cmsg_server *server)
{
    int32_t ret = CMSG_RET_ERR;
    cmsg_status_code peek_status;
    int server_sock;
    cmsg_header header_received;

    if (!server || socket < 0)
    {
        return -1;
    }

    server_sock = server->_transport->connection.sockets.listening_socket;
    peek_status = cmsg_transport_peek_for_header (cmsg_transport_tipc_broadcast_recv,
                                                  (void *) server,
                                                  server->_transport,
                                                  server_sock, MAX_SERVER_PEEK_LOOP,
                                                  &header_received);
    if (peek_status == CMSG_STATUS_CODE_SUCCESS)
    {
        ret = cmsg_transport_server_recv (cmsg_transport_tipc_broadcast_recv, server,
                                          server, &header_received);
    }
    else if (peek_status == CMSG_STATUS_CODE_CONNECTION_CLOSED)
    {
        ret = CMSG_RET_CLOSED;
    }

    return ret;
}


/**
 * TIPC broadcast clients do not receive a reply to their messages. This
 * function therefore returns NULL. It should not be called by the client, but
 * it prevents a null pointer exception from occurring if no function is
 * defined
 */
static cmsg_status_code
cmsg_transport_tipc_broadcast_client_recv (cmsg_transport *transport,
                                           const ProtobufCServiceDescriptor *descriptor,
                                           ProtobufCMessage **messagePtPt)
{
    *messagePtPt = NULL;
    return CMSG_STATUS_CODE_SUCCESS;
}


/**
 * Send the data in buff to the server specified in the clients transports
 * addressing structure. Does not block.
 */
static int32_t
cmsg_transport_tipc_broadcast_client_send (cmsg_transport *transport, void *buff,
                                           int length, int flag)
{
    int retries = 0;
    int saved_errno = 0;

    int result = sendto (transport->connection.sockets.client_socket, buff, length,
                         MSG_DONTWAIT,
                         (struct sockaddr *) &transport->config.socket.sockaddr.tipc,
                         sizeof (struct sockaddr_tipc));

    if (result != length)
    {
        CMSG_LOG_DEBUG ("[TRANSPORT] Failed to send tipc broadcast, result=%d, errno=%d\n",
                        result, errno);

        while (result != length && retries < 25)
        {
            usleep (50000);
            retries++;

            result = sendto (transport->connection.sockets.client_socket, buff, length,
                             MSG_DONTWAIT,
                             (struct sockaddr *) &transport->config.socket.sockaddr.tipc,
                             sizeof (struct sockaddr_tipc));

            saved_errno = errno;
        }
    }

    if (retries >= 25)
    {
        CMSG_LOG_TRANSPORT_ERROR (transport,
                                  "Failed to send tipc broadcast message. Exceeded %d retries. Last error: %s.",
                                  retries, strerror (saved_errno));
        errno = saved_errno;
    }
    else if (retries > 0)
    {
        CMSG_LOG_DEBUG ("[TRANSPORT] Succeeded sending tipc broadcast (retries=%d)\n",
                        retries);
    }

    return result;
}


/**
 * TIPC broadcast servers do not send replies to received messages. This
 * function therefore returns 0. It should not be called by the server, but
 * it prevents a null pointer exception from occurring if no function is
 * defined
 */
static int32_t
cmsg_transport_tipc_broadcast_server_send (cmsg_transport *transport, void *buff,
                                           int length, int flag)
{
    return 0;
}


/**
 * Close the clients socket after a message has been sent.
 */
static void
cmsg_transport_tipc_broadcast_client_close (cmsg_transport *transport)
{
    if (transport->connection.sockets.client_socket != -1)
    {
        CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] shutting down socket\n");
        shutdown (transport->connection.sockets.client_socket, SHUT_RDWR);

        CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] closing socket\n");
        close (transport->connection.sockets.client_socket);

        transport->connection.sockets.client_socket = -1;
    }
}


/**
 * This function is called by the server to close the socket that the server
 * has used to receive a message from a client. TIPC broadcast does not use a
 * dedicated socket to do this, instead it receives messages on its listening
 * socket. Therefore this function does nothing when called.
 */
static void
cmsg_transport_tipc_broadcast_server_close (cmsg_transport *transport)
{
    return;
}


/**
 * Return the servers listening socket
 */
static int
cmsg_transport_tipc_broadcast_server_get_socket (cmsg_transport *transport)
{
    return transport->connection.sockets.listening_socket;
}


/**
 * Return the socket the client will use to send messages
 */
static int
cmsg_transport_tipc_broadcast_client_get_socket (cmsg_transport *transport)
{
    return transport->connection.sockets.client_socket;
}

/**
 * Close the client
 */
static void
cmsg_transport_tipc_broadcast_client_destroy (cmsg_transport *transport)
{
    //placeholder to make sure destroy functions are called in the right order
}


/**
 * Close the servers listening socket
 */
static void
cmsg_transport_tipc_broadcast_server_destroy (cmsg_transport *transport)
{
    if (transport->connection.sockets.listening_socket != -1)
    {
        CMSG_DEBUG (CMSG_INFO, "[SERVER] Shutting down listening socket\n");
        shutdown (transport->connection.sockets.listening_socket, SHUT_RDWR);

        CMSG_DEBUG (CMSG_INFO, "[SERVER] Closing listening socket\n");
        close (transport->connection.sockets.listening_socket);
    }
}


/**
 * TIPC BC can be congested but we don't check for it
 */
uint32_t
cmsg_transport_tipc_broadcast_is_congested (cmsg_transport *transport)
{
    return FALSE;
}


int32_t
cmsg_transport_tipc_broadcast_send_can_block_enable (cmsg_transport *transport,
                                                     uint32_t send_can_block)
{
    // Don't support send blocking
    return -1;
}


int32_t
cmsg_transport_tipc_broadcast_ipfree_bind_enable (cmsg_transport *transport,
                                                  cmsg_bool_t use_ipfree_bind)
{
    /* not supported yet */
    return -1;
}


/**
 * Setup the transport structure with the appropriate function pointers for
 * TIPC broadcast, and transport family.
 */
void
cmsg_transport_tipc_broadcast_init (cmsg_transport *transport)
{
    if (transport == NULL)
    {
        return;
    }

    transport->config.socket.family = AF_TIPC;
    transport->config.socket.sockaddr.tipc.family = AF_TIPC;

    transport->tport_funcs.connect = cmsg_transport_tipc_broadcast_connect;
    transport->tport_funcs.listen = cmsg_transport_tipc_broadcast_listen;
    transport->tport_funcs.server_recv = cmsg_transport_tipc_broadcast_server_recv;
    transport->tport_funcs.client_recv = cmsg_transport_tipc_broadcast_client_recv;
    transport->tport_funcs.client_send = cmsg_transport_tipc_broadcast_client_send;
    transport->tport_funcs.server_send = cmsg_transport_tipc_broadcast_server_send;
    transport->tport_funcs.client_close = cmsg_transport_tipc_broadcast_client_close;
    transport->tport_funcs.server_close = cmsg_transport_tipc_broadcast_server_close;
    transport->tport_funcs.s_socket = cmsg_transport_tipc_broadcast_server_get_socket;
    transport->tport_funcs.c_socket = cmsg_transport_tipc_broadcast_client_get_socket;
    transport->tport_funcs.client_destroy = cmsg_transport_tipc_broadcast_client_destroy;
    transport->tport_funcs.server_destroy = cmsg_transport_tipc_broadcast_server_destroy;

    transport->tport_funcs.is_congested = cmsg_transport_tipc_broadcast_is_congested;
    transport->tport_funcs.send_can_block_enable =
        cmsg_transport_tipc_broadcast_send_can_block_enable;
    transport->tport_funcs.ipfree_bind_enable =
        cmsg_transport_tipc_broadcast_ipfree_bind_enable;

    transport->tport_funcs.closure = cmsg_server_closure_oneway;
}
