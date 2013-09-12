#include "protobuf-c-cmsg-server.h"
#include "protobuf-c-cmsg-queue.h"

/**
 * Forward Function Declarations
 */
void cmsg_server_queue_filter_set_all (cmsg_server *server,
                                       cmsg_queue_filter_type filter_type);

void cmsg_server_queue_filter_init (cmsg_server *server);

cmsg_queue_filter_type cmsg_server_queue_filter_lookup (cmsg_server *server,
                                                        const char *method);

/**
 * Structure definitions
 */


/**
 * Functions
 */
cmsg_server *
cmsg_server_new (cmsg_transport *transport, ProtobufCService *service)
{
    int32_t yes = 1;    // for setsockopt() SO_REUSEADDR, below
    int32_t listening_socket = -1;
    int32_t ret = 0;
    socklen_t addrlen = sizeof (cmsg_socket_address);
    cmsg_server *server = NULL;

    CMSG_ASSERT (transport);
    CMSG_ASSERT (service);

    server = malloc (sizeof (cmsg_server));
    if (server)
    {
        server->_transport = transport;
        server->service = service;
        server->allocator = &protobuf_c_default_allocator;  //initialize alloc and free for message_unpack() and message_free()
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
            return NULL;
        }

        server->queue_enabled_from_parent = 0;

        if (pthread_mutex_init (&server->queue_mutex, NULL) != 0)
        {
            DEBUG (CMSG_ERROR, "[SERVER] error: queue mutex init failed\n");
            free (server);
            return NULL;
        }

        server->accepted_fdmax = 0;
        FD_ZERO (&server->accepted_fdset);
        server->maxQueueLength = 0;
        server->queue = g_queue_new ();
        server->queue_filter_hash_table = g_hash_table_new (cmsg_queue_filter_hash_function,
                                                            cmsg_queue_filter_hash_equal_function);

        if (pthread_cond_init (&server->queue_process_cond, NULL) != 0)
        {
            DEBUG (CMSG_ERROR, "[SERVER] error: queue_process_cond init failed\n");
            return 0;
        }

        if (pthread_mutex_init (&server->queue_process_mutex, NULL) != 0)
        {
            DEBUG (CMSG_ERROR, "[SERVER] error: queue_process_mutex init failed\n");
            return 0;
        }

        server->self_thread_id = pthread_self ();

        cmsg_server_queue_filter_init (server);
    }
    else
    {
        syslog (LOG_CRIT | LOG_LOCAL6,
                "[SERVER] error: unable to create server. line(%d)\n", __LINE__);
    }

    return server;
}


