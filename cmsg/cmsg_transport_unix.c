/**
 * @file cmsg_transport_unix.c
 *
 * Transport layer using UNIX sockets. This transport should be used for
 * process to process IPC
 *
 * Copyright 2016 Allied Telesis Labs, New Zealand
 *
 */

#include "cmsg_private.h"
#include "cmsg_transport.h"
#include "cmsg_client.h"
#include "cmsg_server.h"
#include "cmsg_error.h"

/*
 * Create a UNIX socket connection.
 * Returns 0 on success or a negative integer on failure.
 */
static int32_t
cmsg_transport_unix_connect (cmsg_client *client)
{
    int32_t ret;
    struct sockaddr_un *addr;
    uint32_t addrlen;

    if (client == NULL)
    {
        return 0;
    }


    client->_transport->connection.sockets.client_socket =
        socket (client->_transport->config.socket.family, SOCK_STREAM, 0);

    if (client->_transport->connection.sockets.client_socket < 0)
    {
        ret = -errno;
        CMSG_LOG_TRANSPORT_ERROR (client->_transport, "Unable to create socket. Error:%s",
                                  strerror (errno));
        return ret;
    }

    addr = (struct sockaddr_un *) &client->_transport->config.socket.sockaddr.un;
    addrlen = sizeof (client->_transport->config.socket.sockaddr.un);

    if (connect (client->_transport->connection.sockets.client_socket, addr, addrlen) < 0)
    {
        ret = -errno;
        CMSG_LOG_TRANSPORT_ERROR (client->_transport,
                                  "Failed to connect to remote host. Error:%s",
                                  strerror (errno));
        close (client->_transport->connection.sockets.client_socket);
        client->_transport->connection.sockets.client_socket = -1;

        return ret;
    }
    else
    {
        CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] succesfully connected\n");
        return 0;
    }
}


static int32_t
cmsg_transport_unix_listen (cmsg_server *server)
{
    int32_t yes = 1;    // for setsockopt() SO_REUSEADDR, below
    int32_t listening_socket = -1;
    int32_t ret = 0;
    socklen_t addrlen = 0;
    cmsg_transport *transport = NULL;

    if (server == NULL)
    {
        return 0;
    }

    server->_transport->connection.sockets.listening_socket = 0;
    server->_transport->connection.sockets.client_socket = 0;

    transport = server->_transport;
    listening_socket = socket (transport->config.socket.family, SOCK_STREAM, 0);
    if (listening_socket == -1)
    {
        CMSG_LOG_TRANSPORT_ERROR (server->_transport, "Unable to create socket. Error:%s",
                                  strerror (errno));
        return -1;
    }

    ret = setsockopt (listening_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof (int32_t));
    if (ret == -1)
    {
        CMSG_LOG_TRANSPORT_ERROR (server->_transport, "Unable to setsockopt. Error:%s",
                                  strerror (errno));
        close (listening_socket);
        return -1;
    }

    unlink (transport->config.socket.sockaddr.un.sun_path);
    addrlen = sizeof (transport->config.socket.sockaddr.un);
    ret = bind (listening_socket, &transport->config.socket.sockaddr.un, addrlen);
    if (ret < 0)
    {
        CMSG_LOG_TRANSPORT_ERROR (server->_transport, "Unable to bind socket. Error:%s",
                                  strerror (errno));
        close (listening_socket);
        return -1;
    }

    ret = listen (listening_socket, 10);
    if (ret < 0)
    {
        CMSG_LOG_TRANSPORT_ERROR (server->_transport, "Listen failed. Error:%s",
                                  strerror (errno));
        close (listening_socket);
        return -1;
    }

    server->_transport->connection.sockets.listening_socket = listening_socket;

    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] listening on unix socket: %d\n", listening_socket);

    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] listening on: %s\n",
                transport->config.socket.sockaddr.un.sun_path);

    return 0;
}


