/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#include "cmsg_private.h"
#include "cmsg_server.h"
#include "cmsg_error.h"
#include "cmsg_transport.h"

#ifdef HAVE_COUNTERD
#include "cntrd_app_defines.h"
#include "cntrd_app_api.h"
#endif

extern GHashTable *cpg_group_name_to_server_hash_table_h;

static int32_t _cmsg_server_method_req_message_processor (cmsg_server *server,
                                                          uint8_t *buffer_data);

static int32_t _cmsg_server_echo_req_message_processor (cmsg_server *server,
                                                        uint8_t *buffer_data);

static cmsg_server *_cmsg_create_server_tipc (const char *server_name, int member_id,
                                              int scope, ProtobufCService *descriptor,
                                              cmsg_transport_type transport_type);

int32_t cmsg_server_counter_create (cmsg_server *server, char *app_name);


/*
 * This is an internal function which can be called from CMSG library.
 * Applications should use cmsg_server_new() instead.
 *
 * Create a new CMSG server (but without creating counters).
 */
cmsg_server *
cmsg_server_create (cmsg_transport *transport, ProtobufCService *service)
{
    int32_t ret = 0;
    cmsg_server *server = NULL;

    CMSG_ASSERT_RETURN_VAL (service != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (transport != NULL, NULL);

    server = (cmsg_server *) CMSG_CALLOC (1, sizeof (cmsg_server));
    if (server)
    {
        /* Generate transport unique id */
        cmsg_transport_write_id (transport, service->descriptor->name);

        server->_transport = transport;
        server->service = service;
        server->message_processor = cmsg_server_message_processor;

        server->self.object_type = CMSG_OBJ_TYPE_SERVER;
        server->self.object = server;
        strncpy (server->self.obj_id, service->descriptor->name, CMSG_MAX_OBJ_ID_LEN);
        server->parent.object_type = CMSG_OBJ_TYPE_NONE;
        server->parent.object = NULL;

        CMSG_DEBUG (CMSG_INFO, "[SERVER] creating new server with type: %d\n",
                    transport->type);

#ifdef HAVE_VCSTACK
        if (server->_transport->type == CMSG_TRANSPORT_CPG)
        {
            /* Add entry into the hash table for the server to be found by cpg group name. */
            g_hash_table_insert (cpg_group_name_to_server_hash_table_h,
                                 (gpointer) server->_transport->config.cpg.group_name.value,
                                 (gpointer) server);
        }
#endif /* HAVE_VCSTACK */

        ret = transport->listen (server->_transport);

        if (ret < 0)
        {
            CMSG_FREE (server);
            server = NULL;
            return NULL;
        }

        server->queue_enabled_from_parent = 0;

        if (pthread_mutex_init (&server->queue_mutex, NULL) != 0)
        {
            CMSG_LOG_SERVER_ERROR (server, "Init failed for queue_mutex.");
            CMSG_FREE (server);
            return NULL;
        }

        server->accepted_fdmax = 0;
        FD_ZERO (&server->accepted_fdset);
        server->maxQueueLength = 0;
        server->queue = g_queue_new ();
        server->queue_filter_hash_table = g_hash_table_new (g_str_hash, g_str_equal);

        if (pthread_mutex_init (&server->queueing_state_mutex, NULL) != 0)
        {
            CMSG_LOG_SERVER_ERROR (server, "Init failed for queueing_state_mutex.");
            return NULL;
        }

        if (pthread_mutex_init (&server->queue_filter_mutex, NULL) != 0)
        {
            CMSG_LOG_SERVER_ERROR (server, "Init failed for queue_filter_mutex.");
            return NULL;
        }

        server->self_thread_id = pthread_self ();

        cmsg_server_queue_filter_init (server);


        pthread_mutex_lock (&server->queueing_state_mutex);
        server->queueing_state = CMSG_QUEUE_STATE_DISABLED;
        server->queueing_state_last = CMSG_QUEUE_STATE_DISABLED;

        server->queue_process_number = 0;

        server->queue_in_process = 0;

        pthread_mutex_unlock (&server->queueing_state_mutex);

        server->app_owns_current_msg = FALSE;
        server->app_owns_all_msgs = FALSE;
    }
    else
    {
        CMSG_LOG_GEN_ERROR ("[%s%s] Unable to create server.", service->descriptor->name,
                            transport->tport_id);
    }

    return server;
}


/*
 * Create a new CMSG server.
 */
cmsg_server *
cmsg_server_new (cmsg_transport *transport, ProtobufCService *service)
{
    cmsg_server *server;
    server = cmsg_server_create (transport, service);

#ifdef HAVE_COUNTERD
    char app_name[CNTRD_MAX_APP_NAME_LENGTH];

    /* initialise our counters */
    if (server != NULL)
    {
        snprintf (app_name, CNTRD_MAX_APP_NAME_LENGTH, "%s%s%s",
                  CMSG_COUNTER_APP_NAME_PREFIX, service->descriptor->name,
                  transport->tport_id);

        if (cmsg_server_counter_create (server, app_name) != CMSG_RET_OK)
        {
            CMSG_LOG_GEN_ERROR ("[%s] Unable to create server counters.", app_name);
        }
    }
#endif

    return server;
}


void
cmsg_server_destroy (cmsg_server *server)
{
    int fd;

    CMSG_ASSERT_RETURN_VOID (server != NULL);

    // Close accepted sockets before destroying server
    for (fd = 0; fd <= server->accepted_fdmax; fd++)
    {
        if (FD_ISSET (fd, &server->accepted_fdset))
        {
            close (fd);
        }
    }

    /* Free counter session info but do not destroy counter data in the shared memory */
#ifdef HAVE_COUNTERD
    cntrd_app_unInit_app (&server->cntr_session, CNTRD_APP_PERSISTENT);
#endif
    server->cntr_session = NULL;

    cmsg_queue_filter_free (server->queue_filter_hash_table, server->service->descriptor);
    g_hash_table_destroy (server->queue_filter_hash_table);
    cmsg_receive_queue_free_all (server->queue);
    pthread_mutex_destroy (&server->queueing_state_mutex);
    pthread_mutex_destroy (&server->queue_mutex);

    if (server->_transport)
    {
        server->_transport->server_destroy (server->_transport);
    }

    CMSG_FREE (server);
}

// create counters
int32_t
cmsg_server_counter_create (cmsg_server *server, char *app_name)
{
    int32_t ret = CMSG_RET_ERR;

#ifdef HAVE_COUNTERD
    if (cntrd_app_init_app (app_name, CNTRD_APP_PERSISTENT, (void **) &server->cntr_session)
        == CNTRD_APP_SUCCESS)
    {
        cntrd_app_register_ctr_in_group (server->cntr_session, "Server Unknown RPC",
                                         &server->cntr_unknown_rpc);
        cntrd_app_register_ctr_in_group (server->cntr_session, "Server RPC Calls",
                                         &server->cntr_rpc);
        cntrd_app_register_ctr_in_group (server->cntr_session, "Server Unknown Fields",
                                         &server->cntr_unknown_fields);
        cntrd_app_register_ctr_in_group (server->cntr_session, "Server Msgs Queued",
                                         &server->cntr_messages_queued);
        cntrd_app_register_ctr_in_group (server->cntr_session, "Server Msgs Dropped",
                                         &server->cntr_messages_dropped);
        cntrd_app_register_ctr_in_group (server->cntr_session, "Server Connect Accepts",
                                         &server->cntr_connections_accepted);
        cntrd_app_register_ctr_in_group (server->cntr_session, "Server Connect Closed",
                                         &server->cntr_connections_closed);
        cntrd_app_register_ctr_in_group (server->cntr_session, "Server Errors: General",
                                         &server->cntr_errors);
        cntrd_app_register_ctr_in_group (server->cntr_session, "Server Errors: Poll",
                                         &server->cntr_poll_errors);
        cntrd_app_register_ctr_in_group (server->cntr_session, "Server Errors: Recv",
                                         &server->cntr_recv_errors);
        cntrd_app_register_ctr_in_group (server->cntr_session, "Server Errors: Send",
                                         &server->cntr_send_errors);
        cntrd_app_register_ctr_in_group (server->cntr_session, "Server Errors: Pack",
                                         &server->cntr_pack_errors);
        cntrd_app_register_ctr_in_group (server->cntr_session, "Server Errors: Memory",
                                         &server->cntr_memory_errors);
        cntrd_app_register_ctr_in_group (server->cntr_session, "Server Errors: Protocol",
                                         &server->cntr_protocol_errors);
        cntrd_app_register_ctr_in_group (server->cntr_session, "Server Errors: Queue",
                                         &server->cntr_queue_errors);

        /* Tell cntrd not to destroy the counter data in the shared memory */
        cntrd_app_set_shutdown_instruction (app_name, CNTRD_SHUTDOWN_RESTART);
        ret = CMSG_RET_OK;
    }
#endif

    return ret;
}

int
cmsg_server_get_socket (cmsg_server *server)
{
    int socket = 0;

    CMSG_ASSERT_RETURN_VAL (server != NULL, -1);
    CMSG_ASSERT_RETURN_VAL (server->_transport != NULL, -1);

    socket = server->_transport->s_socket (server->_transport);

    CMSG_DEBUG (CMSG_INFO, "[SERVER] done. socket: %d\n", socket);

    return socket;
}

cmsg_server_list *
cmsg_server_list_new (void)
{
    cmsg_server_list *server_list;

    server_list = (cmsg_server_list *) CMSG_CALLOC (1, sizeof (cmsg_server_list));
    if (server_list)
    {
        if (pthread_mutex_init (&server_list->server_mutex, NULL) != 0)
        {
            CMSG_LOG_GEN_ERROR ("Failed to create server list mutex");
        }
    }
    else
    {
        CMSG_LOG_GEN_ERROR ("Unable to create server list.");
    }

    return server_list;
}

void
cmsg_server_list_destroy (cmsg_server_list *server_list)
{
    CMSG_ASSERT_RETURN_VOID (server_list != NULL);
    CMSG_ASSERT_RETURN_VOID (g_list_length (server_list->list) == 0);

    if (pthread_mutex_destroy (&server_list->server_mutex) != 0)
    {
        CMSG_LOG_GEN_ERROR ("Failed to destroy server list mutex");
    }

    CMSG_FREE (server_list);
}

int
cmsg_server_list_is_empty (cmsg_server_list *server_list)
{
    int ret = TRUE;

    if (server_list != NULL)
    {
        pthread_mutex_lock (&server_list->server_mutex);
        ret = g_list_length (server_list->list) == 0;
        pthread_mutex_unlock (&server_list->server_mutex);
    }

    return ret;
}

void
cmsg_server_list_add_server (cmsg_server_list *server_list, cmsg_server *server)
{
    CMSG_ASSERT_RETURN_VOID (server_list);
    CMSG_ASSERT_RETURN_VOID (server);

    pthread_mutex_lock (&server_list->server_mutex);
    server_list->list = g_list_prepend (server_list->list, server);
    pthread_mutex_unlock (&server_list->server_mutex);
}

void
cmsg_server_list_remove_server (cmsg_server_list *server_list, cmsg_server *server)
{
    CMSG_ASSERT_RETURN_VOID (server_list);
    CMSG_ASSERT_RETURN_VOID (server);

    pthread_mutex_lock (&server_list->server_mutex);
    server_list->list = g_list_remove (server_list->list, server);
    pthread_mutex_unlock (&server_list->server_mutex);
}

/**
 * Wait for any data on a list of sockets or until timeout expires.
 *
 * Note: If the select system call is interrupted before any messages
 *       are received (i.e. returns EINTR) then this function will
 *       return success (instead of blocking until the timeout expires)
 *
 * Timeout is specified in 'timeout_ms' (0: return immediately,
 * negative number: no timeout).
 * On success returns 0, failure returns -1.
 */
int32_t
cmsg_server_receive_poll (cmsg_server *server, int32_t timeout_ms, fd_set *master_fdset,
                          int *fdmax)
{
    int ret = 0;
    fd_set read_fds = *master_fdset;
    int nfds = *fdmax;
    struct timeval timeout = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    int fd;
    int newfd;
    int check_fdmax = FALSE;
    int listen_socket;

    CMSG_ASSERT_RETURN_VAL (server != NULL, CMSG_RET_ERR);

    listen_socket = cmsg_server_get_socket (server);

    ret = select (nfds + 1, &read_fds, NULL, NULL, (timeout_ms < 0) ? NULL : &timeout);
    if (ret == -1)
    {
        if (errno == EINTR)
        {
            // We were interrupted, this is transient so just pretend we timed out.
            return CMSG_RET_OK;
        }

        CMSG_LOG_SERVER_ERROR (server,
                               "An error occurred with receive poll (timeout %dms): %s.",
                               timeout_ms, strerror (errno));
        CMSG_COUNTER_INC (server, cntr_poll_errors);
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
                if (cmsg_server_receive (server, fd) < 0)
                {
                    // only close the socket if we have errored
                    cmsg_server_close_wrapper (server);
                    shutdown (fd, SHUT_RDWR);
                    close (fd);
                    FD_CLR (fd, master_fdset);
                    check_fdmax = TRUE;
                }
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
 *
 * Note: If the select system call is interrupted before any messages
 *       are received (i.e. returns EINTR) then this function will
 *       return success (instead of blocking until the timeout expires)
 *
 * Timeout : (0: return immediately, +: wait in milli-seconds, -: no timeout).
 * On success returns 0, failure returns -1.
 */
int32_t
cmsg_server_receive_poll_list (cmsg_server_list *server_list, int32_t timeout_ms)
{
    struct timeval timeout = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    cmsg_server *server = NULL;
    GList *node;
    fd_set read_fds;
    int fdmax;
    int ret;
    int listen_socket;
    int fd;
    int newfd;

    if (!server_list)
    {
        CMSG_COUNTER_INC (server, cntr_errors);
        return 0;
    }

    pthread_mutex_lock (&server_list->server_mutex);
    if (g_list_length (server_list->list) == 0)
    {
        pthread_mutex_unlock (&server_list->server_mutex);

        // Nothing to do
        return 0;
    }

    FD_ZERO (&read_fds);

    // Collect fds to examine, make sure the list cannot be changed while we traverse it.
    fdmax = 0;
    for (node = g_list_first (server_list->list); node && node->data;
         node = g_list_next (node))
    {
        server = (cmsg_server *) node->data;

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
    pthread_mutex_unlock (&server_list->server_mutex);

    // Check any data is available
    ret = select (fdmax + 1, &read_fds, NULL, NULL, (timeout_ms < 0) ? NULL : &timeout);
    if (ret == -1)
    {
        if (errno == EINTR)
        {
            // We were interrupted, this is transient so just pretend we timed out.
            return CMSG_RET_OK;
        }

        CMSG_LOG_SERVER_ERROR (server,
                               "An error occurred with list receive poll (timeout: %dms): %s.",
                               timeout_ms, strerror (errno));
        CMSG_COUNTER_INC (server, cntr_poll_errors);
        return CMSG_RET_ERR;
    }
    else if (ret == 0)
    {
        // timed out, so func success but nothing received, early return
        return CMSG_RET_OK;
    }

    // Process any data available on the sockets, make sure the list cannot be changed while
    // we traverse it. Changes between the first and second traversal should not cause any
    // problems.
    pthread_mutex_lock (&server_list->server_mutex);
    for (node = g_list_first (server_list->list); node && node->data;
         node = g_list_next (node))
    {
        server = (cmsg_server *) node->data;
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
                    if (cmsg_server_receive (server, fd) < 0)
                    {
                        // only close the socket if we have errored
                        cmsg_server_close_wrapper (server);
                        shutdown (fd, SHUT_RDWR);
                        close (fd);
                        FD_CLR (fd, &server->accepted_fdset);
                        if (server->accepted_fdmax == fd)
                        {
                            server->accepted_fdmax--;
                        }
                    }

                    FD_CLR (fd, &read_fds);
                }
            }
        }
    }
    pthread_mutex_unlock (&server_list->server_mutex);

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

    CMSG_ASSERT_RETURN_VAL (server != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (server->_transport != NULL, CMSG_RET_ERR);

    ret = server->_transport->server_recv (socket, server);

    if (ret < 0)
    {
        CMSG_DEBUG (CMSG_INFO,
                    "[SERVER] server receive failed, server %s transport type %d socket %d ret %d\n",
                    server->service->descriptor->name, server->_transport->type, socket,
                    ret);

        /* Count an unknown method error separately */
        if (ret == CMSG_RET_METHOD_NOT_FOUND)
        {
            CMSG_COUNTER_INC (server, cntr_unknown_rpc);
        }
        /* Do not count as an error if the peer has performed an orderly shutdown */
        else if (ret != CMSG_RET_CLOSED)
        {
            CMSG_COUNTER_INC (server, cntr_recv_errors);
        }

        /* Caller function closes this socket on failure */
        CMSG_COUNTER_INC (server, cntr_connections_closed);

        return CMSG_RET_ERR;
    }

    return CMSG_RET_OK;
}


/* Accept an incoming socket from a client */
int32_t
cmsg_server_accept (cmsg_server *server, int32_t listen_socket)
{
    int sock = -1;

    CMSG_ASSERT_RETURN_VAL (server != NULL, -1);

    if (server->_transport->server_accept != NULL)
    {
        sock = server->_transport->server_accept (listen_socket, server->_transport);
        if (sock >= 0 && server->_transport->use_crypto &&
            server->_transport->config.socket.crypto.accept)
        {
            server->_transport->config.socket.crypto.accept (sock);
        }
        // count the accepted connection
        CMSG_COUNTER_INC (server, cntr_connections_accepted);
    }

    return sock;
}


/**
 * Callback function for CMSG server when a new socket is accepted.
 * This function is for applications that accept sockets by other than CMSG API,
 * cmsg_server_accept() (e.g. by using liboop socket utility functions).
 */
void
cmsg_server_accept_callback (cmsg_server *server, int32_t sock)
{
    // count the accepted connection
    if (server != NULL)
    {
        CMSG_COUNTER_INC (server, cntr_connections_accepted);
    }
}


/**
 * Assumes that server_request will have been set in the server by the caller.
 */
void
cmsg_server_invoke (cmsg_server *server, uint32_t method_index, ProtobufCMessage *message,
                    cmsg_method_processing_reason process_reason)
{
    uint32_t queue_length = 0;
    cmsg_server_closure_data closure_data;

    CMSG_ASSERT_RETURN_VOID (server != NULL);

    // Setup closure_data so it can be used no matter what the action is
    closure_data.server = server;
    closure_data.method_processing_reason = process_reason;

    // increment the counter if this message has unknown fields,
    if (message->unknown_fields)
    {
        CMSG_COUNTER_INC (server, cntr_unknown_fields);
    }

    switch (process_reason)
    {
    case CMSG_METHOD_OK_TO_INVOKE:
    case CMSG_METHOD_INVOKING_FROM_QUEUE:
        server->service->invoke (server->service,
                                 method_index,
                                 message,
                                 server->_transport->closure, (void *) &closure_data);

        if (!(server->app_owns_current_msg || server->app_owns_all_msgs))
        {
            protobuf_c_message_free_unpacked (message, &cmsg_memory_allocator);
        }
        server->app_owns_current_msg = FALSE;

        // Closure is called by the invoke.
        break;

    case CMSG_METHOD_QUEUED:
        // Add to queue
        pthread_mutex_lock (&server->queue_mutex);
        cmsg_receive_queue_push (server->queue, (uint8_t *) message, method_index);
        queue_length = g_queue_get_length (server->queue);
        pthread_mutex_unlock (&server->queue_mutex);

        CMSG_DEBUG (CMSG_ERROR, "[SERVER] queue length: %d\n", queue_length);
        if (queue_length > server->maxQueueLength)
        {
            server->maxQueueLength = queue_length;
        }

        // Send response, if required by the closure function
        server->_transport->closure (message, (void *) &closure_data);
        // count this as queued
        CMSG_COUNTER_INC (server, cntr_messages_queued);
        break;

    case CMSG_METHOD_DROPPED:
        // Send response, if required by the closure function
        server->_transport->closure (message, (void *) &closure_data);

        // count this as dropped
        CMSG_COUNTER_INC (server, cntr_messages_dropped);

        // Free the unpacked message
        protobuf_c_message_free_unpacked (message, &cmsg_memory_allocator);
        break;

    default:
        // Don't want to do anything in this case.
        break;
    }
}


/**
 * Invokes the server _impl_ function directly. Used by the loopback client,
 * which has no actual IPC involved (i.e. the client _api_ call actually invokes
 * the _impl_ code in the same process space).
 */
void
cmsg_server_invoke_direct (cmsg_server *server, const ProtobufCMessage *input,
                           uint32_t method_index)
{
    cmsg_server_request server_request;
    ProtobufCService *service = server->service;
    const char *method_name;

    method_name = service->descriptor->methods[method_index].name;

    /* setup the server request, which is needed to get a response sent back */
    server_request.msg_type = CMSG_MSG_TYPE_METHOD_REQ;
    server_request.message_length = protobuf_c_message_get_packed_size (input);
    server_request.method_index = method_index;
    strcpy (server_request.method_name_recvd, method_name);
    server->server_request = &server_request;

    /* call the server invoke function. */
    cmsg_server_invoke (server, method_index, (ProtobufCMessage *) input,
                        CMSG_METHOD_OK_TO_INVOKE);
}


/**
 * Process a METHOD_REQ message.
 *
 * Unpack the parameters, perform filtering (if applicable) and invoke the method.
 *
 * @returns -1 on failure, 0 on success
 */
static int32_t
_cmsg_server_method_req_message_processor (cmsg_server *server, uint8_t *buffer_data)
{
    cmsg_queue_filter_type action;
    cmsg_method_processing_reason processing_reason = CMSG_METHOD_OK_TO_INVOKE;
    ProtobufCMessage *message = NULL;
    ProtobufCAllocator *allocator = &cmsg_memory_allocator;
    cmsg_server_request *server_request = server->server_request;
    const char *method_name;
    const ProtobufCMessageDescriptor *desc;

    method_name = server->service->descriptor->methods[server_request->method_index].name;
    desc = server->service->descriptor->methods[server_request->method_index].input;

    if (server_request->method_index >= server->service->descriptor->n_methods)
    {
        // count this as an unknown rpc request
        CMSG_COUNTER_INC (server, cntr_unknown_rpc);
        CMSG_LOG_SERVER_ERROR (server,
                               "Server request method index is too high. idx %d, max %d.",
                               server_request->method_index,
                               server->service->descriptor->n_methods);
        return CMSG_RET_ERR;
    }
    // count every rpc call
    CMSG_COUNTER_INC (server, cntr_rpc);
    if (buffer_data)
    {
        CMSG_DEBUG (CMSG_INFO, "[SERVER] processsing message with data\n");
        CMSG_DEBUG (CMSG_INFO, "[SERVER] unpacking message\n");

        //unpack the message
        message = protobuf_c_message_unpack (desc, allocator,
                                             server_request->message_length, buffer_data);
    }
    else
    {
        CMSG_DEBUG (CMSG_INFO, "[SERVER] processsing message without data\n");
        //create a new empty message
        // ATL_1716_TODO need to allocate message before init'ing it
        protobuf_c_message_init (desc, message);
    }

    if (message == NULL)
    {
        CMSG_LOG_SERVER_ERROR (server,
                               "Error unpacking the message for method %s. No message.",
                               method_name);
        CMSG_COUNTER_INC (server, cntr_pack_errors);

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
        action = cmsg_server_queue_filter_lookup (server, method_name);

        if (action == CMSG_QUEUE_FILTER_ERROR)
        {
            CMSG_LOG_SERVER_ERROR (server,
                                   "An error occurred with queue_lookup_filter: %s.",
                                   method_name);
            CMSG_COUNTER_INC (server, cntr_queue_errors);
            // Free unpacked message prior to return
            protobuf_c_message_free_unpacked (message, allocator);
            return CMSG_RET_ERR;
        }
        else if (action == CMSG_QUEUE_FILTER_DROP)
        {
            CMSG_DEBUG (CMSG_INFO, "[SERVER] dropping message: %s\n", method_name);

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

    CMSG_DEBUG (CMSG_INFO, "[SERVER] end of message processor\n");

    return CMSG_RET_OK;
}

/**
 * Wrap the sending of a buffer so that the input buffer can be encrypted if required
 *
 * @returns -1 on failure, 0 on success
 */
static int
cmsg_server_send_wrapper (cmsg_server *server, void *buff, int length, int flag)
{
    uint8_t *encrypt_buffer;
    int encrypt_length;
    int ret = 0;
    int sock;

    if (server->_transport->config.socket.crypto.encrypt)
    {
        sock = server->_transport->connection.sockets.client_socket;
        encrypt_buffer = (uint8_t *) CMSG_CALLOC (1, length + ENCRYPT_EXTRA);
        if (encrypt_buffer == NULL)
        {
            CMSG_LOG_SERVER_ERROR (server, "Server failed to allocate buffer on socket %d",
                                   sock);
            return -1;
        }

        encrypt_length =
            server->_transport->config.socket.crypto.encrypt (sock, buff, length,
                                                              encrypt_buffer,
                                                              length + ENCRYPT_EXTRA);
        if (encrypt_length < 0)
        {
            CMSG_LOG_SERVER_ERROR (server, "Server encrypt on socket %d failed", sock);
            CMSG_FREE (encrypt_buffer);
            return -1;
        }

        ret = server->_transport->server_send (server->_transport, encrypt_buffer,
                                               encrypt_length, 0);
        /* if the send was successful, fixup the return length to match the original
         * plaintext length so callers are unaware of the encryption */
        if (encrypt_length == ret)
        {
            ret = length;
        }

        CMSG_FREE (encrypt_buffer);
    }
    else
    {
        ret = server->_transport->server_send (server->_transport, buff, length, 0);
    }

    return ret;
}

/**
 * Process ECHO_REQ message
 *
 * We reply straight away to an ECHO_REQ
 */
static int32_t
_cmsg_server_echo_req_message_processor (cmsg_server *server, uint8_t *buffer_data)
{
    int ret = 0;
    cmsg_header header;

    header = cmsg_header_create (CMSG_MSG_TYPE_ECHO_REPLY, 0, 0 /* empty msg */ ,
                                 CMSG_STATUS_CODE_SUCCESS);

    CMSG_DEBUG (CMSG_INFO, "[SERVER] ECHO Reply header\n");

    cmsg_buffer_print ((void *) &header, sizeof (header));

    ret = cmsg_server_send_wrapper (server, &header, sizeof (header), 0);
    if (ret < (int) sizeof (header))
    {
        CMSG_LOG_SERVER_ERROR (server, "Sending of echo reply failed. Sent:%d of %u bytes.",
                               ret, (uint32_t) sizeof (header));
        CMSG_COUNTER_INC (server, cntr_send_errors);
        return CMSG_RET_ERR;
    }
    return CMSG_RET_OK;
}


/**
 * The buffer has been received and now needs to be processed by protobuf-c.
 * Once unpacked the method will be invoked.
 * If the
 */
int32_t
cmsg_server_message_processor (cmsg_server *server, uint8_t *buffer_data)
{
    CMSG_ASSERT_RETURN_VAL (server != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (buffer_data != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (server->server_request != NULL, CMSG_RET_ERR);

    cmsg_server_request *server_request = server->server_request;

    // Check that the msg received is a type we support
    switch (server_request->msg_type)
    {
    case CMSG_MSG_TYPE_METHOD_REQ:
        return _cmsg_server_method_req_message_processor (server, buffer_data);
        break;

    case CMSG_MSG_TYPE_ECHO_REQ:
        return _cmsg_server_echo_req_message_processor (server, buffer_data);
        break;

    case CMSG_MSG_TYPE_CONN_OPEN:
        // ignore and return
        return CMSG_RET_OK;
        break;

    default:
        CMSG_LOG_SERVER_ERROR (server,
                               "Received a message type the server doesn't support: %d.",
                               server_request->msg_type);
        CMSG_COUNTER_INC (server, cntr_protocol_errors);
        return CMSG_RET_ERR;
    }

}


void
cmsg_server_empty_method_reply_send (cmsg_server *server, cmsg_status_code status_code,
                                     uint32_t method_index)
{
    int ret = 0;
    cmsg_header header;

    CMSG_ASSERT_RETURN_VOID (server != NULL);

    header = cmsg_header_create (CMSG_MSG_TYPE_METHOD_REPLY, 0, 0 /* empty msg */ ,
                                 status_code);

    CMSG_DEBUG (CMSG_INFO, "[SERVER] response header\n");

    cmsg_buffer_print ((void *) &header, sizeof (header));

    ret = cmsg_server_send_wrapper (server, &header, sizeof (header), 0);
    if (ret < (int) sizeof (header))
    {
        CMSG_DEBUG (CMSG_ERROR,
                    "[SERVER] error: sending of response failed sent:%d of %d bytes.\n",
                    ret, (int) sizeof (header));
        CMSG_COUNTER_INC (server, cntr_send_errors);
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

    cmsg_server_closure_data *closure_data = (cmsg_server_closure_data *) closure_data_void;
    cmsg_server *server = closure_data->server;

    CMSG_ASSERT_RETURN_VOID (server != NULL);
    CMSG_ASSERT_RETURN_VOID (closure_data_void != NULL);
    CMSG_ASSERT_RETURN_VOID (server->_transport != NULL);
    CMSG_ASSERT_RETURN_VOID (server->server_request != NULL);

    cmsg_server_request *server_request = server->server_request;
    uint32_t ret = 0;
    int send_ret = 0;
    int type = CMSG_TLV_METHOD_TYPE;

    CMSG_DEBUG (CMSG_INFO, "[SERVER] invoking rpc method=%d\n",
                server_request->method_index);

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
        CMSG_DEBUG (CMSG_INFO, "[SERVER] method %d queued, sending response without data\n",
                    server_request->method_index);

        cmsg_server_empty_method_reply_send (server, CMSG_STATUS_CODE_SERVICE_QUEUED,
                                             server_request->method_index);
        return;
    }
    /* If the method has been dropped due a filter then send a response with no data.
     * This allows the other end to unblock.
     */
    else if (closure_data->method_processing_reason == CMSG_METHOD_DROPPED)
    {
        CMSG_DEBUG (CMSG_INFO,
                    "[SERVER] method %d dropped, sending response without data\n",
                    server_request->method_index);

        cmsg_server_empty_method_reply_send (server, CMSG_STATUS_CODE_SERVICE_DROPPED,
                                             server_request->method_index);
        return;
    }
    /* No response message was specified, therefore reply with an error
     */
    else if (!message)
    {
        CMSG_DEBUG (CMSG_INFO, "[SERVER] sending response without data\n");

        cmsg_server_empty_method_reply_send (server, CMSG_STATUS_CODE_SERVICE_FAILED,
                                             server_request->method_index);
        CMSG_COUNTER_INC (server, cntr_memory_errors);
        return;
    }
    /* Method has executed normally and has a response to be sent.
     */
    else
    {
        CMSG_DEBUG (CMSG_INFO, "[SERVER] sending response with data\n");

        int method_len = strlen (server_request->method_name_recvd) + 1;

        cmsg_header header;
        uint32_t packed_size = protobuf_c_message_get_packed_size (message);
        uint32_t extra_header_size = CMSG_TLV_SIZE (method_len);
        uint32_t total_header_size = sizeof (header) + extra_header_size;
        uint32_t total_message_size = total_header_size + packed_size;

        header = cmsg_header_create (CMSG_MSG_TYPE_METHOD_REPLY, extra_header_size,
                                     packed_size, CMSG_STATUS_CODE_SUCCESS);

        uint8_t *buffer = (uint8_t *) CMSG_CALLOC (1, total_message_size);
        if (!buffer)
        {
            CMSG_LOG_SERVER_ERROR (server, "Unable to allocate memory for message.");
            CMSG_COUNTER_INC (server, cntr_memory_errors);
            return;
        }

        cmsg_tlv_method_header_create (buffer, header, type, method_len,
                                       server_request->method_name_recvd);
        uint8_t *buffer_data = buffer + total_header_size;

        ret = protobuf_c_message_pack (message, buffer_data);
        if (ret < packed_size)
        {
            CMSG_LOG_SERVER_ERROR (server,
                                   "Underpacked message data. Packed %d of %d bytes.", ret,
                                   packed_size);
            CMSG_COUNTER_INC (server, cntr_pack_errors);
            CMSG_FREE (buffer);
            return;
        }
        else if (ret > packed_size)
        {
            CMSG_LOG_SERVER_ERROR
                (server, "Overpacked message data. Packed %d of %d bytes.", ret,
                 packed_size);
            CMSG_COUNTER_INC (server, cntr_pack_errors);
            CMSG_FREE (buffer);
            return;
        }

        CMSG_DEBUG (CMSG_INFO, "[SERVER] response header\n");
        cmsg_buffer_print ((void *) &header, sizeof (header));

        CMSG_DEBUG (CMSG_INFO, "[SERVER] response data\n");
        cmsg_buffer_print ((void *) buffer_data, packed_size);

        send_ret = cmsg_server_send_wrapper (server, buffer, total_message_size, 0);

        if (send_ret < (int) total_message_size)
        {
            CMSG_LOG_SERVER_ERROR (server,
                                   "sending of reply failed send:%d of %d, error %s\n",
                                   send_ret, total_message_size, strerror (errno));
            CMSG_COUNTER_INC (server, cntr_send_errors);
        }

        CMSG_FREE (buffer);
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

/**
 * Has to be called from server receive thread in application!
 */
int32_t
cmsg_server_queue_process (cmsg_server *server)
{
    int32_t processed = 0;

    CMSG_ASSERT_RETURN_VAL (server != NULL, processed);

    pthread_mutex_lock (&server->queueing_state_mutex);

    if (server->queueing_state == CMSG_QUEUE_STATE_TO_DISABLED)
    {
        if (!server->queue_in_process)
        {
            server->queue_in_process = TRUE;

            pthread_mutex_lock (&server->queue_filter_mutex);
            cmsg_queue_filter_set_all (server->queue_filter_hash_table,
                                       server->service->descriptor,
                                       CMSG_QUEUE_FILTER_QUEUE);
            pthread_mutex_unlock (&server->queue_filter_mutex);
        }

        if (server->queue_process_number >= 0)
        {
            processed =
                cmsg_receive_queue_process_some (server->queue, &server->queue_mutex,
                                                 server, server->queue_process_number);
        }
        else if (server->queue_process_number == -1)
        {
            processed = cmsg_receive_queue_process_all (server->queue, &server->queue_mutex,
                                                        server);
        }

        if (processed > 0)
        {
            CMSG_DEBUG (CMSG_INFO,
                        "server has processed: %d messages in CMSG_QUEUE_STATE_TO_DISABLED state",
                        processed);
        }

        if (cmsg_server_queue_get_length (server) == 0)
        {
            server->queue_process_number = 0;
            server->queue_in_process = FALSE;

            pthread_mutex_lock (&server->queue_filter_mutex);
            cmsg_queue_filter_clear_all (server->queue_filter_hash_table,
                                         server->service->descriptor);
            pthread_mutex_unlock (&server->queue_filter_mutex);

            server->queueing_state = CMSG_QUEUE_STATE_DISABLED;
        }
    }
    else if (server->queueing_state == CMSG_QUEUE_STATE_ENABLED)
    {
        if (server->queue_process_number >= 0)
        {
            processed =
                cmsg_receive_queue_process_some (server->queue, &server->queue_mutex,
                                                 server, server->queue_process_number);
        }
        else if (server->queue_process_number == -1)
        {
            processed = cmsg_receive_queue_process_all (server->queue, &server->queue_mutex,
                                                        server);
        }
        if (processed > 0)
        {
            CMSG_DEBUG (CMSG_INFO,
                        "server has processed: %d messages in CMSG_QUEUE_STATE_ENABLED state",
                        processed);
        }
    }

    if (server->queueing_state != server->queueing_state_last)
    {
        switch (server->queueing_state)
        {
        case CMSG_QUEUE_STATE_ENABLED:
            CMSG_DEBUG (CMSG_INFO, "server state changed to: CMSG_QUEUE_STATE_ENABLED");
            break;
        case CMSG_QUEUE_STATE_TO_DISABLED:
            CMSG_DEBUG (CMSG_INFO, "server state changed to: CMSG_QUEUE_STATE_TO_DISABLED");
            break;
        case CMSG_QUEUE_STATE_DISABLED:
            CMSG_DEBUG (CMSG_INFO, "server state changed to: CMSG_QUEUE_STATE_DISABLED");
            break;
        default:
            break;
        }
    }

    server->queueing_state_last = server->queueing_state;

    pthread_mutex_unlock (&server->queueing_state_mutex);

    return processed;
}

int32_t
cmsg_server_queue_process_some (cmsg_server *server, int32_t number_to_process)
{
    CMSG_ASSERT_RETURN_VAL (server != NULL, 0);

    pthread_mutex_lock (&server->queueing_state_mutex);
    server->queue_process_number = number_to_process;
    pthread_mutex_unlock (&server->queueing_state_mutex);

    return cmsg_server_queue_process (server);
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

    return cmsg_server_queue_request_process_all (server);
}

uint32_t
cmsg_server_queue_get_length (cmsg_server *server)
{
    if (server == NULL)
    {
        return 0;
    }

    pthread_mutex_lock (&server->queue_mutex);
    uint32_t queue_length = g_queue_get_length (server->queue);
    pthread_mutex_unlock (&server->queue_mutex);

    return queue_length;
}


uint32_t
cmsg_server_queue_max_length_get (cmsg_server *server)
{
    if (server == NULL)
    {
        return 0;
    }

    return server->maxQueueLength;
}


/**
 * Processes the up to the given number of items to process out of the queue
 */
int32_t
cmsg_server_queue_request_process_some (cmsg_server *server, uint32_t num_to_process)
{
    //thread save, will be executed on the next server receive in the server thread
    cmsg_bool_t queue_in_process = TRUE;
    pthread_mutex_lock (&server->queueing_state_mutex);
    server->queue_process_number = num_to_process;
    pthread_mutex_unlock (&server->queueing_state_mutex);

    while (queue_in_process == TRUE)
    {
        pthread_mutex_lock (&server->queueing_state_mutex);
        queue_in_process = server->queue_in_process;
        pthread_mutex_unlock (&server->queueing_state_mutex);
    }

    return CMSG_RET_OK;
}


/**
 * Processes all the items in the queue.
 *
 * @returns the number of items processed off the queue
 */
int32_t
cmsg_server_queue_request_process_all (cmsg_server *server)
{
    //thread save, will be executed on the next server receive in the server thread
    cmsg_bool_t queue_in_process = TRUE;
    pthread_mutex_lock (&server->queueing_state_mutex);
    server->queue_process_number = -1;
    pthread_mutex_unlock (&server->queueing_state_mutex);

    while (queue_in_process == TRUE)
    {
        pthread_mutex_lock (&server->queueing_state_mutex);
        queue_in_process = server->queue_in_process;
        pthread_mutex_unlock (&server->queueing_state_mutex);
    }

    return CMSG_RET_OK;
}

void
cmsg_server_queue_filter_set_all (cmsg_server *server, cmsg_queue_filter_type filter_type)
{
    pthread_mutex_lock (&server->queueing_state_mutex);

    if ((filter_type == CMSG_QUEUE_FILTER_PROCESS) ||
        (filter_type == CMSG_QUEUE_FILTER_DROP))
    {
        server->queueing_state = CMSG_QUEUE_STATE_TO_DISABLED;
    }
    else if (filter_type == CMSG_QUEUE_FILTER_QUEUE)
    {
        server->queueing_state = CMSG_QUEUE_STATE_ENABLED;
    }

    pthread_mutex_lock (&server->queue_filter_mutex);
    cmsg_queue_filter_set_all (server->queue_filter_hash_table, server->service->descriptor,
                               filter_type);

    pthread_mutex_unlock (&server->queue_filter_mutex);
    pthread_mutex_unlock (&server->queueing_state_mutex);
}

void
cmsg_server_queue_filter_clear_all (cmsg_server *server)
{
    pthread_mutex_lock (&server->queueing_state_mutex);

    server->queueing_state = CMSG_QUEUE_STATE_TO_DISABLED;

    pthread_mutex_lock (&server->queue_filter_mutex);
    cmsg_queue_filter_clear_all (server->queue_filter_hash_table,
                                 server->service->descriptor);

    pthread_mutex_unlock (&server->queue_filter_mutex);
    pthread_mutex_unlock (&server->queueing_state_mutex);
}

int32_t
cmsg_server_queue_filter_set (cmsg_server *server, const char *method,
                              cmsg_queue_filter_type filter_type)
{
    int32_t ret;

    pthread_mutex_lock (&server->queueing_state_mutex);

    pthread_mutex_lock (&server->queue_filter_mutex);
    ret = cmsg_queue_filter_set (server->queue_filter_hash_table, method, filter_type);
    server->queueing_state =
        cmsg_queue_filter_get_type (server->queue_filter_hash_table,
                                    server->service->descriptor);

    pthread_mutex_unlock (&server->queue_filter_mutex);
    pthread_mutex_unlock (&server->queueing_state_mutex);

    return ret;
}

int32_t
cmsg_server_queue_filter_clear (cmsg_server *server, const char *method)
{
    int32_t ret;

    pthread_mutex_lock (&server->queueing_state_mutex);

    pthread_mutex_lock (&server->queue_filter_mutex);

    ret = cmsg_queue_filter_clear (server->queue_filter_hash_table, method);
    server->queueing_state =
        cmsg_queue_filter_get_type (server->queue_filter_hash_table,
                                    server->service->descriptor);

    pthread_mutex_unlock (&server->queue_filter_mutex);
    pthread_mutex_unlock (&server->queueing_state_mutex);

    return ret;
}

void
cmsg_server_queue_filter_init (cmsg_server *server)
{
    pthread_mutex_lock (&server->queue_filter_mutex);
    cmsg_queue_filter_init (server->queue_filter_hash_table, server->service->descriptor);
    pthread_mutex_unlock (&server->queue_filter_mutex);
}

cmsg_queue_filter_type
cmsg_server_queue_filter_lookup (cmsg_server *server, const char *method)
{
    cmsg_queue_filter_type ret;
    pthread_mutex_lock (&server->queue_filter_mutex);
    ret = cmsg_queue_filter_lookup (server->queue_filter_hash_table, method);
    pthread_mutex_unlock (&server->queue_filter_mutex);

    return ret;
}

static cmsg_server *
_cmsg_create_server_tipc (const char *server_name, int member_id, int scope,
                          ProtobufCService *descriptor, cmsg_transport_type transport_type)
{
    cmsg_transport *transport = NULL;
    cmsg_server *server = NULL;

    transport = cmsg_create_transport_tipc (server_name, member_id, scope, transport_type);
    if (transport == NULL)
    {
        return NULL;
    }

    server = cmsg_server_new (transport, descriptor);
    if (server == NULL)
    {
        cmsg_transport_destroy (transport);
        CMSG_LOG_GEN_ERROR ("[%s%s] Failed to create TIPC server for member %d.",
                            descriptor->descriptor->name, transport->tport_id, member_id);
        return NULL;
    }

    return server;
}

cmsg_server *
cmsg_create_server_tipc_rpc (const char *server_name, int member_id, int scope,
                             ProtobufCService *descriptor)
{
    CMSG_ASSERT_RETURN_VAL (server_name != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (descriptor != NULL, NULL);

    return _cmsg_create_server_tipc (server_name, member_id, scope, descriptor,
                                     CMSG_TRANSPORT_RPC_TIPC);
}

cmsg_server *
cmsg_create_server_tipc_oneway (const char *server_name, int member_id, int scope,
                                ProtobufCService *descriptor)
{
    CMSG_ASSERT_RETURN_VAL (server_name != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (descriptor != NULL, NULL);

    return _cmsg_create_server_tipc (server_name, member_id, scope, descriptor,
                                     CMSG_TRANSPORT_ONEWAY_TIPC);
}

static cmsg_server *
_cmsg_create_server_unix (ProtobufCService *descriptor, cmsg_transport_type transport_type)
{
    cmsg_transport *transport = NULL;
    cmsg_server *server = NULL;

    transport = cmsg_create_transport_unix (descriptor->descriptor, transport_type);
    if (transport == NULL)
    {
        CMSG_LOG_GEN_ERROR ("[%s] Failed to create UNIX IPC server.",
                            descriptor->descriptor->name);
        return NULL;
    }

    server = cmsg_server_new (transport, descriptor);
    if (server == NULL)
    {
        cmsg_transport_destroy (transport);
        CMSG_LOG_GEN_ERROR ("[%s] Failed to create UNIX IPC server.",
                            descriptor->descriptor->name);
        return NULL;
    }

    return server;
}

cmsg_server *
cmsg_create_server_unix_rpc (ProtobufCService *descriptor)
{
    CMSG_ASSERT_RETURN_VAL (descriptor != NULL, NULL);

    return _cmsg_create_server_unix (descriptor, CMSG_TRANSPORT_RPC_UNIX);
}

cmsg_server *
cmsg_create_server_unix_oneway (ProtobufCService *descriptor)
{
    CMSG_ASSERT_RETURN_VAL (descriptor != NULL, NULL);

    return _cmsg_create_server_unix (descriptor, CMSG_TRANSPORT_ONEWAY_UNIX);
}

static cmsg_server *
_cmsg_create_server_tcp (cmsg_socket *config, ProtobufCService *descriptor,
                         cmsg_transport_type transport_type)
{
    cmsg_transport *transport = NULL;
    cmsg_server *server = NULL;

    transport = cmsg_create_transport_tcp (config, transport_type);
    if (transport == NULL)
    {
        return NULL;
    }

    /* Configure the transport to enable non-existent, non-local address binding */
    cmsg_transport_ipfree_bind_enable (transport, TRUE);

    server = cmsg_server_new (transport, descriptor);
    if (server == NULL)
    {
        cmsg_transport_destroy (transport);
        CMSG_LOG_GEN_ERROR ("[%s] Failed to create TCP RPC server.",
                            descriptor->descriptor->name);
        return NULL;
    }

    return server;
}

cmsg_server *
cmsg_create_server_tcp_rpc (cmsg_socket *config, ProtobufCService *descriptor)
{
    CMSG_ASSERT_RETURN_VAL (config != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (descriptor != NULL, NULL);

    return _cmsg_create_server_tcp (config, descriptor, CMSG_TRANSPORT_RPC_TCP);
}

cmsg_server *
cmsg_create_server_tcp_oneway (cmsg_socket *config, ProtobufCService *descriptor)
{
    CMSG_ASSERT_RETURN_VAL (config != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (descriptor != NULL, NULL);

    return _cmsg_create_server_tcp (config, descriptor, CMSG_TRANSPORT_ONEWAY_TCP);
}

/**
 * Destroy a cmsg server and its transport
 *
 * @param server - the cmsg server to destroy
 */
void
cmsg_destroy_server_and_transport (cmsg_server *server)
{
    cmsg_transport *transport;

    if (server != NULL)
    {
        transport = server->_transport;
        cmsg_server_destroy (server);

        cmsg_transport_destroy (transport);
    }
}

/**
 * @brief Allows the application to take ownership of the current message only.
 * This flag will be reset after each impl function returns.
 *
 * @warning This should only be called from within an impl function.
 * @warning Taking ownership also means the application is responsible for freeing the msg
 *
 * @param server The server you are setting the flag in
 * @returns nothing
 */
void
cmsg_server_app_owns_current_msg_set (cmsg_server *server)
{
    server->app_owns_current_msg = TRUE;
}

/**
 * @brief Allows the application to take ownership of all messages.
 * @brief This flag defaults to FALSE but will never reset once it is set by
 * @bried the application.
 *
 * @warning Taking ownership also means the application is responsible for freeing all msgs
 * @warning  msgs received from this server.
 * @warning If you want CMSG to takeover ownership of new received messages, the application
 * @warning must call this function again with FALSE. Note, the application will still be
 * @warning responsible for freeing any messages it received before resetting this flag.
 * @warning There is no way to change ownership of an existing message once the IMPL exits.
 *
 * @param server The server you are setting the flag in
 * @param app_is_owner Is the application the owner of all messages? TRUE or FALSE
 * @returns nothing
 */
void
cmsg_server_app_owns_all_msgs_set (cmsg_server *server, cmsg_bool_t app_is_owner)
{
    server->app_owns_all_msgs = app_is_owner;
}

/**
 * Close a server socket to a remote client. If the server is using crypto then
 * call out to the server application so it can cleanup any associated
 * crypto structures.
 *
 * NOTE user applications should not call this routine directly
 *
 * @param server - the server that is closing the current client connection
 */
void
cmsg_server_close_wrapper (cmsg_server *server)
{
    int sock = server->_transport->connection.sockets.client_socket;

    if (server->_transport->config.socket.crypto.close)
    {
        server->_transport->config.socket.crypto.close (sock);
    }

    server->_transport->server_close (server->_transport);
}
