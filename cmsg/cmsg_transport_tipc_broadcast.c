#include "cmsg_private.h"
#include "cmsg_transport.h"
#include "cmsg_client.h"
#include "cmsg_server.h"
#include "cmsg_error.h"


/**
 * Creates the connectionless socket used to send messages using tipc.
 * Returns 0 on success or a negative integer on failure.
 */
static int32_t
cmsg_transport_tipc_broadcast_connect (cmsg_client *client)
{
    int ret;

    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] cmsg_transport_tipc_broadcast_connect\n");

    if (client == NULL)
        return 0;

    client->connection.socket = socket (client->_transport->config.socket.family,
                                        SOCK_RDM, 0);

    if (client->connection.socket < 0)
    {
        ret = -errno;
        client->state = CMSG_CLIENT_STATE_FAILED;
        CMSG_LOG_CLIENT_ERROR (client, "Unable to create socket. Error:%s",
                               strerror (errno));
        return ret;
    }

    client->state = CMSG_CLIENT_STATE_CONNECTED;
    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] successfully connected\n");

    return 0;
}


/**
 * Creates the connectionless socket used to receive tipc messages
 */
static int32_t
cmsg_transport_tipc_broadcast_listen (cmsg_server *server)
{
    int32_t listening_socket = -1;
    int32_t addrlen = 0;
    cmsg_transport *transport = NULL;

    if (server == NULL)
        return 0;

    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] Creating listen socket\n");
    server->connection.sockets.listening_socket = 0;
    transport = server->_transport;

    listening_socket = socket (transport->config.socket.family, SOCK_RDM, 0);
    if (listening_socket == -1)
    {
        CMSG_LOG_SERVER_ERROR (server, "Failed to create socket. Error:%s",
                               strerror (errno));

        return -1;
    }

    //TODO: stk_tipc.c adds the addressing information here

    addrlen = sizeof (transport->config.socket.sockaddr.tipc);
    /* bind the socket address (publishes the TIPC port name) */
    if (bind (listening_socket,
              (struct sockaddr *) &transport->config.socket.sockaddr.tipc, addrlen) != 0)
    {
        CMSG_LOG_SERVER_ERROR (server, "TIPC port could not be created");
        return -1;
    }

    //TODO: Do we need a listen?
    server->connection.sockets.listening_socket = listening_socket;
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

    nbytes = recvfrom (server->connection.sockets.listening_socket, buff, len, flags,
                       (struct sockaddr *) &transport->config.socket.sockaddr.tipc,
                       &addrlen);
    return nbytes;
}

/**
 * Receive a message sent by the client. The data is then passed to the server
 * for processing.
 */
static int32_t
cmsg_transport_tipc_broadcast_server_recv (int32_t socket, cmsg_server *server)
{
    int32_t ret = 0;

    if (!server || socket < 0)
    {
        return -1;
    }

    ret = cmsg_transport_server_recv_with_peek (cmsg_transport_tipc_broadcast_recv, server,
                                                server);

    return ret;
}


/**
 * TIPC broadcast clients do not receive a reply to their messages. This
 * function therefore returns NULL. It should not be called by the client, but
 * it prevents a null pointer exception from occurring if no function is
 * defined
 */