/* Wrapper function to call "recv" on a UNIX socket */
int
cmsg_transport_unix_recv (void *handle, void *buff, int len, int flags)
{
    int *sock = (int *) handle;

    return recv (*sock, buff, len, flags);
}


static int32_t
cmsg_transport_unix_server_recv (int32_t server_socket, cmsg_server *server)
{
    int32_t ret = 0;

    if (!server || server_socket < 0)
    {
        CMSG_LOG_GEN_ERROR ("UNIX server receive error. Invalid arguments.");
        return -1;
    }

    /* Remember the client socket to use when send reply */
    server->_transport->connection.sockets.client_socket = server_socket;

    ret = cmsg_transport_server_recv (cmsg_transport_unix_recv,
                                      (void *) &server_socket, server);

    return ret;
}


static int32_t
cmsg_transport_unix_server_accept (int32_t listen_socket, cmsg_server *server)
{
    uint32_t client_len;
    cmsg_transport client_transport;
    int sock;
    struct sockaddr *addr;

    if (!server || listen_socket < 0)
    {
        CMSG_LOG_GEN_ERROR ("Unix server accept error. Invalid arguments.");
        return -1;
    }

    addr = (struct sockaddr *) &client_transport.config.socket.sockaddr.un;
    client_len = sizeof (client_transport.config.socket.sockaddr.un);

    sock = accept (listen_socket, addr, &client_len);

    if (sock < 0)
    {
        CMSG_LOG_TRANSPORT_ERROR (server->_transport, "Accept failed. Error:%s",
                                  strerror (errno));
        CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] sock = %d\n", sock);

        return -1;
    }

    return sock;
}


