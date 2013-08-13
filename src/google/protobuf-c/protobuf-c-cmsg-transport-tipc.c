#include "protobuf-c-cmsg-transport.h"
#include "protobuf-c-cmsg-client.h"
#include "protobuf-c-cmsg-server.h"



static int32_t
cmsg_transport_tipc_connect (cmsg_client *client)
{
    DEBUG (CMSG_INFO, "[TRANSPORT] cmsg_transport_tipc_connect\n");

    if (client == NULL)
        return 0;

    client->connection.socket = socket (client->transport->config.socket.family,
                                        SOCK_STREAM,
                                        0);

    if (client->connection.socket < 0)
    {
        client->state = CMSG_CLIENT_STATE_FAILED;
        DEBUG (CMSG_ERROR,
               "[TRANSPORT] error creating socket: %s\n",
               strerror (errno));

        return 0;
    }

    if (connect (client->connection.socket,
                 (struct sockaddr *)&client->transport->config.socket.sockaddr.tipc,
                 sizeof (client->transport->config.socket.sockaddr.tipc)) < 0)
    {
        shutdown (client->connection.socket, 2);
        close (client->connection.socket);
        client->connection.socket = 0;
        client->state = CMSG_CLIENT_STATE_FAILED;
        DEBUG (CMSG_ERROR,
               "[TRANSPORT] error connecting to remote host: %s\n",
               strerror (errno));

        return 0;
    }
    else
    {
        client->state = CMSG_CLIENT_STATE_CONNECTED;
        DEBUG (CMSG_INFO, "[TRANSPORT] successfully connected\n");
        return 0;
    }
}



static int32_t
cmsg_transport_tipc_listen (cmsg_server *server)
{
    int32_t yes = 1; // for setsockopt() SO_REUSEADDR, below
    int32_t listening_socket = -1;
    int32_t ret = 0;
    socklen_t addrlen = 0;
    cmsg_transport *transport = NULL;

    if (server == NULL)
        return 0;

    server->connection.sockets.listening_socket = 0;
    server->connection.sockets.client_socket = 0;

    transport = server->transport;
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

    addrlen  = sizeof (transport->config.socket.sockaddr.generic);

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

    DEBUG (CMSG_INFO,
           "[TRANSPORT] listening on tipc socket: %d\n",
           listening_socket);

    DEBUG (CMSG_INFO,
           "[TRANSPORT] listening on tipc type: %d\n",
           server->transport->config.socket.sockaddr.tipc.addr.name.name.type);

    DEBUG (CMSG_INFO,
           "[TRANSPORT] listening on tipc instance: %d\n",
           server->transport->config.socket.sockaddr.tipc.addr.name.name.instance);

    DEBUG (CMSG_INFO,
           "[TRANSPORT] listening on tipc domain: %d\n",
           server->transport->config.socket.sockaddr.tipc.addr.name.domain);

    DEBUG (CMSG_INFO,
           "[TRANSPORT] listening on tipc scope: %d\n",
           server->transport->config.socket.sockaddr.tipc.scope);

    return 0;
}


/* Wrapper function to call "recv" on a TIPC socket */
int
cmsg_transport_tipc_recv (void *handle, void *buff, int len, int flags)
{
    int sock = (int) handle;

    return recv (sock, buff, len, flags);
}


static int32_t
cmsg_transport_tipc_server_recv (int32_t socket, cmsg_server *server)
{
    int32_t client_len;
    cmsg_transport client_transport;
    int sock;
    int32_t ret = 0;

    if (!server || socket < 0)
    {
        return -1;
    }

    client_len = sizeof (client_transport.config.socket.sockaddr.tipc);
    sock = accept (socket,
                   (struct sockaddr *) &client_transport.config.socket.sockaddr.tipc,
                   &client_len);
    server->connection.sockets.client_socket = sock;

    if (sock < 0)
    {
        DEBUG (CMSG_ERROR, "[TRANSPORT] accept failed\n");
        DEBUG (CMSG_INFO, "[TRANSPORT] sock = %d\n", sock);

        return -1;
    }


    DEBUG (CMSG_INFO, "[TRANSPORT] server->accecpted_client_socket %d\n", sock);

    ret = cmsg_transport_server_recv (cmsg_transport_tipc_recv, (void *) sock, server);

    return ret;
}