static cmsg_status_code
cmsg_transport_tipc_broadcast_client_recv (cmsg_client *client,
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
cmsg_transport_tipc_broadcast_client_send (cmsg_client *client, void *buff, int length,
                                           int flag)
{
    int retries = 0;
    int saved_errno = 0;

    int result = sendto (client->connection.socket,
                         buff,
                         length,
                         MSG_DONTWAIT,
                         (struct sockaddr *) &client->_transport->config.socket.
                         sockaddr.tipc,
                         sizeof (struct sockaddr_tipc));

    if (result != length)
    {
        CMSG_LOG_DEBUG ("[TRANSPORT] Failed to send tipc broadcast, result=%d, errno=%d\n",
                        result, errno);

        while (result != length && retries < 25)
        {
            usleep (50000);
            retries++;

            result = sendto (client->connection.socket,
                             buff,
                             length,
                             MSG_DONTWAIT,
                             (struct sockaddr *) &client->_transport->config.
                             socket.sockaddr.tipc, sizeof (struct sockaddr_tipc));

            saved_errno = errno;
        }
    }

    if (retries >= 25)
    {
        CMSG_LOG_CLIENT_ERROR (client,
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
cmsg_transport_tipc_broadcast_server_send (cmsg_server *server, void *buff, int length,
                                           int flag)
{
    return 0;
}


/**
 * Close the clients socket after a message has been sent.
 */
static void
cmsg_transport_tipc_broadcast_client_close (cmsg_client *client)
{
    if (client->connection.socket != -1)
    {
        CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] shutting down socket\n");
        shutdown (client->connection.socket, SHUT_RDWR);

        CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] closing socket\n");
        close (client->connection.socket);

        client->connection.socket = -1;
    }
}


/**
 * This function is called by the server to close the socket that the server
 * has used to receive a message from a client. TIPC broadcast does not use a
 * dedicated socket to do this, instead it receives messages on its listening
 * socket. Therefore this function does nothing when called.
 */
static void
cmsg_transport_tipc_broadcast_server_close (cmsg_server *server)
{
    return;
}


/**
 * Return the servers listening socket
 */
static int
cmsg_transport_tipc_broadcast_server_get_socket (cmsg_server *server)
{
    return server->connection.sockets.listening_socket;
}


/**
 * Return the socket the client will use to send messages
 */
static int
cmsg_transport_tipc_broadcast_client_get_socket (cmsg_client *client)
{
    return client->connection.socket;
}

/**
 * Close the client
 */
static void
cmsg_transport_tipc_broadcast_client_destroy (cmsg_client *cmsg_client)
{
    //placeholder to make sure destroy functions are called in the right order
}


/**
 * Close the servers listening socket
 */
static void
cmsg_transport_tipc_broadcast_server_destroy (cmsg_server *server)
{
    CMSG_DEBUG (CMSG_INFO, "[SERVER] Shutting down listening socket\n");
    shutdown (server->connection.sockets.listening_socket, SHUT_RDWR);

    CMSG_DEBUG (CMSG_INFO, "[SERVER] Closing listening socket\n");
    close (server->connection.sockets.listening_socket);
}


/**
 * TIPC BC can be congested but we don't check for it
 */
uint32_t
cmsg_transport_tipc_broadcast_is_congested (cmsg_client *client)
{
    return FALSE;
}


int32_t
cmsg_transport_tipc_broadcast_send_called_multi_threads_enable (cmsg_transport *transport,
                                                                uint32_t enable)
{
    // Don't support sending from multiple threads
    return -1;
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
        return;

    transport->config.socket.family = AF_TIPC;
    transport->config.socket.sockaddr.tipc.family = AF_TIPC;

    transport->connect = cmsg_transport_tipc_broadcast_connect;
    transport->listen = cmsg_transport_tipc_broadcast_listen;
    transport->server_recv = cmsg_transport_tipc_broadcast_server_recv;
    transport->client_recv = cmsg_transport_tipc_broadcast_client_recv;
    transport->client_send = cmsg_transport_tipc_broadcast_client_send;
    transport->server_send = cmsg_transport_tipc_broadcast_server_send;
    transport->client_close = cmsg_transport_tipc_broadcast_client_close;
    transport->server_close = cmsg_transport_tipc_broadcast_server_close;
    transport->s_socket = cmsg_transport_tipc_broadcast_server_get_socket;
    transport->c_socket = cmsg_transport_tipc_broadcast_client_get_socket;
    transport->client_destroy = cmsg_transport_tipc_broadcast_client_destroy;
    transport->server_destroy = cmsg_transport_tipc_broadcast_server_destroy;

    transport->is_congested = cmsg_transport_tipc_broadcast_is_congested;
    transport->send_called_multi_threads_enable =
        cmsg_transport_tipc_broadcast_send_called_multi_threads_enable;
    transport->send_called_multi_enabled = FALSE;
    transport->send_can_block_enable = cmsg_transport_tipc_broadcast_send_can_block_enable;
    transport->ipfree_bind_enable = cmsg_transport_tipc_broadcast_ipfree_bind_enable;

    transport->closure = cmsg_server_closure_oneway;
    transport->invoke_send = cmsg_client_invoke_send;
    transport->invoke_recv = NULL;
}
