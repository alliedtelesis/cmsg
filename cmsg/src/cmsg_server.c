/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#define _GNU_SOURCE

#include <sys/eventfd.h>
#include "cmsg_private.h"
#include "cmsg_server.h"
#include "cmsg_error.h"
#include "cmsg_transport.h"

#ifdef HAVE_COUNTERD
#include "cntrd_app_defines.h"
#include "cntrd_app_api.h"
#endif

static int32_t _cmsg_server_method_req_message_processor (cmsg_server *server,
                                                          uint8_t *buffer_data);

static int32_t _cmsg_server_echo_req_message_processor (cmsg_server *server,
                                                        uint8_t *buffer_data);

static cmsg_server *_cmsg_create_server_tipc (const char *server_name, int member_id,
                                              int scope, ProtobufCService *descriptor,
                                              cmsg_transport_type transport_type);

static void cmsg_server_queue_filter_init (cmsg_server *server);

static cmsg_queue_filter_type cmsg_server_queue_filter_lookup (cmsg_server *server,
                                                               const char *method);

int32_t cmsg_server_counter_create (cmsg_server *server, char *app_name);


static ProtobufCClosure
cmsg_server_get_closure_func (cmsg_transport *transport)
{
    switch (transport->type)
    {
    case CMSG_TRANSPORT_RPC_TCP:
    case CMSG_TRANSPORT_RPC_TIPC:
    case CMSG_TRANSPORT_RPC_USERDEFINED:
    case CMSG_TRANSPORT_LOOPBACK:
    case CMSG_TRANSPORT_RPC_UNIX:
        return cmsg_server_closure_rpc;

    case CMSG_TRANSPORT_ONEWAY_TCP:
    case CMSG_TRANSPORT_ONEWAY_TIPC:
    case CMSG_TRANSPORT_BROADCAST:
    case CMSG_TRANSPORT_ONEWAY_USERDEFINED:
    case CMSG_TRANSPORT_ONEWAY_UNIX:
        return cmsg_server_closure_oneway;
    }

    CMSG_LOG_GEN_ERROR ("Unsupported closure function for transport type");
    return NULL;
}


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

        server->closure = cmsg_server_get_closure_func (transport);

        CMSG_DEBUG (CMSG_INFO, "[SERVER] creating new server with type: %d\n",
                    transport->type);

        ret = transport->tport_funcs.listen (server->_transport);

        if (ret < 0)
        {
            CMSG_FREE (server);
            server = NULL;
            return NULL;
        }

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

        server->queue_in_process = false;

        pthread_mutex_unlock (&server->queueing_state_mutex);

        server->app_owns_current_msg = false;
        server->app_owns_all_msgs = false;
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
                  cmsg_transport_counter_app_tport_id (transport));

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
        server->_transport->tport_funcs.server_destroy (server->_transport);
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

    socket = server->_transport->tport_funcs.s_socket (server->_transport);

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

