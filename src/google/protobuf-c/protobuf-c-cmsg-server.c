#include "protobuf-c-cmsg-server.h"


cmsg_server *
cmsg_server_new (cmsg_transport   *transport,
                 ProtobufCService *service)
{
    int32_t yes = 1; // for setsockopt() SO_REUSEADDR, below
    int32_t listening_socket = -1;
    int32_t ret = 0;
    socklen_t addrlen  = sizeof (cmsg_socket_address);
    cmsg_server *server = 0;

    CMSG_ASSERT (transport);
    CMSG_ASSERT (service);

    server = malloc (sizeof (cmsg_server));
    if (server)
    {
        server->_transport = transport;
        server->service = service;
        server->allocator = &protobuf_c_default_allocator; //initialize alloc and free for message_unpack() and message_free()
        server->message_processor = cmsg_server_message_processor;

        server->self.object_type = CMSG_OBJ_TYPE_SERVER;
        server->self.object = server;
        server->parent.object_type = CMSG_OBJ_TYPE_NONE;
        server->parent.object = NULL;

        DEBUG (CMSG_INFO, "[SERVER] creating new server with type: %d\n", transport->type);

        ret = transport->listen (server);

        if (ret < 0)
        {
            free (server);
            server = NULL;
        }
    }
    else
    {
        syslog (LOG_CRIT | LOG_LOCAL6, "[SERVER] error: unable to create server. line(%d)\n", __LINE__);
    }

    return server;
}


void
cmsg_server_destroy (cmsg_server *server)
{
    CMSG_ASSERT (server);
    CMSG_ASSERT (server->_transport);

    server->_transport->server_destroy (server);

    free (server);
}


int
cmsg_server_get_socket (cmsg_server *server)
{
    int socket = 0;

    CMSG_ASSERT (server);
    CMSG_ASSERT (server->_transport);

    socket = server->_transport->s_socket (server);

    DEBUG (CMSG_INFO, "[SERVER] done. socket: %d\n", socket);

    return socket;
}


/**
 * cmsg_server_receive_poll
 *
 * Wait for any data on a list of sockets or until timeout expires.
 * Timeout is specified in 'timeout_ms' (0: return immediately,
 * negative number: no timeout).
 * On success returns 0, failure returns -1.
 */
int32_t
cmsg_server_receive_poll (cmsg_server *server,
                          int32_t timeout_ms,
                          fd_set *master_fdset,
                          int *fdmax)
{
    int ret = 0;
    struct pollfd poll_list[1];
    int sock;
    fd_set read_fds = *master_fdset;
    int nfds = *fdmax;
    struct timeval timeout = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    int fd;
    int newfd;
    int check_fdmax = FALSE;
    int listen_socket;

    CMSG_ASSERT (server);

    listen_socket = cmsg_server_get_socket (server);

    ret = select (nfds + 1, &read_fds, NULL, NULL, (timeout_ms < 0) ? NULL : &timeout);
    if (ret == -1)
    {
        DEBUG (CMSG_ERROR, "[SERVER] poll error occurred: %s ", strerror (errno));
        return CMSG_RET_ERR;
    }
    else if (ret == 0)
    {
        // timed out, so func success but nothing received, early return
        return CMSG_RET_OK;
    }

    // run through the existing connections looking for data to read
    for (fd = 0; fd <= nfds; fd++)
    {
        if (FD_ISSET (fd, &read_fds))
        {
            if (fd == listen_socket)
            {
                newfd = cmsg_server_accept (server, fd);
                if (newfd >= 0)
                {
                    FD_SET (newfd, master_fdset);
                    *fdmax = MAX (newfd, *fdmax);
                }
            }
            else
            {
                // there is something happening on the socket so receive it.
                cmsg_server_receive (server, fd);
                server->_transport->server_close (server);
                FD_CLR (fd, master_fdset);
                check_fdmax = TRUE;
            }
        }
    }

    // Check the largest file descriptor
    if (check_fdmax)
    {
        for (fd = *fdmax; fd >= 0; fd--)
        {
            if (FD_ISSET (fd, master_fdset))
            {
                *fdmax = fd;
                break;
            }
        }
    }

    return CMSG_RET_OK;
}


/**
 * cmsg_server_receive
 *
 * Calls the transport receive function.
 * The expectation of the transport receive function is that it will return
 * <0 on failure & 0=< on success.
 *
 * On success returns 0, failure returns -1.
 */
int32_t
cmsg_server_receive (cmsg_server *server,
                     int32_t      socket)
{
    int32_t ret = 0;

    CMSG_ASSERT (server);
    CMSG_ASSERT (server->_transport);
    CMSG_ASSERT (socket > 0);

    ret = server->_transport->server_recv (socket, server);

    if (ret < 0)
    {
        DEBUG (CMSG_ERROR, "[SERVER] server receive failed\n");
        return CMSG_RET_ERR;
    }

    return CMSG_RET_OK;
}


/* Accept an incoming socket from a client */
int32_t
cmsg_server_accept (cmsg_server *server, int32_t listen_socket)
{
    int sock = 0;

    if (server->_transport->server_accept != NULL)
    {
        sock = server->_transport->server_accept (listen_socket, server);
    }

    return sock;
}


