/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#include <sys/eventfd.h>
#include "cmsg_private.h"
#include "cmsg_server.h"
#include "cmsg_error.h"
#include "cmsg_transport.h"
#include "transport/cmsg_transport_private.h"
#include "service_listener/cmsg_sl_api_private.h"
#include "cmsg_protobuf-c.h"
#include "cmsg_ant_result.h"

#ifdef HAVE_COUNTERD
#include "cntrd_app_defines.h"
#include "cntrd_app_api.h"
#endif

/* This value controls how long a server waits to peek the header of a
 * CMSG packet in seconds. This value is kept small as there is no reason
 * outside of error conditions why peeking the header on a server should
 * take a long time. */
#define SERVER_RECV_HEADER_PEEK_TIMEOUT 10

static void cmsg_server_queue_filter_init (cmsg_server *server);

static cmsg_queue_filter_type cmsg_server_queue_filter_lookup (cmsg_server *server,
                                                               const char *method);

int32_t cmsg_server_counter_create (cmsg_server *server, char *app_name);

static void cmsg_server_empty_method_reply_send (int socket, cmsg_server *server,
                                                 cmsg_status_code status_code,
                                                 uint32_t method_index);

static int32_t cmsg_server_message_processor (int socket,
                                              cmsg_server_request *server_request,
                                              cmsg_server *server, uint8_t *buffer_data);


static ProtobufCClosure
cmsg_server_get_closure_func (cmsg_transport *transport)
{
    switch (transport->type)
    {
    case CMSG_TRANSPORT_RPC_TCP:
    case CMSG_TRANSPORT_LOOPBACK:
    case CMSG_TRANSPORT_RPC_UNIX:
        return cmsg_server_closure_rpc;

    case CMSG_TRANSPORT_ONEWAY_TCP:
    case CMSG_TRANSPORT_BROADCAST:
    case CMSG_TRANSPORT_ONEWAY_UNIX:
    case CMSG_TRANSPORT_FORWARDING:
        return cmsg_server_closure_oneway;
    }

    CMSG_LOG_GEN_ERROR ("Unsupported closure function for transport type");
    return NULL;
}

/**
 * Call the appropriate closure function for this service. This is the common code called
 * by the auto-generated send functions. The auto-generated functions are generated as
 * inline functions in the header file that only call this function. This allows type
 * checking, but reduces the compiled code size.
 */
void
cmsg_server_send_response (const ProtobufCMessage *send_msg, const void *service)
{
    cmsg_closure_func _closure = ((const cmsg_server_closure_info *) service)->closure;
    void *_closure_data = ((const cmsg_server_closure_info *) service)->closure_data;

    _closure (send_msg, _closure_data);
}

/*
 * This is an internal function which can be called from CMSG library.
 * Applications should use cmsg_server_new() instead.
 *
 * Create a new CMSG server (but without creating counters).
 */