static cmsg_status_code
cmsg_transport_unix_client_recv (cmsg_client *client, ProtobufCMessage **messagePtPt)
{
    int nbytes = 0;
    uint32_t dyn_len = 0;
    cmsg_header header_received;
    cmsg_header header_converted;
    uint8_t *recv_buffer = NULL;
    uint8_t *buffer = NULL;
    uint8_t buf_static[512];
    const ProtobufCMessageDescriptor *desc;
    uint32_t extra_header_size;
    cmsg_server_request server_request;

    *messagePtPt = NULL;

    if (!client)
    {
        return CMSG_STATUS_CODE_SERVICE_FAILED;
    }

    nbytes = recv (client->_transport->connection.sockets.client_socket,
                   &header_received, sizeof (cmsg_header), MSG_WAITALL);
    CMSG_PROF_TIME_LOG_ADD_TIME (&client->_transport->prof, "receive",
                                 cmsg_prof_time_toc (&client->_transport->prof));

    if (nbytes == (int) sizeof (cmsg_header))
    {
        if (cmsg_header_process (&header_received, &header_converted) != CMSG_RET_OK)
        {
            // Couldn't process the header for some reason
            CMSG_LOG_TRANSPORT_ERROR (client->_transport,
                                      "Unable to process message header for client receive. Bytes:%d",
                                      nbytes);
            CMSG_PROF_TIME_LOG_ADD_TIME (&client->_transport->prof, "unpack",
                                         cmsg_prof_time_toc (&client->_transport->prof));
            return CMSG_STATUS_CODE_SERVICE_FAILED;
        }

        CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] received response header\n");

        // read the message

        // Take into account that someone may have changed the size of the header
        // and we don't know about it, make sure we receive all the information.
        // Any TLV is taken into account in the header length.
        dyn_len = header_converted.message_length +
            header_converted.header_length - sizeof (cmsg_header);

        // There is no more data to read so exit.
        if (dyn_len == 0)
        {
            // May have been queued, dropped or there was no message returned
            CMSG_DEBUG (CMSG_INFO,
                        "[TRANSPORT] received response without data. server status %d\n",
                        header_converted.status_code);
            CMSG_PROF_TIME_LOG_ADD_TIME (&client->_transport->prof, "unpack",
                                         cmsg_prof_time_toc (&client->_transport->prof));
            return header_converted.status_code;
        }

        if (dyn_len > sizeof (buf_static))
        {
            recv_buffer = (uint8_t *) CMSG_CALLOC (1, dyn_len);
            if (recv_buffer == NULL)
            {
                /* Didn't allocate memory for recv buffer.  This is an error.
                 * Shut the socket down, it will reopen on the next api call.
                 * Record and return an error. */
                client->_transport->client_close (client);
                CMSG_LOG_TRANSPORT_ERROR (client->_transport,
                                          "Couldn't allocate memory for server reply (TLV + message), closed the socket");
                return CMSG_STATUS_CODE_SERVICE_FAILED;
            }
        }
        else
        {
            recv_buffer = (uint8_t *) buf_static;
            memset (recv_buffer, 0, sizeof (buf_static));
        }

        //just recv the rest of the data to clear the socket
        nbytes =
            recv (client->_transport->connection.sockets.client_socket, recv_buffer,
                  dyn_len, MSG_WAITALL);

        if (nbytes == (int) dyn_len)
        {
            extra_header_size = header_converted.header_length - sizeof (cmsg_header);
            // Set buffer to take into account a larger header than we expected
            buffer = recv_buffer;

            cmsg_tlv_header_process (buffer, &server_request, extra_header_size,
                                     client->descriptor);

            buffer = buffer + extra_header_size;
            CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] received response data\n");
            cmsg_buffer_print (buffer, dyn_len);

            /* Message is only returned if the server returned Success,
             */
            if (header_converted.status_code == CMSG_STATUS_CODE_SUCCESS)
            {
                ProtobufCMessage *message = NULL;
                ProtobufCAllocator *allocator = client->allocator;

                CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] unpacking response message\n");

                desc = client->descriptor->methods[server_request.method_index].output;
                message = protobuf_c_message_unpack (desc, allocator,
                                                     header_converted.message_length,
                                                     buffer);

                // Free the allocated buffer
                if (recv_buffer != (void *) buf_static)
                {
                    if (recv_buffer)
                    {
                        CMSG_FREE (recv_buffer);
                        recv_buffer = NULL;
                    }
                }

                // Msg not unpacked correctly
                if (message == NULL)
                {
                    CMSG_LOG_TRANSPORT_ERROR (client->_transport,
                                              "Error unpacking response message. Msg length:%d",
                                              header_converted.message_length);
                    CMSG_PROF_TIME_LOG_ADD_TIME (&client->_transport->prof, "unpack",
                                                 cmsg_prof_time_toc (&client->
                                                                     _transport->prof));
                    return CMSG_STATUS_CODE_SERVICE_FAILED;
                }
                *messagePtPt = message;
                CMSG_PROF_TIME_LOG_ADD_TIME (&client->_transport->prof, "unpack",
                                             cmsg_prof_time_toc (&client->
                                                                 _transport->prof));
            }

            // Make sure we return the status from the server
            return header_converted.status_code;
        }
        else
        {
            CMSG_LOG_TRANSPORT_ERROR (client->_transport,
                                      "No data for recv. socket:%d, dyn_len:%d, actual len:%d strerr %d:%s",
                                      client->_transport->connection.sockets.client_socket,
                                      dyn_len, nbytes, errno, strerror (errno));

        }
        if (recv_buffer != (void *) buf_static)
        {
            if (recv_buffer)
            {
                CMSG_FREE (recv_buffer);
                recv_buffer = NULL;
            }
        }
    }
    else if (nbytes > 0)
    {
        /* Didn't receive all of the CMSG header.
         */
        CMSG_LOG_TRANSPORT_ERROR (client->_transport,
                                  "Bad header length for recv. Socket:%d nbytes:%d",
                                  client->_transport->connection.sockets.client_socket,
                                  nbytes);

        // TEMP to keep things going
        recv_buffer = (uint8_t *) CMSG_CALLOC (1, nbytes);
        nbytes =
            recv (client->_transport->connection.sockets.client_socket, recv_buffer, nbytes,
                  MSG_WAITALL);
        CMSG_FREE (recv_buffer);
        recv_buffer = NULL;
    }
    else if (nbytes == 0)
    {
        //Normal socket shutdown case. Return other than TRANSPORT_OK to
        //have socket removed from select set.
    }
    else
    {
        if (errno == ECONNRESET)
        {
            CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] recv socket %d error: %s\n",
                        client->_transport->connection.sockets.client_socket,
                        strerror (errno));
            return CMSG_STATUS_CODE_SERVER_CONNRESET;
        }
        else
        {
            CMSG_LOG_TRANSPORT_ERROR (client->_transport, "Recv error. Socket:%d Error:%s",
                                      client->_transport->connection.sockets.client_socket,
                                      strerror (errno));
        }
    }

    CMSG_PROF_TIME_LOG_ADD_TIME (&client->_transport->prof, "unpack",
                                 cmsg_prof_time_toc (&client->_transport->prof));
    return CMSG_STATUS_CODE_SERVICE_FAILED;
}