bool
cmsg_server_list_is_empty (cmsg_server_list *server_list)
{
    bool ret = true;

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
 * Poll a CMSG server that is accepting connections in a separate thread.
 *
 * Note: If the select system call is interrupted before any messages
 *       are received (i.e. returns EINTR) then this function will
 *       return success (instead of blocking until the timeout expires)
 *
 * Timeout is specified in 'timeout_ms' (0: return immediately,
 * negative number: no timeout).
 *
 * @param info - The 'cmsg_server_accept_thread_info' structure containing
 *               the CMSG server and it's connection thread.
 * @param timeout_ms - The timeout to use with select.
 *                     (0: return immediately, negative number: no timeout).
 * @param master_fdset - An fd_set containing all of the sockets being polled.
 * @param fdmax - Pointer to an integer holding the maximum fd number.
 *
 * @returns On success returns 0, failure returns -1.
 */
int32_t
cmsg_server_thread_receive_poll (cmsg_server_accept_thread_info *info,
                                 int32_t timeout_ms, fd_set *master_fdset, int *fdmax)
{
    int ret = 0;
    fd_set read_fds = *master_fdset;
    int nfds = *fdmax;
    struct timeval timeout = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    int fd;
    bool check_fdmax = false;
    int accept_event_fd;
    eventfd_t value;
    int *newfd_ptr = NULL;

    CMSG_ASSERT_RETURN_VAL (info->server != NULL, CMSG_RET_ERR);

    accept_event_fd = info->accept_sd_eventfd;

    ret = select (nfds + 1, &read_fds, NULL, NULL, (timeout_ms < 0) ? NULL : &timeout);
    if (ret == -1)
    {
        if (errno == EINTR)
        {
            // We were interrupted, this is transient so just pretend we timed out.
            return CMSG_RET_OK;
        }

        CMSG_LOG_SERVER_ERROR (info->server,
                               "An error occurred with receive poll (timeout %dms): %s.",
                               timeout_ms, strerror (errno));
        CMSG_COUNTER_INC (info->server, cntr_poll_errors);
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
            if (fd == accept_event_fd)
            {
                /* clear notification */
                TEMP_FAILURE_RETRY (eventfd_read (info->accept_sd_eventfd, &value));
                while ((newfd_ptr = g_async_queue_try_pop (info->accept_sd_queue)))
                {
                    FD_SET (*newfd_ptr, master_fdset);
                    *fdmax = MAX (*newfd_ptr, *fdmax);
                    CMSG_FREE (newfd_ptr);
                }
            }
            else
            {
                // there is something happening on the socket so receive it.
                if (cmsg_server_receive (info->server, fd) < 0)
                {
                    // only close the socket if we have errored
                    cmsg_server_close_wrapper (info->server);
                    shutdown (fd, SHUT_RDWR);
                    close (fd);
                    FD_CLR (fd, master_fdset);
                    check_fdmax = true;
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
    bool check_fdmax = false;
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
                    check_fdmax = true;
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


static int32_t
cmsg_server_recv_process (uint8_t *buffer_data, cmsg_server *server,
                          uint32_t extra_header_size, uint32_t dyn_len,
                          int nbytes, cmsg_header *header_converted)
{
    cmsg_server_request server_request;
    int32_t ret;

    // Header is good so make use of it.
    server_request.msg_type = header_converted->msg_type;
    server_request.message_length = header_converted->message_length;
    server_request.method_index = UNDEFINED_METHOD;
    memset (&(server_request.method_name_recvd), 0, CMSG_SERVER_REQUEST_MAX_NAME_LENGTH);

    ret = cmsg_tlv_header_process (buffer_data, &server_request, extra_header_size,
                                   server->service->descriptor);

    if (ret != CMSG_RET_OK)
    {
        if (ret == CMSG_RET_METHOD_NOT_FOUND)
        {
            cmsg_server_empty_method_reply_send (server,
                                                 CMSG_STATUS_CODE_SERVER_METHOD_NOT_FOUND,
                                                 UNDEFINED_METHOD);
        }
    }
    else
    {

        buffer_data = buffer_data + extra_header_size;
        // Process any message that has no more length or we have received what
        // we expected to from the socket
        if (header_converted->message_length == 0 || nbytes == (int) dyn_len)
        {
            CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] received data\n");
            cmsg_buffer_print (buffer_data, dyn_len);
            server->server_request = &server_request;
            if (server->message_processor (server, buffer_data) != CMSG_RET_OK)
            {
                CMSG_LOG_SERVER_ERROR (server,
                                       "Server message processing returned an error.");
            }

        }
        else
        {
            CMSG_LOG_SERVER_ERROR (server, "No data on recv socket %d.",
                                   server->_transport->connection.sockets.client_socket);

            ret = CMSG_RET_ERR;
        }
    }

    return ret;
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
    uint8_t recv_buf_static[CMSG_RECV_BUFFER_SZ] = { 0 };
    uint8_t *recv_buff = recv_buf_static;
    cmsg_header processed_header;
    int nbytes = 0;
    uint32_t extra_header_size = 0;
    uint8_t *buffer_data;
    uint32_t dyn_len = 0;

    CMSG_ASSERT_RETURN_VAL (server != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (server->_transport != NULL, CMSG_RET_ERR);

    ret = server->_transport->tport_funcs.server_recv (socket, server->_transport,
                                                       &recv_buff, &processed_header,
                                                       &nbytes);

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

    if (nbytes > 0)
    {
        extra_header_size = processed_header.header_length - sizeof (cmsg_header);

        // packet size is determined by header_length + message_length.
        // header_length may be greater than sizeof (cmsg_header)
        dyn_len = processed_header.message_length + processed_header.header_length;

        buffer_data = recv_buff + sizeof (cmsg_header);

        ret = cmsg_server_recv_process (buffer_data, server, extra_header_size,
                                        dyn_len, nbytes, &processed_header);
    }

    if (recv_buff != recv_buf_static)
    {
        CMSG_FREE (recv_buff);
        recv_buff = NULL;
    }

    return ret;
}


/* Accept an incoming socket from a client */
int32_t
cmsg_server_accept (cmsg_server *server, int32_t listen_socket)
{
    int sock = -1;

    CMSG_ASSERT_RETURN_VAL (server != NULL, -1);

    if (server->_transport->tport_funcs.server_accept != NULL)
    {
        sock =
            server->_transport->tport_funcs.server_accept (listen_socket,
                                                           server->_transport);
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
        server->service->invoke (server->service, method_index, message,
                                 server->closure, (void *) &closure_data);

        if (!(server->app_owns_current_msg || server->app_owns_all_msgs))
        {
            protobuf_c_message_free_unpacked (message, &cmsg_memory_allocator);
        }
        server->app_owns_current_msg = false;

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
        server->closure (message, (void *) &closure_data);
        // count this as queued
        CMSG_COUNTER_INC (server, cntr_messages_queued);
        break;

    case CMSG_METHOD_DROPPED:
        // Send response, if required by the closure function
        server->closure (message, (void *) &closure_data);

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
    return server->_transport->tport_funcs.server_send (server->_transport,
                                                        buff, length, 0);
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
            cmsg_server_empty_method_reply_send (server, CMSG_STATUS_CODE_SERVICE_FAILED,
                                                 server_request->method_index);
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
            cmsg_server_empty_method_reply_send (server, CMSG_STATUS_CODE_SERVICE_FAILED,
                                                 server_request->method_index);
            return;
        }
        else if (ret > packed_size)
        {
            CMSG_LOG_SERVER_ERROR
                (server, "Overpacked message data. Packed %d of %d bytes.", ret,
                 packed_size);
            CMSG_COUNTER_INC (server, cntr_pack_errors);
            CMSG_FREE (buffer);
            cmsg_server_empty_method_reply_send (server, CMSG_STATUS_CODE_SERVICE_FAILED,
                                                 server_request->method_index);
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
            server->queue_in_process = true;

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
            server->queue_in_process = false;

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
    cmsg_bool_t queue_in_process = true;
    pthread_mutex_lock (&server->queueing_state_mutex);
    server->queue_process_number = num_to_process;
    pthread_mutex_unlock (&server->queueing_state_mutex);

    while (queue_in_process)
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
    cmsg_bool_t queue_in_process = true;
    pthread_mutex_lock (&server->queueing_state_mutex);
    server->queue_process_number = -1;
    pthread_mutex_unlock (&server->queueing_state_mutex);

    while (queue_in_process)
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

static void
cmsg_server_queue_filter_init (cmsg_server *server)
{
    pthread_mutex_lock (&server->queue_filter_mutex);
    cmsg_queue_filter_init (server->queue_filter_hash_table, server->service->descriptor);
    pthread_mutex_unlock (&server->queue_filter_mutex);
}

static cmsg_queue_filter_type
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
    cmsg_transport_ipfree_bind_enable (transport, true);

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
    server->app_owns_current_msg = true;
}

/**
 * @brief Allows the application to take ownership of all messages.
 * @brief This flag defaults to false but will never reset once it is set by
 * @bried the application.
 *
 * @warning Taking ownership also means the application is responsible for freeing all msgs
 * @warning  msgs received from this server.
 * @warning If you want CMSG to takeover ownership of new received messages, the application
 * @warning must call this function again with false. Note, the application will still be
 * @warning responsible for freeing any messages it received before resetting this flag.
 * @warning There is no way to change ownership of an existing message once the IMPL exits.
 *
 * @param server The server you are setting the flag in
 * @param app_is_owner Is the application the owner of all messages? true or false
 * @returns nothing
 */
void
cmsg_server_app_owns_all_msgs_set (cmsg_server *server, cmsg_bool_t app_is_owner)
{
    server->app_owns_all_msgs = app_is_owner;
}

/**
 * Close a server socket to a remote client.
 *
 * NOTE user applications should not call this routine directly
 *
 * @param server - the server that is closing the current client connection
 */
void
cmsg_server_close_wrapper (cmsg_server *server)
{
    server->_transport->tport_funcs.server_close (server->_transport);
}

/**
 * Blocks waiting on an accept call for any incoming connections. Once
 * the accept completes the new socket is passed back to the broadcast
 * client user to read from.
 */
static void *
cmsg_server_accept_thread (void *_info)
{
    cmsg_server_accept_thread_info *info = (cmsg_server_accept_thread_info *) _info;
    cmsg_server *server = info->server;
    int listen_socket = cmsg_server_get_socket (server);
    int newfd = -1;
    fd_set read_fds;
    int *newfd_ptr;

    FD_ZERO (&read_fds);
    FD_SET (listen_socket, &read_fds);

    while (1)
    {
        /* Explicitly set where the thread can be cancelled. This ensures no
         * data can be leaked if the thread is cancelled while accepting a
         * connection. */
        pthread_setcancelstate (PTHREAD_CANCEL_ENABLE, NULL);
        select (listen_socket + 1, &read_fds, NULL, NULL, NULL);
        pthread_setcancelstate (PTHREAD_CANCEL_DISABLE, NULL);

        newfd = cmsg_server_accept (server, listen_socket);
        if (newfd >= 0)
        {
            newfd_ptr = CMSG_CALLOC (1, sizeof (int));
            *newfd_ptr = newfd;
            g_async_queue_push (info->accept_sd_queue, newfd_ptr);
            TEMP_FAILURE_RETRY (eventfd_write (info->accept_sd_eventfd, 1));
        }
    }

    pthread_exit (NULL);
}

/**
 * When destroying the accept_sd_queue there may still be
 * accepted sockets on there. Simply close them to avoid
 * leaking the descriptors.
 */
static void
_clear_accept_sd_queue (gpointer data)
{
    close (GPOINTER_TO_INT (data));
}

/**
 * Start the server accept thread.
 *
 * @param server - The server to accept connections for.
 *
 * @return Pointer to a cmsg_server_accept_thread_info struct on success,
 *         NULL on failure.
 */
cmsg_server_accept_thread_info *
cmsg_server_accept_thread_init (cmsg_server *server)
{
    cmsg_server_accept_thread_info *info = NULL;

    info = CMSG_CALLOC (1, sizeof (cmsg_server_accept_thread_info));
    if (info == NULL)
    {
        return NULL;
    }

    info->accept_sd_eventfd = eventfd (0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (info->accept_sd_eventfd < 0)
    {
        return NULL;
    }

    info->accept_sd_queue = g_async_queue_new_full (_clear_accept_sd_queue);
    if (!info->accept_sd_queue)
    {
        close (info->accept_sd_eventfd);
        return NULL;
    }

    info->server = server;

    if (pthread_create (&info->server_accept_thread, NULL,
                        cmsg_server_accept_thread, info) != 0)
    {
        close (info->accept_sd_eventfd);
        g_async_queue_unref (info->accept_sd_queue);
        return NULL;
    }

    return info;
}

/**
 * Shutdown the server accept thread.
 *
 * @param info - The cmsg_server_accept_thread_info struct returned from
 *               the related cmsg_server_accept_thread_init call.
 */
void
cmsg_server_accept_thread_deinit (cmsg_server_accept_thread_info *info)
{
    if (info)
    {
        pthread_cancel (info->server_accept_thread);
        pthread_join (info->server_accept_thread, NULL);
        close (info->accept_sd_eventfd);
        g_async_queue_unref (info->accept_sd_queue);
        CMSG_FREE (info);
    }
}