static ProtobufCMessage *
cmsg_transport_tipc_client_recv (cmsg_client *client)
{
    int32_t nbytes = 0;
    int32_t dyn_len = 0;
    ProtobufCMessage *ret = NULL;
    cmsg_header_response header_received;
    cmsg_header_response header_converted;
    uint8_t *buffer = 0;
    uint8_t buf_static[512];

    if (!client)
    {
        return 0;
    }

    nbytes = recv (client->connection.socket,
                   &header_received, sizeof (cmsg_header_response), MSG_WAITALL);

    if (nbytes == sizeof (cmsg_header_response))
    {
        //we have little endian on the wire
        header_converted.status_code = cmsg_common_uint32_from_le (header_received.status_code);
        header_converted.method_index = cmsg_common_uint32_from_le (header_received.method_index);
        header_converted.message_length = cmsg_common_uint32_from_le (header_received.message_length);
        header_converted.request_id = header_received.request_id;

        DEBUG (CMSG_INFO, "[TRANSPORT] received response header\n");
        cmsg_buffer_print ((void *)&header_received, sizeof (cmsg_header_response));

        DEBUG (CMSG_INFO,
               "[TRANSPORT] status_code    host: %d, wire: %d\n",
               header_converted.status_code, header_received.status_code);

        DEBUG (CMSG_INFO,
               "[TRANSPORT] method_index   host: %d, wire: %d\n",
               header_converted.method_index, header_received.method_index);

        DEBUG (CMSG_INFO,
               "[TRANSPORT] message_length host: %d, wire: %d\n",
               header_converted.message_length, header_received.message_length);

        DEBUG (CMSG_INFO,
               "[TRANSPORT] request_id     host: %d, wire: %d\n",
               header_converted.request_id, header_received.request_id);

        if (header_converted.status_code != CMSG_STATUS_CODE_SUCCESS)
        {
            DEBUG (CMSG_INFO, "[TRANSPORT] server could not process message correctly\n");
            DEBUG (CMSG_INFO, "[TRANSPORT] todo: handle this case better\n");
        }

        // read the message
        dyn_len = header_converted.message_length;

        if (dyn_len > sizeof buf_static)
        {
            buffer = malloc (dyn_len);
        }
        else
        {
            buffer = (void *)buf_static;
        }

        //just recv more data when the packed message length is greater zero
        if (header_converted.message_length)
            nbytes = recv (client->connection.socket, buffer, dyn_len, MSG_WAITALL);
        else
        {
            DEBUG (CMSG_INFO, "[TRANSPORT] received response without data\n");
            return NULL;
        }

        if (nbytes == dyn_len)
        {
            DEBUG (CMSG_INFO, "[TRANSPORT] received response data\n");
            cmsg_buffer_print (buffer, dyn_len);

            //todo: call cmsg_client_response_message_processor
            ProtobufCMessage *message = 0;
            ProtobufCAllocator *allocator = (ProtobufCAllocator *)client->allocator;

            DEBUG (CMSG_INFO, "[TRANSPORT] unpacking response message\n");

            message = protobuf_c_message_unpack (client->descriptor->methods[header_converted.method_index].output,
                                                 allocator,
                                                 header_converted.message_length,
                                                 buffer);

            if (message == NULL)
            {
                DEBUG (CMSG_ERROR, "[TRANSPORT] error unpacking response message\n");
                return NULL;
            }
            return message;
        }
        else
        {
            DEBUG (CMSG_INFO,
                   "[TRANSPORT] recv socket %d no data\n",
                   client->connection.socket);

            ret = 0;
        }
        if (buffer != (void *)buf_static)
        {
            if (buffer)
            {
                free (buffer);
                buffer = 0;
            }
        }
    }
    else if (nbytes > 0)
    {
        DEBUG (CMSG_ERROR,
               "[TRANSPORT] recv socket %d bad header nbytes %d\n",
               client->connection.socket, nbytes);

        // TEMP to keep things going
        buffer = malloc (nbytes);
        nbytes = recv (client->connection.socket, buffer , nbytes, MSG_WAITALL);
        free (buffer);
        buffer = 0;
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

    return 0;
}


static  int32_t
cmsg_transport_tipc_client_send (cmsg_client *client, void *buff, int length, int flag)
{
    return (send (client->connection.socket, buff, length, flag));
}

static  int32_t
cmsg_transport_tipc_server_send (cmsg_server *server, void *buff, int length, int flag)
{
    return (send (server->connection.sockets.client_socket, buff, length, flag));
}

static void
cmsg_transport_tipc_client_close (cmsg_client *client)
{
    DEBUG (CMSG_INFO, "[TRANSPORT] shutting down socket\n");
    shutdown (client->connection.socket, 2);

    DEBUG (CMSG_INFO, "[TRANSPORT] closing socket\n");
    close (client->connection.socket);
}

static void
cmsg_transport_tipc_server_close (cmsg_server *server)
{
    DEBUG (CMSG_INFO, "[SERVER] shutting down socket\n");
    shutdown (server->connection.sockets.client_socket, 2);

    DEBUG (CMSG_INFO, "[SERVER] closing socket\n");
    close (server->connection.sockets.client_socket);
}


static int
cmsg_transport_tipc_server_get_socket (cmsg_server *server)
{
    return server->connection.sockets.listening_socket;
}


static int
cmsg_transport_tipc_client_get_socket (cmsg_client *client)
{
    return client->connection.socket;
}

static void
cmsg_transport_tipc_server_destroy (cmsg_server *server)
{
    DEBUG (CMSG_INFO, "[SERVER] Shutting down listening socket\n");
    shutdown (server->connection.sockets.listening_socket, 2);

    DEBUG (CMSG_INFO, "[SERVER] Closing listening socket\n");
    close (server->connection.sockets.listening_socket);
}


void
cmsg_transport_tipc_init (cmsg_transport *transport)
{
    if (transport == NULL)
        return;

    transport->config.socket.family = PF_TIPC;
    transport->config.socket.sockaddr.generic.sa_family = PF_TIPC;
    transport->connect = cmsg_transport_tipc_connect;
    transport->listen = cmsg_transport_tipc_listen;
    transport->server_recv = cmsg_transport_tipc_server_recv;
    transport->client_recv = cmsg_transport_tipc_client_recv;
    transport->client_send = cmsg_transport_tipc_client_send;
    transport->server_send = cmsg_transport_tipc_server_send;
    transport->closure = cmsg_server_closure_rpc;
    transport->invoke = cmsg_client_invoke_rpc;
    transport->client_close = cmsg_transport_tipc_client_close;
    transport->server_close = cmsg_transport_tipc_server_close;

    transport->s_socket = cmsg_transport_tipc_server_get_socket;
    transport->c_socket = cmsg_transport_tipc_client_get_socket;

    transport->server_destroy = cmsg_transport_tipc_server_destroy;

    DEBUG (CMSG_INFO, "%s: done\n", __FUNCTION__);
}

void
cmsg_transport_oneway_tipc_init (cmsg_transport *transport)
{
    if (transport == NULL)
        return;

    transport->config.socket.family = PF_TIPC;
    transport->config.socket.sockaddr.generic.sa_family = PF_TIPC;

    transport->connect = cmsg_transport_tipc_connect;
    transport->listen = cmsg_transport_tipc_listen;
    transport->server_recv = cmsg_transport_tipc_server_recv;
    transport->client_recv = cmsg_transport_tipc_client_recv;
    transport->client_send = cmsg_transport_tipc_client_send;
    transport->server_send = cmsg_transport_tipc_server_send;
    transport->closure = cmsg_server_closure_oneway;
    transport->invoke = cmsg_client_invoke_oneway;
    transport->client_close = cmsg_transport_tipc_client_close;
    transport->server_close = cmsg_transport_tipc_server_close;

    transport->s_socket = cmsg_transport_tipc_server_get_socket;
    transport->c_socket = cmsg_transport_tipc_client_get_socket;

    transport->server_destroy = cmsg_transport_tipc_server_destroy;

    DEBUG (CMSG_INFO, "%s: done\n", __FUNCTION__);
}


