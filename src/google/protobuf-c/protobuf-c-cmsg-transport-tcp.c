#include "protobuf-c-cmsg-transport.h"
#include "protobuf-c-cmsg-client.h"
#include "protobuf-c-cmsg-server.h"


static int32_t
cmsg_transport_tcp_connect (cmsg_client *client)
{
    if (client == NULL)
        return 0;

    client->connection.socket = socket (client->_transport->config.socket.family,
                                        SOCK_STREAM, 0);

    if (client->connection.socket < 0)
    {
        client->state = CMSG_CLIENT_STATE_FAILED;
        DEBUG (CMSG_ERROR, "[TRANSPORT] error creating socket: %s\n", strerror (errno));
        return 0;
    }
    if (connect (client->connection.socket,
                 (struct sockaddr *) &client->_transport->config.socket.sockaddr.in,
                 sizeof (client->_transport->config.socket.sockaddr.in)) < 0)
    {
        if (errno == EINPROGRESS)
        {
            //?
        }
        close (client->connection.socket);
        client->connection.socket = 0;
        client->state = CMSG_CLIENT_STATE_FAILED;
        DEBUG (CMSG_ERROR,
               "[TRANSPORT] error connecting to remote host: %s\n", strerror (errno));

        return 0;
    }
    else
    {
        client->state = CMSG_CLIENT_STATE_CONNECTED;
        DEBUG (CMSG_INFO, "[TRANSPORT] succesfully connected\n");
        return 0;
    }
}


static int32_t
cmsg_transport_tcp_listen (cmsg_server *server)
{
    int32_t yes = 1;    // for setsockopt() SO_REUSEADDR, below
    int32_t listening_socket = -1;
    int32_t ret = 0;
    socklen_t addrlen = 0;
    cmsg_transport *transport = NULL;

    if (server == NULL)
        return 0;

    server->connection.sockets.listening_socket = 0;
    server->connection.sockets.client_socket = 0;

    transport = server->_transport;
    listening_socket = socket (transport->config.socket.family, SOCK_STREAM, 0);
    if (listening_socket == -1)
    {
        DEBUG (CMSG_ERROR, "[TRANSPORT] socket failed with: %s\n", strerror (errno));
        return -1;
    }

    ret = setsockopt (listening_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof (int32_t));
    if (ret == -1)
    {
        DEBUG (CMSG_ERROR, "[TRANSPORT] setsockopt failed with: %s\n", strerror (errno));
        close (listening_socket);
        return -1;
    }

    addrlen = sizeof (transport->config.socket.sockaddr.generic);

    ret = bind (listening_socket, &transport->config.socket.sockaddr.generic, addrlen);
    if (ret < 0)
    {
        DEBUG (CMSG_ERROR, "[TRANSPORT] bind failed with: %s\n", strerror (errno));
        close (listening_socket);
        return -1;
    }

    ret = listen (listening_socket, 10);
    if (ret < 0)
    {
        DEBUG (CMSG_ERROR, "[TRANSPORT] listen failed with: %s\n", strerror (errno));
        close (listening_socket);
        return -1;
    }

    server->connection.sockets.listening_socket = listening_socket;

    DEBUG (CMSG_INFO, "[TRANSPORT] listening on tcp socket: %d\n", listening_socket);

    DEBUG (CMSG_INFO,
           "[TRANSPORT] listening on port: %d\n",
           (int) (ntohs (server->_transport->config.socket.sockaddr.in.sin_port)));

    return 0;
}


/* Wrapper function to call "recv" on a TCP socket */
int
cmsg_transport_tcp_recv (void *handle, void *buff, int len, int flags)
{
    int *sock = (int *) handle;

    return recv (*sock, buff, len, flags);
}


static int32_t
cmsg_transport_tcp_server_recv (int32_t server_socket, cmsg_server *server)
{
    int32_t ret = 0;

    if (!server || server_socket < 0)
    {
        DEBUG (CMSG_ERROR, "[TRANSPORT] error server/socket invalid\n");
        return -1;
    }

    /* Remember the client socket to use when send reply */
    server->connection.sockets.client_socket = server_socket;

    ret = cmsg_transport_server_recv (cmsg_transport_tcp_recv,
                                      (void *) &server_socket, server);

    return ret;
}


static int32_t
cmsg_transport_tcp_server_accept (int32_t listen_socket, cmsg_server *server)
{
    int32_t client_len;
    cmsg_transport client_transport;
    int sock;
    int32_t ret = 0;

    if (!server || listen_socket < 0)
    {
        DEBUG (CMSG_ERROR, "[TRANSPORT] error server/socket invalid\n");
        return -1;
    }

    client_len = sizeof (client_transport.config.socket.sockaddr.in);
    sock = accept (listen_socket,
                   (struct sockaddr *) &client_transport.config.socket.sockaddr.in,
                   &client_len);

    if (sock < 0)
    {
        DEBUG (CMSG_ERROR, "[TRANSPORT] error accept failed: %s\n", strerror (errno));
        DEBUG (CMSG_INFO, "[TRANSPORT] sock = %d\n", sock);

        return -1;
    }

    return sock;
}