static int32_t
cmsg_transport_unix_client_send (cmsg_client *client, void *buff, int length, int flag)
{
    return (send
            (client->_transport->connection.sockets.client_socket, buff, length, flag));
}

static int32_t
cmsg_transport_unix_server_send (cmsg_server *server, void *buff, int length, int flag)
{
    return (send
            (server->_transport->connection.sockets.client_socket, buff, length, flag));
}

/**
 * UNIX oneway servers do not send replies to received messages. This function therefore
 * returns 0.
 */
static int32_t
cmsg_transport_unix_oneway_server_send (cmsg_server *server, void *buff, int length,
                                        int flag)
{
    return 0;
}

static void
cmsg_transport_unix_client_close (cmsg_client *client)
{
    if (client->_transport->connection.sockets.client_socket != -1)
    {
        CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] shutting down socket\n");
        shutdown (client->_transport->connection.sockets.client_socket, SHUT_RDWR);

        CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] closing socket\n");
        close (client->_transport->connection.sockets.client_socket);

        client->_transport->connection.sockets.client_socket = -1;
    }
}

static void
cmsg_transport_unix_server_close (cmsg_transport *transport)
{
    CMSG_DEBUG (CMSG_INFO, "[SERVER] shutting down socket\n");
    shutdown (transport->connection.sockets.client_socket, SHUT_RDWR);

    CMSG_DEBUG (CMSG_INFO, "[SERVER] closing socket\n");
    close (transport->connection.sockets.client_socket);
}

static int
cmsg_transport_unix_server_get_socket (cmsg_server *server)
{
    return server->_transport->connection.sockets.listening_socket;
}


static int
cmsg_transport_unix_client_get_socket (cmsg_client *client)
{
    return client->_transport->connection.sockets.client_socket;
}

static void
cmsg_transport_unix_client_destroy (cmsg_client *cmsg_client)
{
    //placeholder to make sure destroy functions are called in the right order
}

static void
cmsg_transport_unix_server_destroy (cmsg_transport *transport)
{
    CMSG_DEBUG (CMSG_INFO, "[SERVER] Shutting down listening socket\n");
    shutdown (transport->connection.sockets.listening_socket, SHUT_RDWR);

    CMSG_DEBUG (CMSG_INFO, "[SERVER] Closing listening socket\n");
    close (transport->connection.sockets.listening_socket);
}


/**
 * UNIX is never congested
 */
uint32_t
cmsg_transport_unix_is_congested (cmsg_transport *transport)
{
    return FALSE;
}

int32_t
cmsg_transport_unix_send_called_multi_threads_enable (cmsg_transport *transport,
                                                      uint32_t enable)
{
    // Don't support sending from multiple threads
    return -1;
}


int32_t
cmsg_transport_unix_send_can_block_enable (cmsg_transport *transport,
                                           uint32_t send_can_block)
{
    transport->send_can_block = send_can_block;
    return 0;
}