cmsg_server *
cmsg_server_create (cmsg_transport *transport, const ProtobufCService *service)
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
        cmsg_transport_set_recv_peek_timeout (server->_transport,
                                              SERVER_RECV_HEADER_PEEK_TIMEOUT);

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

        if (transport->tport_funcs.listen)
        {
            ret = transport->tport_funcs.listen (server->_transport);
            if (ret < 0)
            {
                CMSG_FREE (server);
                server = NULL;
                return NULL;
            }
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
        server->suppress_errors = false;
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
cmsg_server_new (cmsg_transport *transport, const ProtobufCService *service)
{
    cmsg_server *server;
    server = cmsg_server_create (transport, service);

    if (server != NULL)
    {
        cmsg_service_listener_add_server (server);

#ifdef HAVE_COUNTERD
        char app_name[CNTRD_MAX_APP_NAME_LENGTH];

        /* initialise our counters */
        snprintf (app_name, CNTRD_MAX_APP_NAME_LENGTH, "%s%s%s",
                  CMSG_COUNTER_APP_NAME_PREFIX, service->descriptor->name,
                  cmsg_transport_counter_app_tport_id (transport));

        if (cmsg_server_counter_create (server, app_name) != CMSG_RET_OK)
        {
            CMSG_LOG_GEN_ERROR ("[%s] Unable to create server counters.", app_name);
        }
#endif
    }

    return server;
}


void
cmsg_server_destroy (cmsg_server *server)
{
    int fd;

    CMSG_ASSERT_RETURN_VOID (server != NULL);

    cmsg_service_listener_remove_server (server);

    // Close accepted sockets before destroying server
    for (fd = 0; fd <= server->accepted_fdmax; fd++)
    {
        if (FD_ISSET (fd, &server->accepted_fdset))
        {
            cmsg_server_close_accepted_socket (server, fd);
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

    if (server->accept_thread_info)
    {
        cmsg_server_accept_thread_deinit (server);
    }

    if (server->_transport && server->_transport->tport_funcs.socket_close)
    {
        server->_transport->tport_funcs.socket_close (server->_transport);
    }

    if (cmsg_server_crypto_enabled (server))
    {
        g_hash_table_remove_all (server->crypto_sa_hash_table);
        g_hash_table_unref (server->crypto_sa_hash_table);
        pthread_mutex_destroy (&server->crypto_sa_hash_table_mutex);
    }

    CMSG_FREE (server);
}

/**
 * Suppress log-level syslog to debug-level for the server.
 * @param server    CMSG server
 * @param enable    Enable/disable error-log suppression
 */
void
cmsg_server_suppress_error (cmsg_server *server, cmsg_bool_t enable)
{
    server->suppress_errors = enable;

    /* Apply to transport as well */
    if (server->_transport)
    {
        server->_transport->suppress_errors = enable;
    }
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

    socket = server->_transport->tport_funcs.get_socket (server->_transport);

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

    cmsg_server_accept_thread_init (server);

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
 * @param server - The CMSG server to poll.
 * @param timeout_ms - The timeout to use with select.
 *                     (0: return immediately, negative number: no timeout).
 * @param master_fdset - An fd_set containing all of the sockets being polled.
 * @param fdmax - Pointer to an integer holding the maximum fd number.
 *
 * @returns On success returns 0, failure returns -1.
 */
int32_t
cmsg_server_thread_receive_poll (cmsg_server *server, int32_t timeout_ms,
                                 fd_set *master_fdset, int *fdmax)
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
    cmsg_server_accept_thread_info *info;

    CMSG_ASSERT_RETURN_VAL (server != NULL, CMSG_RET_ERR);

    info = server->accept_thread_info;
    accept_event_fd = info->accept_sd_eventfd;

    /* Explicitly set where the thread can be cancelled to avoid leaking
     * connected sockets. */
    pthread_setcancelstate (PTHREAD_CANCEL_ENABLE, NULL);
    ret = select (nfds + 1, &read_fds, NULL, NULL, (timeout_ms < 0) ? NULL : &timeout);
    pthread_setcancelstate (PTHREAD_CANCEL_DISABLE, NULL);
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
                if (cmsg_server_receive (server, fd) < 0)
                {
                    // only close the socket if we have errored
                    cmsg_server_close_accepted_socket (server, fd);
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
    int fd;
    cmsg_server_accept_thread_info *info;
    int accept_event_fd;
    eventfd_t value;
    int *newfd_ptr = NULL;

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

        info = server->accept_thread_info;
        accept_event_fd = info->accept_sd_eventfd;

        FD_SET (accept_event_fd, &read_fds);
        fdmax = MAX (fdmax, accept_event_fd);

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
        info = server->accept_thread_info;
        accept_event_fd = info->accept_sd_eventfd;

        for (fd = 0; fd <= fdmax; fd++)
        {
            if (FD_ISSET (fd, &read_fds))
            {
                if (fd == accept_event_fd)
                {
                    /* clear notification */
                    TEMP_FAILURE_RETRY (eventfd_read (info->accept_sd_eventfd, &value));
                    while ((newfd_ptr = g_async_queue_try_pop (info->accept_sd_queue)))
                    {
                        FD_SET (*newfd_ptr, &server->accepted_fdset);
                        server->accepted_fdmax = MAX (server->accepted_fdmax, *newfd_ptr);
                        CMSG_FREE (newfd_ptr);
                    }
                }
                else if (FD_ISSET (fd, &server->accepted_fdset))
                {
                    // there is something happening on the socket so receive it.
                    if (cmsg_server_receive (server, fd) < 0)
                    {
                        // only close the socket if we have errored
                        cmsg_server_close_accepted_socket (server, fd);
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
cmsg_server_recv_process (int socket, uint8_t *buffer_data, cmsg_server *server,
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
            cmsg_server_empty_method_reply_send (socket, server,
                                                 CMSG_STATUS_CODE_SERVER_METHOD_NOT_FOUND,
                                                 UNDEFINED_METHOD);
            CMSG_COUNTER_INC (server, cntr_unknown_rpc);
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
            if (server->message_processor (socket, &server_request, server,
                                           buffer_data) != CMSG_RET_OK)
            {
                CMSG_LOG_SERVER_ERROR (server,
                                       "Server message processing returned an error.");
            }

        }
        else
        {
            CMSG_LOG_SERVER_ERROR (server, "No data on recv socket %d.", socket);

            ret = CMSG_RET_ERR;
        }
    }

    return ret;
}

/**
 * Helper function for 'cmsg_server_receive_encrypted'.
 * Receive the encrypted data, decrypt it and then return the decoded
 * data to the caller.
 *
 * @param sa - The crypto sa required to decrypt the received data.
 * @param socket - The socket to read the encrypted data off.
 * @param server - The server to receive on.
 * @param decoded_data - Pointer to the buffer to return the received data in.
 * @param decoded_bytes - Pointer to return the number of bytes of received data.
 *
 * @return CMSG_RET_OK on success, related error code on failure.
 */
static int32_t
_cmsg_server_receive_encrypted (cmsg_crypto_sa *sa, int socket, cmsg_server *server,
                                uint8_t **decoded_data, int *decoded_bytes)
{
    int32_t ret = CMSG_RET_OK;
    int recv_bytes = 0;
    uint8_t *buffer = NULL;
    uint8_t buf_static[512];
    uint32_t msg_length = 0;
    cmsg_transport *transport = server->_transport;
    uint8_t sec_header[8];
    cmsg_peek_code peek_status;
    time_t receive_timeout = transport->receive_peek_timeout;

    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] server->accecpted_client_socket %d\n", socket);

    peek_status = cmsg_transport_peek_for_header (transport->tport_funcs.recv_wrapper,
                                                  transport, socket, receive_timeout,
                                                  sec_header, sizeof (sec_header));
    if (peek_status != CMSG_PEEK_CODE_SUCCESS)
    {
        if (peek_status == CMSG_PEEK_CODE_CONNECTION_CLOSED ||
            peek_status == CMSG_PEEK_CODE_CONNECTION_RESET)
        {
            return CMSG_RET_CLOSED;
        }

        return CMSG_RET_ERR;
    }

    msg_length = cmsg_crypto_parse_header (sec_header);
    if (msg_length == -1)
    {
        CMSG_LOG_TRANSPORT_ERROR (transport, "Receive error. Invalid crypto header.");
        return CMSG_RET_ERR;
    }

    if (msg_length < sizeof (buf_static))
    {
        buffer = buf_static;
    }
    else
    {
        buffer = (uint8_t *) CMSG_CALLOC (1, msg_length);
        if (buffer == NULL)
        {
            CMSG_LOG_TRANSPORT_ERROR (transport,
                                      "Failed to allocate buffer memory (%d)", msg_length);
            return CMSG_RET_ERR;
        }
    }

    recv_bytes = transport->tport_funcs.recv_wrapper (transport, socket, buffer,
                                                      msg_length, MSG_WAITALL);
    if (recv_bytes == msg_length)
    {
        if (msg_length > CMSG_RECV_BUFFER_SZ)
        {
            *decoded_data = (uint8_t *) CMSG_CALLOC (1, msg_length);
            if (*decoded_data == NULL)
            {
                return CMSG_RET_ERR;
            }
        }

        *decoded_bytes = cmsg_crypto_decrypt (sa, buffer, msg_length, *decoded_data,
                                              server->crypto_sa_derive_func);
        ret = CMSG_RET_OK;
    }
    else
    {
        ret = CMSG_RET_ERR;
    }

    if (buffer != buf_static)
    {
        CMSG_FREE (buffer);
    }

    return ret;
}

/**
 * Receive the data and decrypt it.
 *
 * @param server - The server to receive on.
 * @param socket - The socket to read the encrypted data off.
 * @param decoded_data - Pointer to the buffer to return the received data in.
 * @param header_converted - Pointer to return the converted header in.
 * @param decoded_bytes - Pointer to return the number of decoded bytes received.
 *
 * @return CMSG_RET_OK on success, related error code on failure.
 */
static int32_t
cmsg_server_receive_encrypted (cmsg_server *server, int32_t socket, uint8_t **decoded_data,
                               cmsg_header *header_converted, int *decoded_bytes)
{
    int32_t ret = 0;
    cmsg_crypto_sa *sa = NULL;
    cmsg_header *header_received;

    pthread_mutex_lock (&server->crypto_sa_hash_table_mutex);
    sa = g_hash_table_lookup (server->crypto_sa_hash_table, GINT_TO_POINTER (socket));
    pthread_mutex_unlock (&server->crypto_sa_hash_table_mutex);

    if (sa == NULL)
    {
        CMSG_LOG_SERVER_ERROR (server, "Server failed to lookup sa on socket %d", socket);
        return CMSG_RET_ERR;
    }

    ret = _cmsg_server_receive_encrypted (sa, socket, server, decoded_data, decoded_bytes);
    if (*decoded_bytes >= (int) sizeof (cmsg_header))
    {
        header_received = (cmsg_header *) *decoded_data;

        if (cmsg_header_process (header_received, header_converted) != CMSG_RET_OK)
        {
            /* Couldn't process the header for some reason */
            CMSG_LOG_SERVER_ERROR (server,
                                   "Unable to process message header for server recv. Bytes: %d",
                                   *decoded_bytes);
            return CMSG_RET_ERR;
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

    if (cmsg_server_crypto_enabled (server))
    {
        ret = cmsg_server_receive_encrypted (server, socket, &recv_buff, &processed_header,
                                             &nbytes);
    }
    else
    {
        ret = server->_transport->tport_funcs.server_recv (socket, server->_transport,
                                                           &recv_buff, &processed_header,
                                                           &nbytes);
    }

    if (ret < 0)
    {
        CMSG_DEBUG (CMSG_INFO,
                    "[SERVER] server receive failed, server %s transport type %d socket %d ret %d\n",
                    server->service->descriptor->name, server->_transport->type, socket,
                    ret);

        /* Do not count as an error if the peer has performed an orderly shutdown */
        if (ret != CMSG_RET_CLOSED)
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

        ret = cmsg_server_recv_process (socket, buffer_data, server, extra_header_size,
                                        dyn_len, nbytes, &processed_header);
    }

    if (recv_buff != recv_buf_static)
    {
        CMSG_FREE (recv_buff);
        recv_buff = NULL;
    }

    return ret;
}


/**
 * Accept an incoming connection from a client.
 *
 * @param server - The server to accept the connection on.
 * @param listen_socket - The listening socket.
 *
 * @returns The accepted socket descriptor on success, -1 otherwise.
 */
static int32_t
cmsg_server_accept (cmsg_server *server, int32_t listen_socket)
{
    int sock = -1;
    cmsg_crypto_sa *sa = NULL;
    struct sockaddr_storage addr;
    socklen_t len = sizeof (addr);

    CMSG_ASSERT_RETURN_VAL (server != NULL, -1);

    sock = cmsg_transport_accept (server->_transport);

    if (cmsg_server_crypto_enabled (server))
    {
        getpeername (sock, (struct sockaddr *) &addr, &len);
        sa = server->crypto_sa_create_func (&addr);
        if (sa == NULL)
        {
            CMSG_LOG_SERVER_ERROR (server, "No crypto sa for accepted socket %d.", sock);
            close (sock);
            return -1;
        }

        pthread_mutex_lock (&server->crypto_sa_hash_table_mutex);
        g_hash_table_insert (server->crypto_sa_hash_table, GINT_TO_POINTER (sock), sa);
        pthread_mutex_unlock (&server->crypto_sa_hash_table_mutex);
    }

    // count the accepted connection
    CMSG_COUNTER_INC (server, cntr_connections_accepted);

    return sock;
}

/**
 * Call validation function for message and send a response if it is invalid
 * @param input message to validate
 * @param output_desc descriptor of response message to use if validation fails
 * @param closure_info information needed to send response
 * @param validation_func validation function to call
 * @returns true if validation passed, else false.
 */
static bool
cmsg_service_input_valid (const ProtobufCMessage *input,
                          const ProtobufCMessageDescriptor *output_desc,
                          cmsg_server_closure_info *closure_info,
                          cmsg_validation_func validation_func)
{
    char err_str[512];

    if (!validation_func (input, err_str, sizeof (err_str)))
    {
        ProtobufCMessage *response =
            cmsg_create_ant_response (err_str, ANT_CODE_INVALID_ARGUMENT, output_desc);
        cmsg_server_send_response (response, closure_info);
        CMSG_FREE_RECV_MSG (response);
        return false;
    }

    return true;
}

typedef void (*cmsg_impl_func) (cmsg_server_closure_info *closure_info,
                                const ProtobufCMessage *input);
typedef void (*cmsg_impl_no_input_func) (cmsg_server_closure_info *closure_info);

/**
 * Replacement for protobuf_c_service_invoke_internal. Invokes the CMSG impl function
 * based on the service information.
 * @param service ProtobufCService pointer (cast to cmsg_service to get impl_info pointer)
 * @param method_index index of method to invoke
 * @param input input message
 * @param closure closure function
 * @param closure_data data for closure function
 */
void
cmsg_server_invoke_impl (ProtobufCService *service, unsigned method_index,
                         const ProtobufCMessage *input,
                         ProtobufCClosure closure, void *closure_data)
{
    const cmsg_impl_info *impl_info = ((cmsg_service *) service)->impl_info;
    const cmsg_method_server_extensions *method_extensions;
    cmsg_server_closure_info closure_info;

    // This may not be possible (there will always be dummy input)
    if (input == NULL)
    {
        closure (NULL, closure_data);
        return;
    }

    // these are needed in 'Send' function for sending reply back to the client
    closure_info.closure = closure;
    closure_info.closure_data = closure_data;

    /*
     * Verify that method_index is within range. If this fails, you are
     * likely invoking a newly added method on an old service. (Although
     * other memory corruption bugs can cause this assertion too.)
     */
    assert (method_index < service->descriptor->n_methods);

    method_extensions = impl_info[method_index].method_extensions;
    if (method_extensions)
    {
        if (method_extensions->validation_func)
        {
            if (!cmsg_service_input_valid (input,
                                           service->descriptor->
                                           methods[method_index].output, &closure_info,
                                           method_extensions->validation_func))
            {
                /* response handled as part of validation */
                return;
            }
        }
    }

    if (!impl_info[method_index].impl_func)
    {
        /* impl_func is set to NULL for file response APIs, but shouldn't get to here */
        return;
    }

    if (service->descriptor->methods[method_index].input->n_fields == 0)
    {
        cmsg_impl_no_input_func func =
            (cmsg_impl_no_input_func) impl_info[method_index].impl_func;
        func (&closure_info);
    }
    else
    {
        cmsg_impl_func func = (cmsg_impl_func) impl_info[method_index].impl_func;
        func (&closure_info, input);
    }
}

void
cmsg_server_invoke (int socket, cmsg_server_request *server_request, cmsg_server *server,
                    ProtobufCMessage *message, cmsg_method_processing_reason process_reason)
{
    uint32_t queue_length = 0;
    cmsg_server_closure_data closure_data;
    uint32_t method_index = server_request->method_index;

    CMSG_ASSERT_RETURN_VOID (server != NULL);

    // Setup closure_data so it can be used no matter what the action is
    closure_data.server = server;
    closure_data.server_request = server_request;
    closure_data.reply_socket = socket;
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
        server->service->invoke ((ProtobufCService *) server->service,
                                 method_index, message, server->closure,
                                 (void *) &closure_data);

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
    const ProtobufCService *service = server->service;
    const char *method_name;
    int socket = -1;    /* When invoking the server directly the data is not sent
                         * back across a socket. */

    method_name = service->descriptor->methods[method_index].name;

    /* setup the server request, which is needed to get a response sent back */
    server_request.msg_type = CMSG_MSG_TYPE_METHOD_REQ;
    server_request.message_length = protobuf_c_message_get_packed_size (input);
    server_request.method_index = method_index;
    strcpy (server_request.method_name_recvd, method_name);

    /* call the server invoke function. */
    cmsg_server_invoke (socket, &server_request, server,
                        (ProtobufCMessage *) input, CMSG_METHOD_OK_TO_INVOKE);
}


/**
 * Process a METHOD_REQ message.
 *
 * Unpack the parameters, perform filtering (if applicable) and invoke the method.
 *
 * @returns -1 on failure, 0 on success
 */
static int32_t
_cmsg_server_method_req_message_processor (int socket, cmsg_server_request *server_request,
                                           cmsg_server *server, uint8_t *buffer_data)
{
    cmsg_queue_filter_type action;
    cmsg_method_processing_reason processing_reason = CMSG_METHOD_OK_TO_INVOKE;
    ProtobufCMessage *message = NULL;
    ProtobufCAllocator *allocator = &cmsg_memory_allocator;
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
        CMSG_DEBUG (CMSG_INFO, "[SERVER] processing message with data\n");
        CMSG_DEBUG (CMSG_INFO, "[SERVER] unpacking message\n");

        //unpack the message
        message = protobuf_c_message_unpack (desc, allocator,
                                             server_request->message_length, buffer_data);
    }
    else
    {
        CMSG_DEBUG (CMSG_INFO, "[SERVER] processing message without data\n");
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

    cmsg_server_invoke (socket, server_request, server, message, processing_reason);

    CMSG_DEBUG (CMSG_INFO, "[SERVER] end of message processor\n");

    return CMSG_RET_OK;
}

/**
 * Wrap the sending of a buffer so that the input buffer can be encrypted if required
 *
 * @param server - The server sending data.
 * @param socket - The socket connection to send the data on.
 * @param buff - The data to send.
 * @param length - The length of the data to send.
 *
 * @returns The number of bytes sent if successful, -1 on failure.
 */
static int
cmsg_server_send_wrapper (cmsg_server *server, int socket, void *buff, int length)
{
    int ret = -1;
    cmsg_transport *transport = server->_transport;
    uint8_t *encrypt_buffer;
    int encrypt_length;
    cmsg_crypto_sa *sa = NULL;

    if (cmsg_server_crypto_enabled (server))
    {
        pthread_mutex_lock (&server->crypto_sa_hash_table_mutex);
        sa = g_hash_table_lookup (server->crypto_sa_hash_table, GINT_TO_POINTER (socket));
        pthread_mutex_unlock (&server->crypto_sa_hash_table_mutex);

        if (sa == NULL)
        {
            CMSG_LOG_SERVER_ERROR (server, "Server failed to lookup sa on socket %d",
                                   socket);
            return -1;
        }

        encrypt_buffer = (uint8_t *) CMSG_CALLOC (1, length + ENCRYPT_EXTRA);
        if (encrypt_buffer == NULL)
        {
            CMSG_LOG_SERVER_ERROR (server, "Server failed to allocate buffer on socket %d",
                                   socket);
            return -1;
        }

        encrypt_length = cmsg_crypto_encrypt (sa, buff, length, encrypt_buffer,
                                              length + ENCRYPT_EXTRA);
        if (encrypt_length < 0)
        {
            CMSG_LOG_SERVER_ERROR (server, "Server encrypt on socket %d failed", socket);
            CMSG_FREE (encrypt_buffer);
            return -1;
        }

        ret = transport->tport_funcs.server_send (socket, transport, encrypt_buffer,
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
        ret = transport->tport_funcs.server_send (socket, transport, buff, length, 0);
    }

    return ret;
}

/**
 * Process ECHO_REQ message
 *
 * We reply straight away to an ECHO_REQ
 */
static int32_t
_cmsg_server_echo_req_message_processor (int socket, cmsg_server *server,
                                         uint8_t *buffer_data)
{
    int ret = 0;
    cmsg_header header;

    header = cmsg_header_create (CMSG_MSG_TYPE_ECHO_REPLY, 0, 0 /* empty msg */ ,
                                 CMSG_STATUS_CODE_SUCCESS);

    CMSG_DEBUG (CMSG_INFO, "[SERVER] ECHO Reply header\n");

    cmsg_buffer_print ((void *) &header, sizeof (header));

    ret = cmsg_server_send_wrapper (server, socket, &header, sizeof (header));
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
 */
static int32_t
cmsg_server_message_processor (int socket, cmsg_server_request *server_request,
                               cmsg_server *server, uint8_t *buffer_data)
{
    CMSG_ASSERT_RETURN_VAL (server != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (buffer_data != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (server_request != NULL, CMSG_RET_ERR);

    // Check that the msg received is a type we support
    switch (server_request->msg_type)
    {
    case CMSG_MSG_TYPE_METHOD_REQ:
        return _cmsg_server_method_req_message_processor (socket, server_request,
                                                          server, buffer_data);
        break;

    case CMSG_MSG_TYPE_ECHO_REQ:
        return _cmsg_server_echo_req_message_processor (socket, server, buffer_data);
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


static void
cmsg_server_empty_method_reply_send (int socket, cmsg_server *server,
                                     cmsg_status_code status_code, uint32_t method_index)
{
    int ret = 0;
    cmsg_header header;

    CMSG_ASSERT_RETURN_VOID (server != NULL);

    header = cmsg_header_create (CMSG_MSG_TYPE_METHOD_REPLY, 0, 0 /* empty msg */ ,
                                 status_code);

    CMSG_DEBUG (CMSG_INFO, "[SERVER] response header\n");

    cmsg_buffer_print ((void *) &header, sizeof (header));

    ret = cmsg_server_send_wrapper (server, socket, &header, sizeof (header));
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

    CMSG_ASSERT_RETURN_VOID (closure_data != NULL);
    CMSG_ASSERT_RETURN_VOID (closure_data->server != NULL);
    CMSG_ASSERT_RETURN_VOID (closure_data->server->_transport != NULL);
    CMSG_ASSERT_RETURN_VOID (closure_data->server_request != NULL);

    cmsg_server *server = closure_data->server;
    cmsg_server_request *server_request = closure_data->server_request;
    uint32_t ret = 0;
    int send_ret = 0;
    int type = CMSG_TLV_METHOD_TYPE;
    int socket = closure_data->reply_socket;

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

        cmsg_server_empty_method_reply_send (socket, server,
                                             CMSG_STATUS_CODE_SERVICE_QUEUED,
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

        cmsg_server_empty_method_reply_send (socket, server,
                                             CMSG_STATUS_CODE_SERVICE_DROPPED,
                                             server_request->method_index);
        return;
    }
    /* No response message was specified, therefore reply with an error
     */
    else if (!message)
    {
        CMSG_DEBUG (CMSG_INFO, "[SERVER] sending response without data\n");

        cmsg_server_empty_method_reply_send (socket, server,
                                             CMSG_STATUS_CODE_SERVICE_FAILED,
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
            cmsg_server_empty_method_reply_send (socket, server,
                                                 CMSG_STATUS_CODE_SERVICE_FAILED,
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
            cmsg_server_empty_method_reply_send (socket, server,
                                                 CMSG_STATUS_CODE_SERVICE_FAILED,
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
            cmsg_server_empty_method_reply_send (socket, server,
                                                 CMSG_STATUS_CODE_SERVICE_FAILED,
                                                 server_request->method_index);
            return;
        }

        CMSG_DEBUG (CMSG_INFO, "[SERVER] response header\n");
        cmsg_buffer_print ((void *) &header, sizeof (header));

        CMSG_DEBUG (CMSG_INFO, "[SERVER] response data\n");
        cmsg_buffer_print ((void *) buffer_data, packed_size);

        send_ret = cmsg_server_send_wrapper (server, socket, buffer, total_message_size);

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

int32_t
cmsg_server_queue_process_all (cmsg_server *server)
{
    int processed = 0;

    pthread_mutex_lock (&server->queueing_state_mutex);
    processed = cmsg_receive_queue_process_all (server->queue, &server->queue_mutex,
                                                server);
    pthread_mutex_unlock (&server->queueing_state_mutex);

    return processed;
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

/**
 * Create a TIPC broadcast server.
 *
 * @param descriptor - The service descriptor for the server.
 * @param service_name - The service name in the /etc/services file to get
 *                       the port number.
 * @param id - The TIPC node id for the server.
 *
 * @returns Pointer to the server on success, NULL on failure.
 */
cmsg_server *
cmsg_create_server_tipc_broadcast (ProtobufCService *descriptor, const char *service_name,
                                   int id)
{
    cmsg_transport *transport;
    cmsg_server *server;
    uint16_t port;

    CMSG_ASSERT_RETURN_VAL (descriptor != NULL, NULL);

    port = cmsg_service_port_get (service_name, "tipc");
    if (port == 0)
    {
        CMSG_LOG_GEN_ERROR ("Unknown TIPC broadcast service: %s", service_name);
        return NULL;
    }

    transport = cmsg_transport_new (CMSG_TRANSPORT_BROADCAST);
    if (transport == NULL)
    {
        return NULL;
    }

    transport->config.socket.sockaddr.tipc.addrtype = TIPC_ADDR_NAMESEQ;
    transport->config.socket.sockaddr.tipc.scope = TIPC_CLUSTER_SCOPE;
    transport->config.socket.sockaddr.tipc.addr.nameseq.type = port;
    transport->config.socket.sockaddr.tipc.addr.nameseq.lower = id;
    transport->config.socket.sockaddr.tipc.addr.nameseq.upper = id;

    server = cmsg_server_new (transport, descriptor);
    if (server == NULL)
    {
        cmsg_transport_destroy (transport);
        CMSG_LOG_GEN_ERROR ("[%s] Failed to create TIPC broadcast server.",
                            descriptor->descriptor->name);
        return NULL;
    }

    return server;
}

/**
 * Helper function for creating a CMSG server using TCP over IPv4.
 *
 * @param service_name - The service name in the /etc/services file to get
 *                       the port number.
 * @param addr - The IPv4 address to listen on (in network byte order).
 * @param vrf_bind_dev - For VRF support, the device to bind to the socket (NULL if not relevant)
 * @param service - The CMSG service.
 * @param oneway - Whether to make a one-way server, or a two-way (RPC) server.
 */
static cmsg_server *
_cmsg_create_server_tcp_ipv4 (const char *service_name, struct in_addr *addr,
                              const char *vrf_bind_dev,
                              const ProtobufCService *service, bool oneway)
{
    cmsg_transport *transport;
    cmsg_server *server;

    transport = cmsg_create_transport_tcp_ipv4 (service_name, addr, vrf_bind_dev, oneway);
    if (!transport)
    {
        return NULL;
    }

    server = cmsg_server_new (transport, service);
    if (!server)
    {
        cmsg_transport_destroy (transport);
        CMSG_LOG_GEN_ERROR ("No TCP IPC server on %s", service->descriptor->name);
        return NULL;
    }

    return server;
}

/**
 * Create a RPC (two-way) CMSG server using TCP over IPv4.
 *
 * @param service_name - The service name in the /etc/services file to get
 *                       the port number.
 * @param addr - The IPv4 address to listen on (in network byte order).
 * @param vrf_bind_dev - For VRF support, the device to bind to the socket (NULL if not relevant)
 * @param service - The CMSG service.
 */
cmsg_server *
cmsg_create_server_tcp_ipv4_rpc (const char *service_name, struct in_addr *addr,
                                 const char *vrf_bind_dev, const ProtobufCService *service)
{
    CMSG_ASSERT_RETURN_VAL (service_name != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (addr != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (service != NULL, NULL);

    return _cmsg_create_server_tcp_ipv4 (service_name, addr, vrf_bind_dev, service, false);
}

/**
 * Create a oneway CMSG server using TCP over IPv4.
 *
 * @param service_name - The service name in the /etc/services file to get
 *                       the port number.
 * @param addr - The IPv4 address to listen on.
 * @param vrf_bind_dev - For VRF support, the device to bind to the socket (NULL if not relevant)
 * @param service - The CMSG service.
 */
cmsg_server *
cmsg_create_server_tcp_ipv4_oneway (const char *service_name, struct in_addr *addr,
                                    const char *vrf_bind_dev,
                                    const ProtobufCService *service)
{
    CMSG_ASSERT_RETURN_VAL (service_name != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (addr != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (service != NULL, NULL);

    return _cmsg_create_server_tcp_ipv4 (service_name, addr, vrf_bind_dev, service, true);
}

/**
 * Helper function for creating a CMSG server using TCP over IPv6.
 *
 * @param service_name - The service name in the /etc/services file to get
 *                       the port number.
 * @param addr - The IPv4 address to listen on (in network byte order).
 * @param scope_id - The scope id if a link local address is used, zero otherwise
 * @param vrf_bind_dev - For VRF support, the device to bind to the socket (NULL if not relevant)
 * @param service - The CMSG service.
 * @param oneway - Whether to make a one-way server, or a two-way (RPC) server.
 */
static cmsg_server *
_cmsg_create_server_tcp_ipv6 (const char *service_name, struct in6_addr *addr,
                              uint32_t scope_id, const char *vrf_bind_dev,
                              const ProtobufCService *service, bool oneway)
{
    cmsg_transport *transport;
    cmsg_server *server;

    transport = cmsg_create_transport_tcp_ipv6 (service_name, addr, scope_id, vrf_bind_dev,
                                                oneway);
    if (!transport)
    {
        return NULL;
    }

    server = cmsg_server_new (transport, service);
    if (!server)
    {
        cmsg_transport_destroy (transport);
        return NULL;
    }

    return server;
}

/**
 * Create a RPC (two-way) CMSG server using TCP over IPv4.
 *
 * @param service_name - The service name in the /etc/services file to get
 *                       the port number.
 * @param addr - The IPv4 address to listen on (in network byte order).
 * @param scope_id - The scope id if a link local address is used, zero otherwise
 * @param vrf_bind_dev - For VRF support, the device to bind to the socket (NULL if not relevant)
 * @param service - The CMSG service.
 */
cmsg_server *
cmsg_create_server_tcp_ipv6_rpc (const char *service_name, struct in6_addr *addr,
                                 uint32_t scope_id, const char *vrf_bind_dev,
                                 const ProtobufCService *service)
{
    CMSG_ASSERT_RETURN_VAL (service_name != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (addr != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (service != NULL, NULL);

    return _cmsg_create_server_tcp_ipv6 (service_name, addr, scope_id, vrf_bind_dev,
                                         service, false);
}


/**
 * Create a oneway CMSG server using TCP over IPv6.
 *
 * @param service_name - The service name in the /etc/services file to get
 *                       the port number.
 * @param addr - The IPv6 address to listen on.
 * @param scope_id - The scope id if a link local address is used, zero otherwise
 * @param vrf_bind_dev - For VRF support, the device to bind to the socket (NULL if not relevant)
 * @param service - The CMSG service.
 */
cmsg_server *
cmsg_create_server_tcp_ipv6_oneway (const char *service_name, struct in6_addr *addr,
                                    uint32_t scope_id, const char *vrf_bind_dev,
                                    const ProtobufCService *service)
{
    CMSG_ASSERT_RETURN_VAL (service_name != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (addr != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (service != NULL, NULL);

    return _cmsg_create_server_tcp_ipv6 (service_name, addr, scope_id, vrf_bind_dev,
                                         service, true);
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

        if (transport->type == CMSG_TRANSPORT_RPC_UNIX ||
            transport->type == CMSG_TRANSPORT_ONEWAY_UNIX)
        {
            unlink (transport->config.socket.sockaddr.un.sun_path);
        }

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
 * Blocks waiting on an accept call for any incoming connections. Once
 * the accept completes the new socket is passed back to the broadcast
 * client user to read from.
 */
static void *
cmsg_server_accept_thread (void *_server)
{
    cmsg_server *server = (cmsg_server *) _server;
    cmsg_server_accept_thread_info *info = server->accept_thread_info;
    int listen_socket = cmsg_server_get_socket (server);
    int newfd = -1;
    int *newfd_ptr;
    struct pollfd pfd = {
        .events = POLLIN,
        .fd = listen_socket,
    };

    while (1)
    {
        if (TEMP_FAILURE_RETRY (poll (&pfd, 1, -1)) > 0)
        {
            pthread_setcancelstate (PTHREAD_CANCEL_DISABLE, NULL);
            newfd = cmsg_server_accept (server, listen_socket);
            if (newfd >= 0)
            {
                /* Explicitly set where the thread can be cancelled. This ensures no
                 * sockets can be leaked if the thread is cancelled after accepting
                 * a connection. */
                newfd_ptr = CMSG_CALLOC (1, sizeof (int));
                *newfd_ptr = newfd;
                g_async_queue_push (info->accept_sd_queue, newfd_ptr);
                TEMP_FAILURE_RETRY (eventfd_write (info->accept_sd_eventfd, 1));
            }
            pthread_setcancelstate (PTHREAD_CANCEL_ENABLE, NULL);
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
    int *fd_ptr = (int *) data;
    close (*fd_ptr);
    CMSG_FREE (data);
}

/**
 * Start the server accept thread.
 *
 * @param server - The server to accept connections for.
 *
 * @return CMSG_RET_OK on success, CMSG_RET_ERR on failure.
 */
int32_t
cmsg_server_accept_thread_init (cmsg_server *server)
{
    cmsg_server_accept_thread_info *info = NULL;

    if (server->accept_thread_info)
    {
        /* Already initialised */
        return CMSG_RET_OK;
    }

    info = CMSG_CALLOC (1, sizeof (cmsg_server_accept_thread_info));
    if (info == NULL)
    {
        return CMSG_RET_ERR;
    }

    info->accept_sd_eventfd = eventfd (0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (info->accept_sd_eventfd < 0)
    {
        CMSG_FREE (info);
        return CMSG_RET_ERR;
    }

    info->accept_sd_queue = g_async_queue_new_full (_clear_accept_sd_queue);
    if (!info->accept_sd_queue)
    {
        close (info->accept_sd_eventfd);
        CMSG_FREE (info);
        return CMSG_RET_ERR;
    }

    server->accept_thread_info = info;

    if (pthread_create (&info->server_accept_thread, NULL,
                        cmsg_server_accept_thread, server) != 0)
    {
        close (info->accept_sd_eventfd);
        g_async_queue_unref (info->accept_sd_queue);
        CMSG_FREE (info);
        server->accept_thread_info = NULL;
        return CMSG_RET_ERR;
    }

    cmsg_pthread_setname (info->server_accept_thread,
                          server->service->descriptor->short_name, CMSG_ACCEPT_PREFIX);
    return CMSG_RET_OK;
}

/**
 * Shutdown the server accept thread.
 *
 * @param server - The CMSG server to shutdown the accept thread for.
 */
void
cmsg_server_accept_thread_deinit (cmsg_server *server)
{
    if (server && server->accept_thread_info)
    {
        pthread_cancel (server->accept_thread_info->server_accept_thread);
        pthread_join (server->accept_thread_info->server_accept_thread, NULL);
        close (server->accept_thread_info->accept_sd_eventfd);
        g_async_queue_unref (server->accept_thread_info->accept_sd_queue);
        CMSG_FREE (server->accept_thread_info);
        server->accept_thread_info = NULL;
    }
}

/**
 * Create a 'cmsg_service_info' message for the given CMSG server.
 * This message should be freed using 'cmsg_server_service_info_free'.
 *
 * @param server - The server to build the message for.
 *
 * @returns A pointer to the message on success, NULL on failure.
 */
cmsg_service_info *
cmsg_server_service_info_create (cmsg_server *server)
{
    cmsg_service_info *info = NULL;
    cmsg_transport_info *transport_info = NULL;
    char *service_str = NULL;

    service_str = CMSG_STRDUP (cmsg_service_name_get (server->service->descriptor));
    if (!service_str)
    {
        return NULL;
    }

    transport_info = cmsg_transport_info_create (server->_transport);
    if (!transport_info)
    {
        CMSG_FREE (service_str);
        return NULL;
    }

    info = CMSG_MALLOC (sizeof (cmsg_service_info));
    if (!info)
    {
        CMSG_FREE (service_str);
        cmsg_transport_info_free (transport_info);
        return NULL;
    }

    cmsg_service_info_init (info);
    CMSG_SET_FIELD_PTR (info, server_info, transport_info);
    CMSG_SET_FIELD_PTR (info, service, service_str);
    CMSG_SET_FIELD_VALUE (info, pid, getpid ());

    return info;
}

/**
 * Free a 'cmsg_service_info' message created by a call to
 * 'cmsg_server_service_info_create'.
 *
 * @param info - The message to free.
 */
void
cmsg_server_service_info_free (cmsg_service_info *info)
{
    CMSG_FREE (info->service);
    cmsg_transport_info_free (info->server_info);
    CMSG_FREE (info);
}

/**
 * Helper function that can be used to get the server processing
 * the request from the 'service' parameter of any given IMPL function.
 *
 * @param service - The 'service' pointer passed to the IMPL function.
 *
 * @returns The pointer to the server processing the request.
 */
const cmsg_server *
cmsg_server_from_service_get (const void *service)
{
    const cmsg_server_closure_info *closure_info = service;
    const cmsg_server_closure_data *closure_data;
    const cmsg_server *server = NULL;

    if (closure_info)
    {
        closure_data = closure_info->closure_data;
        server = closure_data->server;
    }

    return server;
}

/**
 * Create a 'cmsg_server_thread_task_info' structure containing
 * the details required to run a server as a task in some thread.
 *
 * @param server - The server to run via the task in a thread.
 * @param timeout - The timeout, in milliseconds, to use when polling the server.
 *                  Note:
 *                  - If '-1' is used then the server will block until
 *                    there is data to read. This makes it difficult
 *                    to stop the task thread.
 *                  - If '0' is used then the server will busy poll.
 *
 * @returns The 'cmsg_server_thread_task_info' on success, NULL otherwise.
 */
cmsg_server_thread_task_info *
cmsg_server_thread_task_info_create (cmsg_server *server, int timeout)
{
    cmsg_server_thread_task_info *info;

    info = (cmsg_server_thread_task_info *) CMSG_CALLOC (1, sizeof (*info));
    if (info)
    {
        info->server = server;
        info->timeout = timeout;
        info->running = true;
    }

    return info;
}

/**
 * A generic task than can be used to run a server in a thread of
 * some given form.
 *
 * @param info - The details required to run the server. This should
 *               have been allocated by a call to 'cmsg_server_thread_task_info_create'.
 *               To stop the server/task the 'running' field inside the structure
 *               should be set to 'false'. Note that once set to 'false' the structure
 *               and server should no longer be accessed as the task will steal the
 *               memory for them and free on exit.
 */
void *
cmsg_server_thread_task (void *_info)
{
    int fd;
    int fd_max;
    fd_set readfds;
    int accept_sd_eventfd;
    cmsg_server_thread_task_info *info = (cmsg_server_thread_task_info *) _info;

    FD_ZERO (&readfds);

    cmsg_server_accept_thread_init (info->server);

    accept_sd_eventfd = info->server->accept_thread_info->accept_sd_eventfd;
    fd_max = accept_sd_eventfd;
    FD_SET (fd_max, &readfds);

    while (info->running)
    {
        cmsg_server_thread_receive_poll (info->server, info->timeout, &readfds, &fd_max);
    }

    cmsg_server_accept_thread_deinit (info->server);

    for (fd = 0; fd <= fd_max; fd++)
    {
        /* Don't double close the accept event fd */
        if (fd == accept_sd_eventfd)
        {
            continue;
        }
        if (FD_ISSET (fd, &readfds))
        {
            cmsg_server_close_accepted_socket (info->server, fd);
        }
    }

    cmsg_destroy_server_and_transport (info->server);
    CMSG_FREE (info);

    return NULL;
}

/**
 * Enable encryption for the connections to this server.
 *
 * @param server - The server to accept connections for.
 * @param create_func - The user supplied callback function to create a crypto sa
 *                      when the server accepts a connection.
 * @param derive_func - The user supplied callback function to derive the crypto sa
 *                      once the nonce is received from the client.
 *
 * @return CMSG_RET_OK on success, CMSG_RET_ERR on failure.
 */
int32_t
cmsg_server_crypto_enable (cmsg_server *server, crypto_sa_create_func_t create_func,
                           crypto_sa_derive_func_t derive_func)
{
    if (create_func == NULL || derive_func == NULL)
    {
        return CMSG_RET_ERR;
    }

    if (server->crypto_sa_hash_table)
    {
        /* Already initialised */
        return CMSG_RET_OK;
    }

    if (pthread_mutex_init (&server->crypto_sa_hash_table_mutex, NULL) != 0)
    {
        return CMSG_RET_ERR;
    }

    server->crypto_sa_create_func = create_func;
    server->crypto_sa_derive_func = derive_func;

    server->crypto_sa_hash_table =
        g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
                               (GDestroyNotify) cmsg_crypto_sa_free);
    if (server->crypto_sa_hash_table == NULL)
    {
        server->crypto_sa_create_func = NULL;
        server->crypto_sa_derive_func = NULL;
        pthread_mutex_destroy (&server->crypto_sa_hash_table_mutex);
        return CMSG_RET_ERR;
    }

    return CMSG_RET_OK;
}

/**
 * Is encrypted connections enabled for this server.
 *
 * @param server - The server to check.
 *
 * @returns true if enabled, false otherwise.
 */
bool
cmsg_server_crypto_enabled (cmsg_server *server)
{
    return (server->crypto_sa_hash_table != NULL);
}

/**
 * Close an accepted socket connection on the server.
 *
 * @param server - The server to close the connection for.
 * @param socket-  The socket connection to close.
 */
void
cmsg_server_close_accepted_socket (cmsg_server *server, int socket)
{
    if (cmsg_server_crypto_enabled (server))
    {
        pthread_mutex_lock (&server->crypto_sa_hash_table_mutex);
        g_hash_table_remove (server->crypto_sa_hash_table, GINT_TO_POINTER (socket));
        pthread_mutex_unlock (&server->crypto_sa_hash_table_mutex);
    }

    shutdown (socket, SHUT_RDWR);
    close (socket);
}

/**
 * Creates a forwarding server. This should be used to process the data
 * sent by a forwarding client.
 *
 * @param descriptor - The service for the server.
 *
 * @returns A pointer to the server on success, NULL otherwise.
 */
cmsg_server *
cmsg_create_server_forwarding (const ProtobufCService *service)
{
    cmsg_transport *transport = NULL;
    cmsg_server *server = NULL;

    transport = cmsg_transport_new (CMSG_TRANSPORT_FORWARDING);
    if (transport == NULL)
    {
        CMSG_LOG_GEN_ERROR ("Could not create transport for forwarding server\n");
        return NULL;
    }

    server = cmsg_server_new (transport, service);
    if (server == NULL)
    {
        cmsg_transport_destroy (transport);
        syslog (LOG_ERR, "Could not create forwarding server");
        return NULL;
    }

    return server;
}

/**
 * Process the received message for the forwarding server.
 *
 * @param server - The forwarding server.
 * @param data - The received message.
 * @param length - The length of the received message.
 * @param user_data - Pointer to data that the caller wishes to access in the IMPL function.
 */
void
cmsg_forwarding_server_process (cmsg_server *server, const uint8_t *data, uint32_t length,
                                void *user_data)
{
    struct cmsg_forwarding_server_data recv_data = {
        .msg = data,
        .len = length,
        .pos = 0,
        .user_data = user_data,
    };

    cmsg_transport_forwarding_user_data_set (server->_transport, &recv_data);
    cmsg_server_receive (server, 1);
}

/**
 * Get the user data supplied to the 'cmsg_forwarding_server_process' call.
 * Note that this data is only available while the message is being processed,
 * i.e. inside an IMPL function.
 *
 * @param server - The server to get the data for.
 *
 * @returns Pointer to the supplied user data.
 */
void *
cmsg_forwarding_server_user_data_get (cmsg_server *server)
{
    struct cmsg_forwarding_server_data *recv_data;

    recv_data = cmsg_transport_forwarding_user_data_get (server->_transport);

    return recv_data->user_data;
}