static cmsg_status_code
cmsg_transport_tcp_client_recv (cmsg_client *client, ProtobufCMessage **messagePtPt)
{
    int32_t nbytes = 0;
    int32_t dyn_len = 0;
    ProtobufCMessage *ret = NULL;
    cmsg_header header_received;
    cmsg_header header_converted;
    uint8_t *recv_buffer = 0;
    uint8_t *buffer = 0;
    uint8_t buf_static[512];
    ProtobufCMessage *message = NULL;
    ProtobufCAllocator *allocator = (ProtobufCAllocator *) client->allocator;

    if (!client)
    {
        *messagePtPt = NULL;
        return CMSG_STATUS_CODE_SUCCESS;
    }

    nbytes = recv (client->connection.socket,
                   &header_received, sizeof (cmsg_header), MSG_WAITALL);

    if (nbytes == sizeof (cmsg_header))
    {
        if (cmsg_header_process (&header_received, &header_converted) != CMSG_RET_OK)
        {
            // Couldn't process the header for some reason
            CMSG_LOG_USER_ERROR ("[TRANSPORT] server receive couldn't process msg header");
            return CMSG_RET_ERR;
        }

        DEBUG (CMSG_INFO, "[TRANSPORT] received response header\n");

        // read the message

        // There is no more data to read so exit.
        if (header_converted.message_length == 0)
        {
            // May have been queued, dropped or there was no message returned
            DEBUG (CMSG_INFO,
                   "[TRANSPORT] received response without data. server status %d\n",
                   header_converted.status_code);
            *messagePtPt = NULL;
            return header_converted.status_code;
        }

        // Take into account that someone may have changed the size of the header
        // and we don't know about it, make sure we receive all the information.
        dyn_len = header_converted.message_length +
                  (header_converted.header_length - sizeof (cmsg_header));
        if (dyn_len > sizeof buf_static)
        {
            recv_buffer = malloc (dyn_len);
        }
        else
        {
            recv_buffer = (void *) buf_static;
        }

        //just recv the rest of the data to clear the socket
        nbytes = recv (client->connection.socket, recv_buffer, dyn_len, MSG_WAITALL);

        if (nbytes == dyn_len)
        {
            // Set buffer to take into account a larger header than we expected
            buffer = recv_buffer + (header_converted.header_length - sizeof (cmsg_header));

            DEBUG (CMSG_INFO, "[TRANSPORT] received response data\n");
            cmsg_buffer_print (buffer, dyn_len);

            //todo: call cmsg_client_response_message_processor

            DEBUG (CMSG_INFO, "[TRANSPORT] unpacking response message\n");

            message =
                protobuf_c_message_unpack (client->descriptor->methods[header_converted.method_index].output,
                                           allocator, header_converted.message_length,
                                           buffer);

            if (message == NULL)
            {
                DEBUG (CMSG_ERROR, "[TRANSPORT] error unpacking response message\n");
                *messagePtPt = NULL;
                return CMSG_STATUS_CODE_SERVICE_FAILED;
            }
            *messagePtPt = message;
            return CMSG_STATUS_CODE_SUCCESS;
        }
        else
        {
            DEBUG (CMSG_INFO, "[TRANSPORT] recv socket %d no data\n",
                   client->connection.socket);

            ret = 0;
        }
        if (recv_buffer != (void *) buf_static)
        {
            if (recv_buffer)
            {
                free (recv_buffer);
                recv_buffer = 0;
            }
        }
    }
    else if (nbytes > 0)
    {
        DEBUG (CMSG_INFO,
               "[TRANSPORT] recv socket %d bad header nbytes %d\n",
               client->connection.socket, nbytes);

        // TEMP to keep things going
        recv_buffer = malloc (nbytes);
        nbytes = recv (client->connection.socket, recv_buffer, nbytes, MSG_WAITALL);
        free (recv_buffer);
        recv_buffer = 0;
        ret = 0;
    }
    else if (nbytes == 0)
    {
        //Normal socket shutdown case. Return other than TRANSPORT_OK to
        //have socket removed from select set.
        ret = 0;
    }
    else
    {
        //Error while peeking at socket data.
        if (errno != ECONNRESET)
        {
            DEBUG (CMSG_ERROR,
                   "[TRANSPORT] recv socket %d error: %s\n",
                   client->connection.socket, strerror (errno));
        }
        ret = 0;
    }

    *messagePtPt = NULL;
    return CMSG_STATUS_CODE_SERVICE_FAILED;
}


static int32_t
cmsg_transport_tcp_client_send (cmsg_client *client, void *buff, int length, int flag)
{
    return (send (client->connection.socket, buff, length, flag));
}

static int32_t
cmsg_transport_tcp_server_send (cmsg_server *server, void *buff, int length, int flag)
{
    return (send (server->connection.sockets.client_socket, buff, length, flag));
}

static void
cmsg_transport_tcp_client_close (cmsg_client *client)
{
    DEBUG (CMSG_INFO, "[TRANSPORT] shutting down socket\n");
    shutdown (client->connection.socket, 2);

    DEBUG (CMSG_INFO, "[TRANSPORT] closing socket\n");
    close (client->connection.socket);
}