int32_t
cmsg_server_message_processor (cmsg_server *server,
                               uint8_t     *buffer_data)
{
    CMSG_ASSERT (server);
    CMSG_ASSERT (server->_transport);
    CMSG_ASSERT (server->service);
    CMSG_ASSERT (server->service->descriptor);
    CMSG_ASSERT (server->server_request);

    cmsg_server_request *server_request = server->server_request;
    ProtobufCMessage *message = 0;
    ProtobufCAllocator *allocator = (ProtobufCAllocator *)server->allocator;

    if (server_request->method_index >= server->service->descriptor->n_methods)
    {
        DEBUG (CMSG_ERROR,
               "[SERVER] error: the method index from read from the header seems to be to high\n");
        return CMSG_RET_ERR;
    }

    if (buffer_data)
    {
        DEBUG (CMSG_INFO, "[SERVER] processsing message with data\n");
        DEBUG (CMSG_INFO, "[SERVER] unpacking message\n");

        //unpack the message
        message = protobuf_c_message_unpack (server->service->descriptor->methods[server_request->method_index].input,
                                             allocator,
                                             server_request->message_length,
                                             buffer_data);
    }
    else
    {
        DEBUG (CMSG_INFO, "[SERVER] processsing message without data\n");
        //create a new empty message
        protobuf_c_message_init (server->service->descriptor->methods[server_request->method_index].input, message);
    }

    if (message == 0)
    {
        DEBUG (CMSG_ERROR, "[SERVER] error: unpacking message\n");
        return CMSG_RET_ERR;
    }

    server->service->invoke (server->service,
                             server_request->method_index,
                             message,
                             server->_transport->closure,
                             (void *)server);

    //todo: we need to handle errors from closure data


    protobuf_c_message_free_unpacked (message, allocator);

    DEBUG (CMSG_INFO, "[SERVER] end of message processor\n");

    return CMSG_RET_OK;
}


void
cmsg_server_closure_rpc (const ProtobufCMessage *message,
                         void                   *closure_data)
{

    cmsg_server *server = (cmsg_server *)closure_data;

    CMSG_ASSERT (server);
    CMSG_ASSERT (server->_transport);
    CMSG_ASSERT (server->server_request);

    cmsg_server_request *server_request = server->server_request;
    int ret = 0;

    DEBUG (CMSG_INFO, "[SERVER] invoking rpc method=%d\n", server_request->method_index);
    if (!message)
    {
        DEBUG (CMSG_INFO, "[SERVER] sending response without data\n");

        uint32_t header[4];
        header[0] = cmsg_common_uint32_to_le (CMSG_STATUS_CODE_SERVICE_FAILED);
        header[1] = cmsg_common_uint32_to_le (server_request->method_index);
        header[2] = 0;            /* no message */
        header[3] = server_request->request_id;

        DEBUG (CMSG_INFO, "[SERVER] response header\n");

        cmsg_buffer_print ((void *)&header, sizeof (header));

        ret = server->_transport->server_send (server, &header, sizeof (header), 0);
        if (ret < sizeof (header))
        {
            DEBUG (CMSG_ERROR,
                   "[SERVER] error: sending if response failed send:%d of %ld\n",
                   ret, sizeof (header));
            return;
        }

    }
    else
    {
        DEBUG (CMSG_INFO, "[SERVER] sending response with data\n");

        uint32_t packed_size = protobuf_c_message_get_packed_size (message);
        uint32_t header[4];
        header[0] = cmsg_common_uint32_to_le (CMSG_STATUS_CODE_SUCCESS);
        header[1] = cmsg_common_uint32_to_le (server_request->method_index);
        header[2] = cmsg_common_uint32_to_le (packed_size);  //packesize
        header[3] = server_request->request_id;

        uint8_t *buffer = malloc (packed_size + sizeof (header));
        if (!buffer)
        {
            syslog (LOG_CRIT | LOG_LOCAL6, "[SERVER] error: unable to allocate buffer. line(%d)\n", __LINE__);
            return;
        }
        uint8_t *buffer_data = malloc (packed_size);
        if (!buffer_data)
        {
            syslog (LOG_CRIT | LOG_LOCAL6, "[SERVER] error: unable to allocate data buffer. line(%d)\n", __LINE__);
            free (buffer);
            return;
        }

        memcpy ((void *)buffer, &header, sizeof (header));

        DEBUG (CMSG_INFO, "[SERVER] packing message\n");

        ret = protobuf_c_message_pack (message, buffer_data);
        if (ret < packed_size)
        {
            DEBUG (CMSG_ERROR,
                   "[SERVER] packing response data failed packet:%d of %d\n",
                   ret, packed_size);

            free (buffer);
            free (buffer_data);
            return;
        }

        memcpy ((void *)buffer + sizeof (header), (void *)buffer_data, packed_size);

        DEBUG (CMSG_INFO, "[SERVER] response header\n");
        cmsg_buffer_print ((void *)&header, sizeof (header));

        DEBUG (CMSG_INFO, "[SERVER] response data\n");
        cmsg_buffer_print ((void *)buffer + sizeof (header), packed_size);

        ret = server->_transport->server_send (server, buffer, packed_size + sizeof (header), 0);
        if (ret < packed_size + sizeof (header))
            DEBUG (CMSG_ERROR,
                   "[SERVER] sending if response failed send:%d of %ld\n",
                   ret, packed_size + sizeof (header));

        free (buffer);
        free (buffer_data);
    }

    server_request->closure_response = 0;

    return;
}


void
cmsg_server_closure_oneway (const ProtobufCMessage *message,
                            void                   *closure_data)
{
    CMSG_ASSERT (closure_data);

    cmsg_server *server = (cmsg_server *)closure_data;
    cmsg_server_request *server_request = server->server_request;
    //we are not sending a response in this transport mode
    DEBUG (CMSG_INFO,
           "[SERVER] invoking oneway method=%d\n",
           server_request->method_index);

    server_request->closure_response = 0;
}