static void
_cmsg_transport_unix_init_common (cmsg_transport *transport)
{
    transport->config.socket.family = PF_UNIX;
    transport->config.socket.sockaddr.generic.sa_family = PF_UNIX;
    transport->connect = cmsg_transport_unix_connect;
    transport->listen = cmsg_transport_unix_listen;
    transport->server_accept = cmsg_transport_unix_server_accept;
    transport->server_recv = cmsg_transport_unix_server_recv;
    transport->client_recv = cmsg_transport_unix_client_recv;
    transport->client_send = cmsg_transport_unix_client_send;
    transport->invoke_send = cmsg_client_invoke_send;
    transport->client_close = cmsg_transport_unix_client_close;
    transport->server_close = cmsg_transport_unix_server_close;
    transport->client_destroy = cmsg_transport_unix_client_destroy;
    transport->server_destroy = cmsg_transport_unix_server_destroy;
    transport->s_socket = cmsg_transport_unix_server_get_socket;
    transport->c_socket = cmsg_transport_unix_client_get_socket;
    transport->is_congested = cmsg_transport_unix_is_congested;
    transport->send_called_multi_threads_enable =
        cmsg_transport_unix_send_called_multi_threads_enable;
    transport->send_called_multi_enabled = FALSE;
    transport->send_can_block_enable = cmsg_transport_unix_send_can_block_enable;
    transport->ipfree_bind_enable = NULL;
}

void
cmsg_transport_rpc_unix_init (cmsg_transport *transport)
{
    if (transport == NULL)
    {
        return;
    }

    _cmsg_transport_unix_init_common (transport);

    transport->server_send = cmsg_transport_unix_server_send;
    transport->closure = cmsg_server_closure_rpc;
    transport->invoke_recv = cmsg_client_invoke_recv;

    CMSG_DEBUG (CMSG_INFO, "%s: done\n", __FUNCTION__);
}


void
cmsg_transport_oneway_unix_init (cmsg_transport *transport)
{
    if (transport == NULL)
    {
        return;
    }

    _cmsg_transport_unix_init_common (transport);

    transport->server_send = cmsg_transport_unix_oneway_server_send;
    transport->closure = cmsg_server_closure_rpc;
    transport->invoke_recv = NULL;

    CMSG_DEBUG (CMSG_INFO, "%s: done\n", __FUNCTION__);
}

cmsg_transport *
cmsg_create_transport_unix (const ProtobufCServiceDescriptor *descriptor,
                            cmsg_transport_type transport_type)
{
    cmsg_transport *transport = NULL;
    char *sun_path;

    transport = cmsg_transport_new (transport_type);
    if (transport == NULL)
    {
        return NULL;
    }

    sun_path = cmsg_transport_unix_sun_path (descriptor);

    transport->config.socket.family = AF_UNIX;
    transport->config.socket.sockaddr.un.sun_family = AF_UNIX;
    strncpy (transport->config.socket.sockaddr.un.sun_path, sun_path,
             sizeof (transport->config.socket.sockaddr.un.sun_path) - 1);

    cmsg_transport_unix_sun_path_free (sun_path);

    return transport;
}

/**
 * Get the CMSG unix transport socket name from the CMSG service descriptor.
 *
 * @param descriptor - CMSG service descriptor to get the unix transport socket name from
 *
 * @return - String representing the unix transport socket name. The memory for this
 *           string must be freed by the caller.
 */
char *
cmsg_transport_unix_sun_path (const ProtobufCServiceDescriptor *descriptor)
{
    char *copy_str = NULL;
    char *iter;

    CMSG_ASPRINTF (&copy_str, "/tmp/%s", descriptor->name);

    /* Replace the '.' in the name with '_' */
    iter = copy_str;
    while (*iter)
    {
        if (*iter == '.')
        {
            *iter = '_';
        }
        iter++;
    }

    return copy_str;
}

void
cmsg_transport_unix_sun_path_free (char *sun_path)
{
    CMSG_FREE (sun_path);
}