static void
cmsg_transport_tcp_server_close (cmsg_server *server)
{
    DEBUG (CMSG_INFO, "[SERVER] shutting down socket\n");
    shutdown (server->connection.sockets.client_socket, 2);

    DEBUG (CMSG_INFO, "[SERVER] closing socket\n");
    close (server->connection.sockets.client_socket);
}

static int
cmsg_transport_tcp_server_get_socket (cmsg_server *server)
{
    return server->connection.sockets.listening_socket;
}


static int
cmsg_transport_tcp_client_get_socket (cmsg_client *client)
{
    return client->connection.socket;
}

static void
cmsg_transport_tcp_client_destroy (cmsg_client *cmsg_client)
{
    //placeholder to make sure destroy functions are called in the right order
}

static void
cmsg_transport_tcp_server_destroy (cmsg_server *server)
{
    DEBUG (CMSG_INFO, "[SERVER] Shutting down listening socket\n");
    shutdown (server->connection.sockets.listening_socket, 2);

    DEBUG (CMSG_INFO, "[SERVER] Closing listening socket\n");
    close (server->connection.sockets.listening_socket);
}


/**
 * TCP is never congested
 */
uint32_t
cmsg_transport_tcp_is_congested (cmsg_client *client)
{
    return FALSE;
}

int32_t
cmsg_transport_tcp_send_called_multi_threads_enable (cmsg_transport *transport,
                                                     uint32_t enable)
{
    // Don't support sending from multiple threads
    return -1;
}


int32_t
cmsg_transport_tcp_send_can_block_enable (cmsg_transport *transport,
                                          uint32_t send_can_block)
{
    transport->send_can_block = send_can_block;
    return 0;
}


void
cmsg_transport_tcp_init (cmsg_transport *transport)
{
    if (transport == NULL)
        return;

    transport->config.socket.family = PF_INET;
    transport->config.socket.sockaddr.generic.sa_family = PF_INET;

    transport->connect = cmsg_transport_tcp_connect;
    transport->listen = cmsg_transport_tcp_listen;
    transport->server_accept = cmsg_transport_tcp_server_accept;
    transport->server_recv = cmsg_transport_tcp_server_recv;
    transport->client_recv = cmsg_transport_tcp_client_recv;
    transport->client_send = cmsg_transport_tcp_client_send;
    transport->server_send = cmsg_transport_tcp_server_send;
    transport->closure = cmsg_server_closure_rpc;
    transport->invoke = cmsg_client_invoke_rpc;
    transport->client_close = cmsg_transport_tcp_client_close;
    transport->server_close = cmsg_transport_tcp_server_close;

    transport->s_socket = cmsg_transport_tcp_server_get_socket;
    transport->c_socket = cmsg_transport_tcp_client_get_socket;

    transport->client_destroy = cmsg_transport_tcp_client_destroy;
    transport->server_destroy = cmsg_transport_tcp_server_destroy;

    transport->is_congested = cmsg_transport_tcp_is_congested;
    transport->send_called_multi_threads_enable =
        cmsg_transport_tcp_send_called_multi_threads_enable;
    transport->send_called_multi_enabled = FALSE;
    transport->send_can_block_enable = cmsg_transport_tcp_send_can_block_enable;

    DEBUG (CMSG_INFO, "%s: done\n", __FUNCTION__);
}


void
cmsg_transport_oneway_tcp_init (cmsg_transport *transport)
{
    if (transport == NULL)
        return;

    transport->config.socket.family = PF_INET;
    transport->config.socket.sockaddr.generic.sa_family = PF_INET;

    transport->connect = cmsg_transport_tcp_connect;
    transport->listen = cmsg_transport_tcp_listen;
    transport->server_accept = cmsg_transport_tcp_server_accept;
    transport->server_recv = cmsg_transport_tcp_server_recv;
    transport->client_recv = cmsg_transport_tcp_client_recv;
    transport->client_send = cmsg_transport_tcp_client_send;
    transport->server_send = cmsg_transport_tcp_server_send;
    transport->closure = cmsg_server_closure_oneway;
    transport->invoke = cmsg_client_invoke_oneway;
    transport->client_close = cmsg_transport_tcp_client_close;
    transport->server_close = cmsg_transport_tcp_server_close;

    transport->s_socket = cmsg_transport_tcp_server_get_socket;
    transport->c_socket = cmsg_transport_tcp_client_get_socket;

    transport->client_destroy = cmsg_transport_tcp_client_destroy;
    transport->server_destroy = cmsg_transport_tcp_server_destroy;

    transport->is_congested = cmsg_transport_tcp_is_congested;
    transport->send_called_multi_threads_enable =
        cmsg_transport_tcp_send_called_multi_threads_enable;
    transport->send_called_multi_enabled = FALSE;
    transport->send_can_block_enable = cmsg_transport_tcp_send_can_block_enable;

    DEBUG (CMSG_INFO, "%s: done\n", __FUNCTION__);
}