void
cmsg_server_destroy (cmsg_server *server)
{
    int fd;

    CMSG_ASSERT (server);
    CMSG_ASSERT (server->_transport);

    // Close accepted sockets before destroying server
    for (fd = 0; fd <= server->accepted_fdmax; fd++)
    {
        if (FD_ISSET (fd, &server->accepted_fdset))
        {
            close (fd);
        }
    }

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
cmsg_server_receive_poll (cmsg_server *server, int32_t timeout_ms, fd_set *master_fdset,
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
 * Perform server receive on a list of cmsg servers.
 * Timeout : (0: return immediately, +: wait in milli-seconds, -: no timeout).
 * On success returns 0, failure returns -1.
 */
int32_t
cmsg_server_receive_poll_list (GList *server_list, int32_t timeout_ms)
{
    struct timeval timeout = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    cmsg_server *server;
    GList *node;
    fd_set read_fds;
    int fdmax;
    int ret;
    int listen_socket;
    int fd;
    int newfd;

    FD_ZERO (&read_fds);

    // Collect fds to examine
    fdmax = 0;
    for (node = g_list_first (server_list); node && node->data; node = g_list_next (node))
    {
        server = node->data;

        listen_socket = cmsg_server_get_socket (server);
        FD_SET (listen_socket, &read_fds);
        fdmax = MAX (fdmax, listen_socket);

        for (fd = 0; fd <= server->accepted_fdmax; fd++)
        {
            if (FD_ISSET (fd, &server->accepted_fdset))
            {
                FD_SET (fd, &read_fds);
            }
        }
        fdmax = MAX (fdmax, server->accepted_fdmax);
    }

    // Check any data is available
    ret = select (fdmax + 1, &read_fds, NULL, NULL, (timeout_ms < 0) ? NULL : &timeout);
    if (ret == -1)
    {
        DEBUG (CMSG_ERROR, "[SERVER] select error occurred: %s ", strerror (errno));
        return CMSG_RET_ERR;
    }
    else if (ret == 0)
    {
        // timed out, so func success but nothing received, early return
        return CMSG_RET_OK;
    }

    // Process any data available on the sockets
    for (node = g_list_first (server_list); node && node->data; node = g_list_next (node))
    {
        server = node->data;
        listen_socket = cmsg_server_get_socket (server);

        for (fd = 0; fd <= fdmax; fd++)
        {
            if (FD_ISSET (fd, &read_fds))
            {
                if (fd == listen_socket)
                {
                    // new connection from a client
                    newfd = cmsg_server_accept (server, fd);
                    if (newfd > 0)
                    {
                        FD_SET (newfd, &server->accepted_fdset);
                        server->accepted_fdmax = MAX (server->accepted_fdmax, newfd);
                    }
                }
                else if (FD_ISSET (fd, &server->accepted_fdset))
                {
                    // there is something happening on the socket so receive it.
                    cmsg_server_receive (server, fd);
                    server->_transport->server_close (server);
                    FD_CLR (fd, &server->accepted_fdset);
                    if (server->accepted_fdmax == fd)
                    {
                        server->accepted_fdmax--;
                    }
                }
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
cmsg_server_receive (cmsg_server *server, int32_t socket)
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


/**
 * Assumes that server_request will have been set in the server by the caller.
 */
void
cmsg_server_invoke (cmsg_server *server, uint32_t method_index, ProtobufCMessage *message,
                    cmsg_method_processing_reason process_reason)
{
    unsigned int queue_length = 0;
    cmsg_closure_data closure_data;

    // Setup closure_data so it can be used no matter what the action is
    closure_data.server = server;
    closure_data.method_processing_reason = process_reason;

    switch (process_reason)
    {
    case CMSG_METHOD_OK_TO_INVOKE:
    case CMSG_METHOD_INVOKING_FROM_QUEUE:
        server->service->invoke (server->service,
                                 method_index,
                                 message,
                                 server->_transport->closure, (void *) &closure_data);

        protobuf_c_message_free_unpacked (message, server->allocator);

        // Closure is called by the invoke.
        break;

    case CMSG_METHOD_QUEUED:
        // Add to queue
        pthread_mutex_lock (&server->queue_mutex);

        //todo: check return
        cmsg_receive_queue_push (server->queue, message, method_index);

        pthread_mutex_unlock (&server->queue_mutex);

        //send signal
        pthread_mutex_lock (&server->queue_process_mutex);
        pthread_cond_signal (&server->queue_process_cond);
        pthread_mutex_unlock (&server->queue_process_mutex);

        queue_length = g_queue_get_length (server->queue);
        DEBUG (CMSG_ERROR, "[SERVER] queue length: %d\n", queue_length);
        if (queue_length > server->maxQueueLength)
        {
            server->maxQueueLength = queue_length;
        }

        // Send response, if required by the closure function
        server->_transport->closure (message, (void *)&closure_data);
        break;

    case CMSG_METHOD_DROPPED:
        // Send response, if required by the closure function
        server->_transport->closure (message, (void *)&closure_data);

        // Free the unpacked message
        protobuf_c_message_free_unpacked (message, server->allocator);
        break;

    default:
        // Don't want to do anything in this case.
        break;
    }
}


/**
 * The buffer has been received and now needs to be processed by protobuf-c.
 * Once unpacked the method will be invoked.
 * If the
 */
int32_t
cmsg_server_message_processor (cmsg_server *server, uint8_t *buffer_data)
{
    cmsg_queue_filter_type action;
    cmsg_method_processing_reason processing_reason;

    CMSG_ASSERT (server);
    CMSG_ASSERT (server->_transport);
    CMSG_ASSERT (server->service);
    CMSG_ASSERT (server->service->descriptor);
    CMSG_ASSERT (server->server_request);

    cmsg_server_request *server_request = server->server_request;
    ProtobufCMessage *message = NULL;
    ProtobufCAllocator *allocator = (ProtobufCAllocator *) server->allocator;

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
        // ATL_1716_TODO need to allocate message before init'ing it
        protobuf_c_message_init (server->service->descriptor->methods[server_request->method_index].input, message);
    }

    if (message == NULL)
    {
        DEBUG (CMSG_ERROR, "[SERVER] error: unpacking message\n");
        return CMSG_RET_ERR;
    }

    if (server->queue_enabled_from_parent)
    {
        // queuing has been enable from parent subscriber
        // so don't do server queue filter lookup
        processing_reason = CMSG_METHOD_QUEUED;
    }
    else
    {
        action = cmsg_server_queue_filter_lookup (server,
                                                  server->service->descriptor->methods[server_request->method_index].name);

        if (action == CMSG_QUEUE_FILTER_ERROR)
        {
            DEBUG (CMSG_ERROR,
                   "[CLIENT] error: queue_lookup_filter returned CMSG_QUEUE_FILTER_ERROR for: %s\n",
                   service->descriptor->methods[server_request->method_index].name);

            // Free unpacked message prior to return
            protobuf_c_message_free_unpacked (message, allocator);
            return CMSG_RET_ERR;
        }
        else if (action == CMSG_QUEUE_FILTER_DROP)
        {
            DEBUG (CMSG_INFO,
                   "[CLIENT] dropping message: %s\n",
                   service->descriptor->methods[server_request->method_index].name);

            processing_reason = CMSG_METHOD_DROPPED;
        }
        else if (action == CMSG_QUEUE_FILTER_QUEUE)
        {
            processing_reason = CMSG_METHOD_QUEUED;
        }
        else if (action == CMSG_QUEUE_FILTER_PROCESS)
        {
            processing_reason = CMSG_METHOD_OK_TO_INVOKE;
        }
    }

    cmsg_server_invoke (server, server_request->method_index, message, processing_reason);

    DEBUG (CMSG_INFO, "[SERVER] end of message processor\n");

    return CMSG_RET_OK;
}


void
_cmsg_server_empty_reply_send (cmsg_server *server, cmsg_status_code status_code,
                               uint32_t method_index, uint32_t request_id)
{
    int ret = 0;
    uint32_t header[4];

    header[0] = cmsg_common_uint32_to_le (status_code);
    header[1] = cmsg_common_uint32_to_le (method_index);
    header[2] = 0;            /* no message */
    header[3] = request_id;

    DEBUG (CMSG_INFO, "[SERVER] response header\n");

    cmsg_buffer_print ((void *)&header, sizeof (header));

    ret = server->_transport->server_send (server, &header, sizeof (header), 0);
    if (ret < sizeof (header))
    {
        DEBUG (CMSG_ERROR,
               "[SERVER] error: sending of response failed send:%d of %d\n",
               ret, sizeof (header));
        return;
    }
    return;
}


/**
 * Assumes that server will have had server_request set prior to being called.
 */
void
cmsg_server_closure_rpc (const ProtobufCMessage *message, void *closure_data_void)
{

    cmsg_closure_data *closure_data = (cmsg_closure_data *) closure_data_void;
    cmsg_server *server = closure_data->server;

    CMSG_ASSERT (server);
    CMSG_ASSERT (server->_transport);
    CMSG_ASSERT (server->server_request);

    cmsg_server_request *server_request = server->server_request;
    int ret = 0;

    DEBUG (CMSG_INFO, "[SERVER] invoking rpc method=%d\n", server_request->method_index);

    /* When invoking from a queue we do not want to send a reply as it will
     * have already been done (as per below).
     */
    if (closure_data->method_processing_reason == CMSG_METHOD_INVOKING_FROM_QUEUE)
    {
        return;
    }
    /* If the method has been queued then send a response with no data
     * This allows the other end to unblock.
     */
    else if (closure_data->method_processing_reason == CMSG_METHOD_QUEUED)
    {
        DEBUG (CMSG_INFO, "[SERVER] method %d queued, sending response without data\n",
               server_request->method_index);

        _cmsg_server_empty_reply_send (server, CMSG_STATUS_CODE_SERVICE_QUEUED,
                                       server_request->method_index,
                                       server_request->request_id);
        return;
    }
    /* If the method has been dropped due a filter then send a response with no data.
     * This allows the other end to unblock.
     */
    else if (closure_data->method_processing_reason == CMSG_METHOD_DROPPED)
    {
        DEBUG (CMSG_INFO, "[SERVER] method %d dropped, sending response without data\n",
               server_request->method_index);

        _cmsg_server_empty_reply_send (server, CMSG_STATUS_CODE_SERVICE_DROPPED,
                                       server_request->method_index,
                                       server_request->request_id);
        return;
    }
    /* No response message was specified, therefore reply with an error
     */
    else if (!message)
    {
        DEBUG (CMSG_INFO, "[SERVER] sending response without data\n");

        _cmsg_server_empty_reply_send (server, CMSG_STATUS_CODE_SERVICE_FAILED,
                                       server_request->method_index,
                                       server_request->request_id);
        return;
    }
    /* Method has executed normally and has a response to be sent.
     */
    else
    {
        DEBUG (CMSG_INFO, "[SERVER] sending response with data\n");

        uint32_t packed_size = protobuf_c_message_get_packed_size (message);
        uint32_t header[4];
        header[0] = cmsg_common_uint32_to_le (CMSG_STATUS_CODE_SUCCESS);
        header[1] = cmsg_common_uint32_to_le (server_request->method_index);
        header[2] = cmsg_common_uint32_to_le (packed_size); //packesize
        header[3] = server_request->request_id;

        uint8_t *buffer = malloc (packed_size + sizeof (header));
        if (!buffer)
        {
            syslog (LOG_CRIT | LOG_LOCAL6,
                    "[SERVER] error: unable to allocate buffer. line(%d)\n", __LINE__);
            return;
        }
        uint8_t *buffer_data = malloc (packed_size);
        if (!buffer_data)
        {
            syslog (LOG_CRIT | LOG_LOCAL6,
                    "[SERVER] error: unable to allocate data buffer. line(%d)\n", __LINE__);
            free (buffer);
            return;
        }

        memcpy ((void *) buffer, &header, sizeof (header));

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

        memcpy ((void *) buffer + sizeof (header), (void *) buffer_data, packed_size);

        DEBUG (CMSG_INFO, "[SERVER] response header\n");
        cmsg_buffer_print ((void *) &header, sizeof (header));

        DEBUG (CMSG_INFO, "[SERVER] response data\n");
        cmsg_buffer_print ((void *) buffer + sizeof (header), packed_size);

        ret = server->_transport->server_send (server,
                                               buffer, packed_size + sizeof (header), 0);
        if (ret < packed_size + sizeof (header))
            DEBUG (CMSG_ERROR,
                   "[SERVER] sending if response failed send:%d of %ld\n",
                   ret, packed_size + sizeof (header));

        free (buffer);
        free (buffer_data);
    }

    return;
}


/**
 * Assumes that server will have had server_request set prior to being called.
 *
 * Ignores whether the messages was queued or not.
 */
void
cmsg_server_closure_oneway (const ProtobufCMessage *message, void *closure_data_void)
{
    // Nothing to do as oneway doesn't reply with anything.
    return;
}


void
cmsg_server_drop_all (cmsg_server *server)
{
    cmsg_server_queue_filter_set_all (server, CMSG_QUEUE_FILTER_DROP);
}

void
cmsg_server_queue_enable (cmsg_server *server)
{
    cmsg_server_queue_filter_set_all (server, CMSG_QUEUE_FILTER_QUEUE);
}

int32_t
cmsg_server_queue_disable (cmsg_server *server)
{
    cmsg_server_queue_filter_set_all (server, CMSG_QUEUE_FILTER_PROCESS);

    return cmsg_server_queue_process_all (server);
}

unsigned int
cmsg_server_queue_get_length (cmsg_server *server)
{
    return cmsg_queue_get_length (server->queue);
}


int32_t
cmsg_server_queue_process_one (cmsg_server *server)
{
    return cmsg_receive_queue_process_one (server->queue, server->queue_mutex,
                                           server->service->descriptor, server);
}


/**
 * Processes the upto the given number of items to process out of the queue
 */
int32_t
cmsg_server_queue_process_some (cmsg_server *server, uint32_t num_to_process)
{
    return cmsg_receive_queue_process_some (server->queue, server->queue_mutex,
                                            server->service->descriptor, server,
                                            num_to_process);
}


/**
 * Processes all the items in the queue.
 *
 * @returns the number of items processed off the queue
 */
int32_t
cmsg_server_queue_process_all (cmsg_server *server)
{
    uint32_t total_processed = 0;
    int32_t processed = -1;

    if (g_queue_get_length (server->queue) == 0)
    {
        return;
    }

    //if the we run do api calls and processing in different threads wait
    //for a signal from the api thread to start processing
    if (!(server->self_thread_id == pthread_self ()))
    {
        syslog (LOG_CRIT,
                "Processing in a different thread to that which created the server, await mutex");
        pthread_mutex_lock (&server->queue_process_mutex);
        syslog (LOG_CRIT, "Locked queue process mutex, now waiting for condition");
        pthread_cond_wait (&server->queue_process_cond, &server->queue_process_mutex);
        syslog (LOG_CRIT, "Condition hit!");
        pthread_mutex_unlock (&server->queue_process_mutex);

        while (processed < 0)
        {
            processed = cmsg_receive_queue_process_all (server->queue, server->queue_mutex,
                                                        server->service->descriptor,
                                                        server);
            total_processed += processed;
        }
        syslog (LOG_CRIT, "Finished processing all msgs");
    }
    else
    {
        syslog (LOG_CRIT, "processing all msgs in a different thread");
        total_processed = cmsg_receive_queue_process_all (server->queue,
                                                          server->queue_mutex,
                                                          server->service->descriptor,
                                                          server);
        syslog (LOG_CRIT, "Finished processing all msgs");
    }

    return total_processed;
}

void
cmsg_server_queue_filter_set_all (cmsg_server *server, cmsg_queue_filter_type filter_type)
{
    cmsg_queue_filter_set_all (server->queue_filter_hash_table, server->service->descriptor,
                               filter_type);
}

void
cmsg_server_queue_filter_clear_all (cmsg_server *server)
{
    cmsg_queue_filter_clear_all (server->queue_filter_hash_table,
                                 server->service->descriptor);
}

int32_t
cmsg_server_queue_filter_set (cmsg_server *server, const char *method,
                              cmsg_queue_filter_type filter_type)
{
    return cmsg_queue_filter_set (server->queue_filter_hash_table, method, filter_type);
}

int32_t
cmsg_server_queue_filter_clear (cmsg_server *server, const char *method)
{
    return cmsg_queue_filter_clear (server->queue_filter_hash_table, method);
}

void
cmsg_server_queue_filter_init (cmsg_server *server)
{
    cmsg_queue_filter_init (server->queue_filter_hash_table, server->service->descriptor);
}

cmsg_queue_filter_type
cmsg_server_queue_filter_lookup (cmsg_server *server, const char *method)
{
    return cmsg_queue_filter_lookup (server->queue_filter_hash_table, method);
}

void
cmsg_server_queue_filter_show (cmsg_server *server)
{
    cmsg_queue_filter_show (server->queue_filter_hash_table, server->service->descriptor);
}

uint32_t
cmsg_server_queue_max_length_get (cmsg_server *server)
{
    return server->maxQueueLength;
}

uint32_t
cmsg_server_queue_current_length_get (cmsg_server *server)
{
    return g_queue_get_length (server->queue);
}
