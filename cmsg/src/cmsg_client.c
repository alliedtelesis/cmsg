/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#include "cmsg_private.h"
#include "cmsg_client.h"
#include "cmsg_error.h"
#include "transport/cmsg_transport_private.h"

#ifdef HAVE_COUNTERD
#include "cntrd_app_defines.h"
#include "cntrd_app_api.h"
#endif
#include <fcntl.h>

static int32_t _cmsg_client_buffer_send_retry_once (cmsg_client *client,
                                                    uint8_t *queue_buffer,
                                                    uint32_t queue_buffer_size,
                                                    const char *method_name);

static int32_t _cmsg_client_queue_process_all_internal (cmsg_client *client);

static int32_t _cmsg_client_queue_process_all_direct (cmsg_client *client);

static int32_t _cmsg_client_buffer_send (cmsg_client *client, uint8_t *buffer,
                                         uint32_t buffer_size);

static cmsg_client *_cmsg_create_client_tipc (const char *server, int member_id,
                                              int scope,
                                              ProtobufCServiceDescriptor *descriptor,
                                              cmsg_transport_type transport_type);

static int _cmsg_client_apply_send_timeout (int sock, uint32_t timeout);

static int _cmsg_client_apply_receive_timeout (int sockfd, uint32_t timeout);

int32_t cmsg_client_counter_create (cmsg_client *client, char *app_name);

static int32_t cmsg_client_invoke (ProtobufCService *service,
                                   uint32_t method_index,
                                   const ProtobufCMessage *input,
                                   ProtobufCClosure closure, void *closure_data);

static int32_t cmsg_client_queue_input (cmsg_client *client, uint32_t method_index,
                                        const ProtobufCMessage *input, bool *did_queue);

static void
cmsg_client_invoke_init (cmsg_client *client, cmsg_transport *transport)
{
    // Note these may be subsequently overridden (e.g. composite client)
    client->invoke = cmsg_client_invoke;
    client->base_service.invoke = cmsg_client_invoke;

    if (transport)
    {
        switch (transport->type)
        {
        case CMSG_TRANSPORT_RPC_TCP:
        case CMSG_TRANSPORT_RPC_TIPC:
        case CMSG_TRANSPORT_RPC_UNIX:
        case CMSG_TRANSPORT_RPC_USERDEFINED:
            client->invoke_send = cmsg_client_invoke_send;
            client->invoke_recv = cmsg_client_invoke_recv;
            break;
        case CMSG_TRANSPORT_ONEWAY_TCP:
        case CMSG_TRANSPORT_ONEWAY_TIPC:
        case CMSG_TRANSPORT_CPG:
        case CMSG_TRANSPORT_ONEWAY_USERDEFINED:
        case CMSG_TRANSPORT_BROADCAST:
        case CMSG_TRANSPORT_ONEWAY_UNIX:
            client->invoke_send = cmsg_client_invoke_send;
            client->invoke_recv = NULL;
            break;
        case CMSG_TRANSPORT_LOOPBACK:
            client->invoke_send = cmsg_client_invoke_send_direct;
            client->invoke_recv = cmsg_client_invoke_recv;
            break;
        default:
            assert (false && "Unknown transport type");
        }
    }
}

/*
 * This is an internal function which can be called from CMSG library.
 * Applications should use cmsg_client_new() instead.
 *
 * Create a new CMSG client (but without creating counters).
 */
cmsg_client *
cmsg_client_create (cmsg_transport *transport, const ProtobufCServiceDescriptor *descriptor)
{
    CMSG_ASSERT_RETURN_VAL (descriptor != NULL, NULL);

    cmsg_client *client = (cmsg_client *) CMSG_CALLOC (1, sizeof (cmsg_client));

    if (client)
    {
        client->state = CMSG_CLIENT_STATE_INIT;

        if (transport)
        {
            client->base_service.destroy = NULL;
            client->_transport = transport;
            cmsg_transport_write_id (transport, descriptor->name);
        }

        //for compatibility with current generated code
        //this is a hack to get around a check when a client method is called
        client->descriptor = descriptor;
        client->base_service.descriptor = descriptor;

        cmsg_client_invoke_init (client, transport);

        client->self.object_type = CMSG_OBJ_TYPE_CLIENT;
        client->self.object = client;
        strncpy (client->self.obj_id, descriptor->name, CMSG_MAX_OBJ_ID_LEN);

        client->parent.object_type = CMSG_OBJ_TYPE_NONE;
        client->parent.object = NULL;


        if (pthread_mutex_init (&client->queue_mutex, NULL) != 0)
        {
            CMSG_LOG_CLIENT_ERROR (client, "Init failed for queue_mutex.");
            CMSG_FREE (client);
            return NULL;
        }

        client->queue = g_queue_new ();
        client->queue_filter_hash_table = g_hash_table_new (g_str_hash, g_str_equal);

        if (pthread_cond_init (&client->queue_process_cond, NULL) != 0)
        {
            CMSG_LOG_CLIENT_ERROR (client, "Init failed for queue_process_cond.");
            CMSG_FREE (client);
            return NULL;
        }

        if (pthread_mutex_init (&client->queue_process_mutex, NULL) != 0)
        {
            CMSG_LOG_CLIENT_ERROR (client, "Init failed for queue_process_mutex.");
            CMSG_FREE (client);
            return NULL;
        }

        if (pthread_mutex_init (&client->invoke_mutex, NULL) != 0)
        {
            CMSG_LOG_CLIENT_ERROR (client, "Init failed for invoke_mutex.");
            CMSG_FREE (client);
            return NULL;
        }

        client->self_thread_id = pthread_self ();

        if (transport)
        {
            cmsg_client_queue_filter_init (client);
        }

        client->send_timeout = 0;
        client->receive_timeout = 0;
        client->suppress_errors = false;

        client->child_clients = NULL;
    }
    else
    {
        CMSG_LOG_GEN_ERROR ("[%s%s] Unable to create client.", descriptor->name,
                            transport->tport_id);
    }

    return client;
}


/*
 * Create a new CMSG client.
 */
cmsg_client *
cmsg_client_new (cmsg_transport *transport, const ProtobufCServiceDescriptor *descriptor)
{
    cmsg_client *client;
    client = cmsg_client_create (transport, descriptor);

#ifdef HAVE_COUNTERD
    char app_name[CNTRD_MAX_APP_NAME_LENGTH];

    /* initialise our counters */
    if (client != NULL)
    {
        snprintf (app_name, CNTRD_MAX_APP_NAME_LENGTH, "%s%s%s",
                  CMSG_COUNTER_APP_NAME_PREFIX, descriptor->name,
                  cmsg_transport_counter_app_tport_id (transport));

        if (cmsg_client_counter_create (client, app_name) != CMSG_RET_OK)
        {
            CMSG_LOG_GEN_ERROR ("[%s] Unable to create client counters.", app_name);
        }
    }
#endif

    return client;
}


void
cmsg_client_destroy (cmsg_client *client)
{
    CMSG_ASSERT_RETURN_VOID (client != NULL);

    /* Free counter session info but do not destroy counter data in the shared memory */
#ifdef HAVE_COUNTERD
    cntrd_app_unInit_app (&client->cntr_session, CNTRD_APP_PERSISTENT);
#endif
    client->cntr_session = NULL;

    cmsg_queue_filter_free (client->queue_filter_hash_table, client->descriptor);
    pthread_mutex_destroy (&client->queue_process_mutex);
    pthread_cond_destroy (&client->queue_process_cond);
    g_hash_table_destroy (client->queue_filter_hash_table);
    cmsg_send_queue_destroy (client->queue);
    pthread_mutex_destroy (&client->queue_mutex);

    // close the connection before destroying the client
    client->state = CMSG_CLIENT_STATE_CLOSED;
    if (client->_transport)
    {
        cmsg_client_close_wrapper (client->_transport);
        client->_transport->tport_funcs.client_destroy (client->_transport);
    }

    if (client->loopback_server)
    {
        cmsg_destroy_server_and_transport (client->loopback_server);
        client->loopback_server = NULL;
    }

    if (client->child_clients)
    {
        g_list_free (client->child_clients);
    }

    pthread_mutex_destroy (&client->invoke_mutex);

    CMSG_FREE (client);
}

// create counters
int32_t
cmsg_client_counter_create (cmsg_client *client, char *app_name)
{
    int32_t ret = CMSG_RET_ERR;

#ifdef HAVE_COUNTERD
    if (cntrd_app_init_app (app_name, CNTRD_APP_PERSISTENT,
                            (void **) &(client->cntr_session)) == CNTRD_APP_SUCCESS)
    {
        cntrd_app_register_ctr_in_group (client->cntr_session, "Client Unknown RPC",
                                         &(client->cntr_unknown_rpc));
        cntrd_app_register_ctr_in_group (client->cntr_session, "Client RPC Calls",
                                         &(client->cntr_rpc));
        cntrd_app_register_ctr_in_group (client->cntr_session, "Client Unknown Fields",
                                         &client->cntr_unknown_fields);
        cntrd_app_register_ctr_in_group (client->cntr_session, "Client Msgs Queued",
                                         &client->cntr_messages_queued);
        cntrd_app_register_ctr_in_group (client->cntr_session, "Client Msgs Dropped",
                                         &client->cntr_messages_dropped);
        cntrd_app_register_ctr_in_group (client->cntr_session, "Client Connect Attempts",
                                         &client->cntr_connect_attempts);
        cntrd_app_register_ctr_in_group (client->cntr_session, "Client Connect Failures",
                                         &client->cntr_connect_failures);
        cntrd_app_register_ctr_in_group (client->cntr_session, "Client Errors: General",
                                         &client->cntr_errors);
        cntrd_app_register_ctr_in_group (client->cntr_session, "Client Errors: Connection",
                                         &client->cntr_connection_errors);
        cntrd_app_register_ctr_in_group (client->cntr_session, "Client Errors: Recv",
                                         &client->cntr_recv_errors);
        cntrd_app_register_ctr_in_group (client->cntr_session, "Client Errors: Send",
                                         &client->cntr_send_errors);
        cntrd_app_register_ctr_in_group (client->cntr_session, "Client Errors: Pack",
                                         &client->cntr_pack_errors);
        cntrd_app_register_ctr_in_group (client->cntr_session, "Client Errors: Memory",
                                         &client->cntr_memory_errors);
        cntrd_app_register_ctr_in_group (client->cntr_session, "Client Errors: Protocol",
                                         &client->cntr_protocol_errors);
        cntrd_app_register_ctr_in_group (client->cntr_session, "Client Errors: Queue",
                                         &client->cntr_queue_errors);

        /* Tell cntrd not to destroy the counter data in the shared memory */
        cntrd_app_set_shutdown_instruction (app_name, CNTRD_SHUTDOWN_RESTART);
        ret = CMSG_RET_OK;
    }
#endif

    return ret;
}

cmsg_status_code
cmsg_client_response_receive (cmsg_client *client, ProtobufCMessage **message)
{
    return (client->_transport->
            tport_funcs.client_recv (client->_transport, client->descriptor, message));
}


/**
 * Connect the transport, unless it is already connected.
 *
 * @param client - The client to connect.
 * @param timeout - The timeout value to use.
 *
 * Returns 0 on success or a negative integer on failure.
 */
static int32_t
_cmsg_client_connect (cmsg_client *client, int timeout)
{
    int32_t ret = 0;
    int sock;

    CMSG_ASSERT_RETURN_VAL (client != NULL, CMSG_RET_ERR);

    CMSG_DEBUG (CMSG_INFO, "[CLIENT] connecting\n");

    if (client->state == CMSG_CLIENT_STATE_CONNECTED)
    {
        CMSG_DEBUG (CMSG_INFO, "[CLIENT] already connected\n");
    }
    else
    {
        // count the connection attempt
        CMSG_COUNTER_INC (client, cntr_connect_attempts);

        ret = client->_transport->tport_funcs.connect (client->_transport, timeout);

        if (ret < 0)
        {
            // count the connection failure
            CMSG_COUNTER_INC (client, cntr_connect_failures);
            client->state = CMSG_CLIENT_STATE_FAILED;
        }
        else
        {
            client->state = CMSG_CLIENT_STATE_CONNECTED;
            sock = client->_transport->connection.sockets.client_socket;

            if (client->send_timeout > 0)
            {
                // Set send timeout on the socket if needed
                if (_cmsg_client_apply_send_timeout (sock, client->send_timeout) < 0)
                {
                    CMSG_DEBUG (CMSG_INFO,
                                "[CLIENT] failed to set send timeout (errno=%d)\n", errno);
                }
            }

            if (client->receive_timeout > 0)
            {
                // Set receive timeout on the socket if needed
                if (_cmsg_client_apply_receive_timeout (sock, client->receive_timeout) < 0)
                {
                    CMSG_DEBUG (CMSG_INFO,
                                "[CLIENT] failed to set receive timeout (errno=%d)\n",
                                errno);
                }
            }
        }
    }

    return ret;
}


/**
 * Connect the transport using the default timeout value,
 * unless it's already connected.
 *
 * @param client - The client to connect.
 *
 * Returns 0 on success or a negative integer on failure.
 */
int32_t
cmsg_client_connect (cmsg_client *client)
{
    return _cmsg_client_connect (client, CONNECT_TIMEOUT_DEFAULT);
}

/**
 * Connect the transport with a non-default timeout value,
 * unless it's already connected.
 *
 * @param client - The client to connect.
 * @param timeout - The timeout value to use.
 *
 * Returns 0 on success or a negative integer on failure.
 */
int32_t
cmsg_client_connect_with_timeout (cmsg_client *client, int timeout)
{
    return _cmsg_client_connect (client, timeout);
}


/**
 * Configure send timeout for a cmsg client. This timeout will be applied immediately
 * to the client if it's already connected. Otherwise it will be applied when connected.
 * @param timeout   Timeout in seconds
 * @returns 0 on success or -1 on failure
 */
int
cmsg_client_set_send_timeout (cmsg_client *client, uint32_t timeout)
{
    CMSG_ASSERT_RETURN_VAL (client != NULL, CMSG_RET_ERR);

    client->send_timeout = timeout;

    /* If the client is already connected, then apply the new timeout immediately */
    if (client->state == CMSG_CLIENT_STATE_CONNECTED)
    {
        _cmsg_client_apply_send_timeout (client->_transport->connection.
                                         sockets.client_socket, client->send_timeout);
    }

    return 0;
}


/**
 * Apply send timeout to a socket
 * @param sockfd    socket file descriptor
 * @param timeout   timeout in seconds
 * @returns 0 on success or -1 on failure
 */
static int
_cmsg_client_apply_send_timeout (int sockfd, uint32_t timeout)
{
    struct timeval tv;

    tv.tv_sec = timeout;
    tv.tv_usec = 0;

    if (setsockopt (sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof (tv)) < 0)
    {
        return -1;
    }

    return 0;
}

/**
 * Apply receive timeout to a socket
 * @param sockfd    socket file descriptor
 * @param timeout   timeout in seconds
 * @returns 0 on success or -1 on failure
 */
static int
_cmsg_client_apply_receive_timeout (int sockfd, uint32_t timeout)
{
    struct timeval tv;

    tv.tv_sec = timeout;
    tv.tv_usec = 0;

    if (setsockopt (sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof (tv)) < 0)
    {
        return -1;
    }

    return 0;
}

/**
 * Configure receive timeout for a cmsg client. This timeout will be applied immediately
 * to the client if it's already connected. Otherwise it will be applied when connected.
 * @param timeout   Timeout in seconds
 * @returns 0 on success or -1 on failure
 */
int
cmsg_client_set_receive_timeout (cmsg_client *client, uint32_t timeout)
{
    CMSG_ASSERT_RETURN_VAL (client != NULL, CMSG_RET_ERR);

    client->receive_timeout = timeout;

    /* If the client is already connected, then apply the new timeout immediately */
    if (client->state == CMSG_CLIENT_STATE_CONNECTED)
    {
        _cmsg_client_apply_receive_timeout (client->_transport->connection.
                                            sockets.client_socket, client->receive_timeout);
    }

    return 0;
}

int32_t
cmsg_client_invoke_recv (cmsg_client *client, uint32_t method_index,
                         ProtobufCClosure closure, void *closure_data)
{
    cmsg_status_code status_code;
    ProtobufCMessage *message_pt;
    ProtobufCService *service = (ProtobufCService *) client;
    const char *method_name = service->descriptor->methods[method_index].name;

    /* message_pt is filled in by the response receive.  It may be NULL or a valid pointer.
     * status_code will tell us whether it is a valid pointer.
     */
    status_code = cmsg_client_response_receive (client, &message_pt);

    if (status_code == CMSG_STATUS_CODE_SERVICE_FAILED ||
        status_code == CMSG_STATUS_CODE_CONNECTION_CLOSED ||
        status_code == CMSG_STATUS_CODE_SERVER_CONNRESET)
    {
        /* CMSG_STATUS_CODE_SERVER_CONNRESET happens when the socket is reset by peer,
         * which can happen if the connection to the peer is lost (e.g. stack node leave).
         * And reporting this event as an error is too annoying.
         * If required the calling application should take care of this error. */
        if (status_code == CMSG_STATUS_CODE_SERVER_CONNRESET)
        {
            CMSG_LOG_DEBUG ("[CLIENT] Connection reset by peer (method: %s)\n",
                            method_name);
        }
        else
        {
            CMSG_LOG_CLIENT_ERROR (client, "No response from server. (method: %s)",
                                   method_name);
        }

        // close the connection and return early
        client->state = CMSG_CLIENT_STATE_CLOSED;
        cmsg_client_close_wrapper (client->_transport);

        CMSG_COUNTER_INC (client, cntr_recv_errors);
        return CMSG_RET_CLOSED;
    }

    /* If the call was queued then no point in calling closure as there is no message.
     * Need to exit.
     */
    if (status_code == CMSG_STATUS_CODE_SERVICE_QUEUED)
    {
        CMSG_DEBUG (CMSG_INFO, "[CLIENT] info: response message QUEUED\n");
        return CMSG_RET_QUEUED;
    }
    else if (status_code == CMSG_STATUS_CODE_SERVICE_DROPPED)
    {
        CMSG_DEBUG (CMSG_INFO, "[CLIENT] info: response message DROPPED\n");
        return CMSG_RET_DROPPED;
    }
    else if (status_code == CMSG_STATUS_CODE_SERVER_METHOD_NOT_FOUND)
    {
        CMSG_DEBUG (CMSG_INFO, "[CLIENT] info: response message METHOD NOT FOUND\n");
        CMSG_COUNTER_INC (client, cntr_unknown_rpc);
        return CMSG_RET_METHOD_NOT_FOUND;
    }
    else if (message_pt == NULL)
    {
        /* There may be no message if the server has sent an empty message which is ok. */
        if (status_code == CMSG_STATUS_CODE_SUCCESS)
        {
            return CMSG_RET_OK;
        }
        else
        {
            CMSG_LOG_CLIENT_ERROR (client,
                                   "Response message not valid or empty. (method: %s)",
                                   method_name);
            CMSG_COUNTER_INC (client, cntr_protocol_errors);
            return CMSG_RET_ERR;
        }
    }

    // increment the counter if this message has unknown fields,
    if (message_pt->unknown_fields)
    {
        CMSG_COUNTER_INC (client, cntr_unknown_fields);
    }

    if (closure_data)
    {
        // free unknown fields from received message as the developer doesn't know about them
        protobuf_c_message_free_unknown_fields (message_pt, &cmsg_memory_allocator);

        ((cmsg_client_closure_data *) (closure_data))->message = (void *) message_pt;
        ((cmsg_client_closure_data *) (closure_data))->allocator = &cmsg_memory_allocator;
    }
    else
    {
        /* only cleanup if the message is not passed back to the
         * api through the closure_data (above) */
        protobuf_c_message_free_unpacked (message_pt, &cmsg_memory_allocator);
    }

    return CMSG_RET_OK;
}

/**
 * To allow the client to be invoked safely from multiple threads
 * (i.e. from parallel CMSG API functions) we need to ensure that
 * the send/recv on the underlying socket is only executed in one
 * thread at a time. Note that the locking required to queue from
 * multiple threads (as part of the invoke call) is handled directly
 * by the queueing functionality.
 */
static int32_t
cmsg_client_invoke (ProtobufCService *service, uint32_t method_index,
                    const ProtobufCMessage *input, ProtobufCClosure closure,
                    void *closure_data)
{
    int32_t ret;
    cmsg_client *client = (cmsg_client *) service;
    bool did_queue = false;

    CMSG_ASSERT_RETURN_VAL (client != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (input != NULL, CMSG_RET_ERR);

    ret = cmsg_client_queue_input (client, method_index, input, &did_queue);
    if (ret != CMSG_RET_OK)
    {
        return ret;
    }

    if (!did_queue)
    {
        pthread_mutex_lock (&client->invoke_mutex);

        ret = client->invoke_send (client, method_index, input);
        if (ret == CMSG_RET_OK && client->invoke_recv)
        {
            ret = client->invoke_recv (client, method_index, closure, closure_data);
        }

        pthread_mutex_unlock (&client->invoke_mutex);
    }

    return ret;
}

static int
_cmsg_client_should_queue (cmsg_client *client, const char *method_name, bool *do_queue)
{
    cmsg_queue_filter_type action = CMSG_QUEUE_FILTER_ERROR;

    /* First check queuing action with the filter function if configured.
     * Otherwise lookup the filter table */
    if (client->queue_filter_func == NULL ||
        client->queue_filter_func (client, method_name, &action) != CMSG_RET_OK)
    {
        action = cmsg_client_queue_filter_lookup (client, method_name);
    }

    if (action == CMSG_QUEUE_FILTER_ERROR)
    {
        CMSG_LOG_CLIENT_ERROR (client,
                               "Error occurred with queue_lookup_filter. (method: %s).",
                               method_name);
        CMSG_COUNTER_INC (client, cntr_queue_errors);
        return CMSG_RET_ERR;
    }
    else if (action == CMSG_QUEUE_FILTER_DROP)
    {
        CMSG_DEBUG (CMSG_INFO, "[CLIENT] dropping message: %s\n", method_name);
        CMSG_COUNTER_INC (client, cntr_messages_dropped);
        return CMSG_RET_DROPPED;
    }
    else if (action == CMSG_QUEUE_FILTER_QUEUE)
    {
        *do_queue = true;
        // count this as queued
        CMSG_COUNTER_INC (client, cntr_messages_queued);
    }
    else if (action == CMSG_QUEUE_FILTER_PROCESS)
    {
        *do_queue = false;
    }

    return CMSG_RET_OK;
}

static int
_cmsg_client_add_to_queue (cmsg_client *client, uint8_t *buffer,
                           uint32_t total_message_size, const char *method_name)
{
    pthread_mutex_lock (&client->queue_mutex);

    //todo: check return
    cmsg_send_queue_push (client->queue, buffer,
                          total_message_size,
                          client, client->_transport, (char *) method_name);

    pthread_mutex_unlock (&client->queue_mutex);

    //send signal to cmsg_client_queue_process_all
    pthread_mutex_lock (&client->queue_process_mutex);
    if (client->queue_process_count == 0)
    {
        pthread_cond_signal (&client->queue_process_cond);
    }
    client->queue_process_count = client->queue_process_count + 1;
    pthread_mutex_unlock (&client->queue_process_mutex);

    // Execute callback function if configured
    if (client->queue_callback_func != NULL)
    {
        client->queue_callback_func (client, method_name);
    }

    return CMSG_RET_QUEUED;
}

/**
 * Create the CMSG packet based on the input method name
 * and data.
 *
 * @param client - CMSG client the packet is to be sent/queued with
 * @param method_name - Method name that was invoked
 * @param input - The input data that was supplied to be invoked with
 * @param buffer_ptr - Pointer to store the created packet
 * @param total_message_size_ptr - Pointer to store the created packet size
 */
int32_t
cmsg_client_create_packet (cmsg_client *client, const char *method_name,
                           const ProtobufCMessage *input, uint8_t **buffer_ptr,
                           uint32_t *total_message_size_ptr)
{
    uint32_t ret = 0;
    cmsg_header header;
    int type = CMSG_TLV_METHOD_TYPE;
    uint32_t method_length = strlen (method_name) + 1;

    uint32_t packed_size = protobuf_c_message_get_packed_size (input);
    uint32_t extra_header_size = CMSG_TLV_SIZE (method_length);
    uint32_t total_header_size = sizeof (header) + extra_header_size;
    uint32_t total_message_size = total_header_size + packed_size;

    header = cmsg_header_create (CMSG_MSG_TYPE_METHOD_REQ, extra_header_size,
                                 packed_size, CMSG_STATUS_CODE_UNSET);

    uint8_t *buffer = (uint8_t *) CMSG_CALLOC (1, total_message_size);
    if (!buffer)
    {
        CMSG_LOG_CLIENT_ERROR (client,
                               "Unable to allocate memory for message. (method: %s).",
                               method_name);
        CMSG_COUNTER_INC (client, cntr_memory_errors);
        return CMSG_RET_ERR;
    }

    cmsg_tlv_method_header_create (buffer, header, type, method_length, method_name);

    uint8_t *buffer_data = buffer + total_header_size;

    CMSG_DEBUG (CMSG_INFO, "[CLIENT] header\n");
    cmsg_buffer_print (&header, sizeof (header));

    ret = protobuf_c_message_pack (input, buffer_data);
    if (ret < packed_size)
    {
        CMSG_LOG_CLIENT_ERROR (client,
                               "Underpacked message data. Packed %d of %d bytes. (method: %s)",
                               ret, packed_size, method_name);

        CMSG_FREE (buffer);
        CMSG_COUNTER_INC (client, cntr_pack_errors);
        return CMSG_RET_ERR;
    }
    else if (ret > packed_size)
    {
        CMSG_LOG_CLIENT_ERROR (client,
                               "Overpacked message data. Packed %d of %d bytes. (method: %s)",
                               ret, packed_size, method_name);

        CMSG_FREE (buffer);
        CMSG_COUNTER_INC (client, cntr_pack_errors);
        return CMSG_RET_ERR;
    }

    CMSG_DEBUG (CMSG_INFO, "[CLIENT] packet data\n");
    cmsg_buffer_print (buffer_data, packed_size);

    *buffer_ptr = buffer;
    *total_message_size_ptr = total_message_size;

    return CMSG_RET_OK;
}

/**
 * Checks whether the input message should be queued and then queues
 * the message on the client if required.
 *
 * @param client - CMSG client the packet is to be queued with
 * @param method_index - Method index that was invoked
 * @param input - The input data that was supplied to be invoked with
 * @param did_queue - Pointer to store whether the message was queued or not
 */
static int32_t
cmsg_client_queue_input (cmsg_client *client, uint32_t method_index,
                         const ProtobufCMessage *input, bool *did_queue)
{
    uint32_t ret = 0;
    uint8_t *buffer = NULL;
    uint32_t total_message_size = 0;
    ProtobufCService *service = (ProtobufCService *) client;
    const char *method_name = service->descriptor->methods[method_index].name;

    ret = _cmsg_client_should_queue (client, method_name, did_queue);
    if (ret != CMSG_RET_OK)
    {
        return ret;
    }

    if (*did_queue)
    {
        ret = cmsg_client_create_packet (client, method_name, input, &buffer,
                                         &total_message_size);
        if (ret == CMSG_RET_OK)
        {
            ret = _cmsg_client_add_to_queue (client, buffer, total_message_size,
                                             method_name);
            CMSG_FREE (buffer);
        }
    }

    return ret;
}

int32_t
cmsg_client_invoke_send (cmsg_client *client, uint32_t method_index,
                         const ProtobufCMessage *input)
{
    uint32_t ret = 0;
    ProtobufCService *service = (ProtobufCService *) client;
    const char *method_name = service->descriptor->methods[method_index].name;
    uint8_t *buffer = NULL;
    uint32_t total_message_size = 0;

    // count every rpc call
    CMSG_COUNTER_INC (client, cntr_rpc);

    CMSG_DEBUG (CMSG_INFO, "[CLIENT] method: %s\n", method_name);

    ret = cmsg_client_create_packet (client, method_name, input, &buffer,
                                     &total_message_size);
    if (ret != CMSG_RET_OK)
    {
        return ret;
    }

    ret = cmsg_client_buffer_send_retry_once (client, buffer, total_message_size,
                                              method_name);
    CMSG_FREE (buffer);

    return ret;
}


/**
 * Invoking like this will call the server invoke directly in the same
 * process/thread as the client. No queuing or filtering is performed.
 *
 * The reply from the server will be written onto a pipe internally.
 */
int32_t
cmsg_client_invoke_send_direct (cmsg_client *client, uint32_t method_index,
                                const ProtobufCMessage *input)
{
    cmsg_server_invoke_direct (client->loopback_server, input, method_index);

    return CMSG_RET_OK;
}


int32_t
cmsg_client_get_socket (cmsg_client *client)
{
    int32_t sock = -1;

    CMSG_ASSERT_RETURN_VAL (client != NULL, -1);

    if (client->state == CMSG_CLIENT_STATE_CONNECTED)
    {
        sock = client->_transport->tport_funcs.c_socket (client->_transport);
    }
    else
    {
        CMSG_LOG_CLIENT_ERROR (client, "Failed to get socket. Client not connected.");
        CMSG_COUNTER_INC (client, cntr_connection_errors);
        return -1;
    }

    return sock;
}


/**
 * Sends an echo request to the server the client connects to.
 * The client should be one used specifically for this purpose.
 * The transport should be a RPC (twoway) connection so that a response can be received.
 *
 * The caller may not want to block however and so the function will return a
 * socket that can be listened on for the echo response.
 *
 * When the response is received the application should call cmsg_client_recv_echo_reply
 * to handle it's reception.
 */
int32_t
cmsg_client_send_echo_request (cmsg_client *client)
{
    int32_t ret = 0;
    // create header
    cmsg_header header = cmsg_header_create (CMSG_MSG_TYPE_ECHO_REQ,
                                             0, 0,
                                             CMSG_STATUS_CODE_UNSET);

    CMSG_ASSERT_RETURN_VAL (client != NULL, -1);

    CMSG_DEBUG (CMSG_INFO, "[CLIENT] header\n");
    cmsg_buffer_print (&header, sizeof (header));

    ret = cmsg_client_buffer_send_retry_once (client, (uint8_t *) &header,
                                              sizeof (header), "echo request");

    if (ret != CMSG_RET_OK)
    {
        return -1;
    }

    // return socket to listen on
    return client->_transport->tport_funcs.c_socket (client->_transport);
}


/**
 * Waits and receives the echo reply on the socket passed in.
 * @returns whether the status_code returned by the server.
 */
cmsg_status_code
cmsg_client_recv_echo_reply (cmsg_client *client)
{
    cmsg_status_code status_code;
    ProtobufCMessage *message_pt = NULL;

    CMSG_ASSERT_RETURN_VAL (client != NULL, CMSG_STATUS_CODE_UNSET);

    /* message_pt is filled in by the response receive.  It may be NULL or a valid pointer.
     * status_code will tell us whether it is a valid pointer.
     */
    status_code = cmsg_client_response_receive (client, &message_pt);

    if (message_pt != NULL)
    {
        // We don't expect a message to have been sent back so free it and
        // move on.  Not treating it as an error as this behaviour might
        // change in the future and it doesn't really matter.
        protobuf_c_message_free_unpacked (message_pt, &cmsg_memory_allocator);
    }

    return status_code;
}


bool
cmsg_client_transport_is_congested (cmsg_client *client)
{
    return client->_transport->tport_funcs.is_congested (client->_transport);
}


void
cmsg_client_queue_enable (cmsg_client *client)
{
    cmsg_client_queue_filter_set_all (client, CMSG_QUEUE_FILTER_QUEUE);
}

int32_t
cmsg_client_queue_disable (cmsg_client *client)
{
    cmsg_client_queue_filter_set_all (client, CMSG_QUEUE_FILTER_PROCESS);

    return cmsg_client_queue_process_all (client);
}

uint32_t
cmsg_client_queue_get_length (cmsg_client *client)
{
    pthread_mutex_lock (&client->queue_mutex);
    uint32_t queue_length = g_queue_get_length (client->queue);
    pthread_mutex_unlock (&client->queue_mutex);

    return queue_length;
}


int32_t
cmsg_client_queue_process_all (cmsg_client *client)
{
    struct timespec time_to_wait;
    uint32_t processed = 0;

    CMSG_ASSERT_RETURN_VAL (client != NULL, CMSG_RET_ERR);

    clock_gettime (CLOCK_REALTIME, &time_to_wait);

    //if the we run do api calls and processing in different threads wait
    //for a signal from the api thread to start processing
    if (client->self_thread_id != pthread_self ())
    {
        pthread_mutex_lock (&client->queue_process_mutex);
        while (client->queue_process_count == 0)
        {
            time_to_wait.tv_sec++;
            pthread_cond_timedwait (&client->queue_process_cond,
                                    &client->queue_process_mutex, &time_to_wait);
        }
        pthread_mutex_unlock (&client->queue_process_mutex);

        processed = _cmsg_client_queue_process_all_direct (client);

        pthread_mutex_lock (&client->queue_process_mutex);
        client->queue_process_count = client->queue_process_count - 1;
        pthread_mutex_unlock (&client->queue_process_mutex);

    }
    else
    {
        processed = _cmsg_client_queue_process_all_direct (client);
    }

    return processed;
}

static int32_t
_cmsg_client_queue_process_all_internal (cmsg_client *client)
{
    cmsg_send_queue_entry *queue_entry = NULL;
    GQueue *queue = client->queue;
    pthread_mutex_t *queue_mutex = &client->queue_mutex;
    cmsg_client *send_client = 0;

    pthread_mutex_lock (queue_mutex);
    if (g_queue_get_length (queue))
    {
        queue_entry = (cmsg_send_queue_entry *) g_queue_pop_tail (queue);
    }
    pthread_mutex_unlock (queue_mutex);

    while (queue_entry)
    {
        send_client = queue_entry->client;

        int ret = cmsg_client_buffer_send_retry (send_client,
                                                 queue_entry->queue_buffer,
                                                 queue_entry->queue_buffer_size,
                                                 CMSG_TRANSPORT_CLIENT_SEND_TRIES);

        if (ret == CMSG_RET_ERR)
        {
            CMSG_LOG_CLIENT_ERROR (client,
                                   "Server not reachable after %d tries. (method: %s).",
                                   CMSG_TRANSPORT_CLIENT_SEND_TRIES,
                                   queue_entry->method_name);
        }

        CMSG_FREE (queue_entry->queue_buffer);
        CMSG_FREE (queue_entry);
        queue_entry = NULL;

        if (ret == CMSG_RET_ERR)
        {
            return CMSG_RET_ERR;
        }

        //get the next entry
        pthread_mutex_lock (queue_mutex);
        queue_entry = (cmsg_send_queue_entry *) g_queue_pop_tail (queue);
        pthread_mutex_unlock (queue_mutex);
    }

    return CMSG_RET_OK;
}

static int32_t
_cmsg_client_queue_process_all_direct (cmsg_client *client)
{
    int ret = CMSG_RET_OK;
    GQueue *queue = client->queue;
    pthread_mutex_t *queue_mutex = &client->queue_mutex;

    if (!queue)
    {
        CMSG_COUNTER_INC (client, cntr_errors);
        return CMSG_RET_ERR;
    }

    ret = _cmsg_client_queue_process_all_internal (client);

    if (ret == CMSG_RET_ERR)
    {
        //delete all messages for this client from queue
        pthread_mutex_lock (queue_mutex);
        cmsg_send_queue_free_all_by_transport (queue, client->_transport);
        pthread_mutex_unlock (queue_mutex);

        CMSG_LOG_CLIENT_ERROR (client, "Server not reachable after %d tries.",
                               CMSG_TRANSPORT_CLIENT_SEND_TRIES);
    }

    return ret;
}

int32_t
cmsg_client_buffer_send_retry_once (cmsg_client *client, uint8_t *queue_buffer,
                                    uint32_t queue_buffer_size, const char *method_name)
{
    int ret = 0;

    CMSG_ASSERT_RETURN_VAL (client != NULL, CMSG_RET_ERR);

    pthread_mutex_lock (&client->_transport->connection_mutex);

    ret = _cmsg_client_buffer_send_retry_once (client, queue_buffer,
                                               queue_buffer_size, method_name);

    pthread_mutex_unlock (&client->_transport->connection_mutex);

    return ret;
}

static int32_t
_cmsg_client_buffer_send_retry_once (cmsg_client *client, uint8_t *queue_buffer,
                                     uint32_t queue_buffer_size, const char *method_name)
{
    int send_ret = 0;
    int connect_error = 0;

    connect_error = cmsg_client_connect (client);

    if (client->state != CMSG_CLIENT_STATE_CONNECTED)
    {
        CMSG_LOG_DEBUG ("[CLIENT] client is not connected (method: %s, error: %d)",
                        method_name, connect_error);
        return CMSG_RET_CLOSED;
    }

    send_ret = client->_transport->tport_funcs.client_send (client->_transport,
                                                            queue_buffer,
                                                            queue_buffer_size, 0);

    if (send_ret < (int) (queue_buffer_size))
    {
        // close the connection as something must be wrong
        client->state = CMSG_CLIENT_STATE_CLOSED;
        cmsg_client_close_wrapper (client->_transport);
        // the connection may be down due to a problem since the last send
        // attempt once to reconnect and send
        connect_error = cmsg_client_connect (client);

        if (client->state == CMSG_CLIENT_STATE_CONNECTED)
        {
            send_ret = client->_transport->tport_funcs.client_send (client->_transport,
                                                                    queue_buffer,
                                                                    queue_buffer_size, 0);

            if (send_ret < (int) (queue_buffer_size))
            {
                // Having retried connecting and now failed again this is
                // an actual problem.
                if (send_ret == -1)
                {
                    if (errno == EAGAIN)
                    {
                        CMSG_LOG_DEBUG ("[CLIENT] client_send failed (method: %s), %s",
                                        method_name, strerror (errno));
                    }
                    else
                    {
                        CMSG_LOG_CLIENT_ERROR (client,
                                               "Client send failed (method: %s), %s",
                                               method_name, strerror (errno));
                    }
                }
                else
                {
                    CMSG_LOG_CLIENT_ERROR (client,
                                           "Client send failed. Sent %d of %u bytes. (method: %s)",
                                           send_ret, (uint32_t) (queue_buffer_size),
                                           method_name);
                }
                CMSG_COUNTER_INC (client, cntr_send_errors);
                return CMSG_RET_ERR;
            }
        }
        else
        {
            CMSG_LOG_DEBUG ("[CLIENT] client is not connected (method: %s, error: %d)",
                            method_name, connect_error);
            return CMSG_RET_ERR;
        }
    }

    return CMSG_RET_OK;
}


int32_t
cmsg_client_buffer_send_retry (cmsg_client *client, uint8_t *queue_buffer,
                               uint32_t queue_buffer_size, int max_tries)
{
    int c = 0;

    CMSG_ASSERT_RETURN_VAL (client != NULL, CMSG_RET_ERR);

    for (c = 0; c <= max_tries; c++)
    {
        pthread_mutex_lock (&client->_transport->connection_mutex);
        int ret = _cmsg_client_buffer_send (client, queue_buffer, queue_buffer_size);
        pthread_mutex_unlock (&client->_transport->connection_mutex);

        if (ret == CMSG_RET_OK)
        {
            return CMSG_RET_OK;
        }
        else
        {
            usleep (200000);
        }
    }
    CMSG_DEBUG (CMSG_WARN, "[CLIENT] send tries %d\n", max_tries);

    return CMSG_RET_ERR;
}

static int32_t
_cmsg_client_buffer_send (cmsg_client *client, uint8_t *buffer, uint32_t buffer_size)
{
    int ret = 0;

    ret = cmsg_client_connect (client);

    if (client->state == CMSG_CLIENT_STATE_CONNECTED)
    {
        ret = client->_transport->tport_funcs.client_send (client->_transport, buffer,
                                                           buffer_size, 0);

        if (ret < (int) buffer_size)
        {
            CMSG_DEBUG (CMSG_ERROR, "[CLIENT] sending buffer failed, send: %d of %d\n",
                        ret, buffer_size);
            CMSG_COUNTER_INC (client, cntr_send_errors);
        }
        return CMSG_RET_OK;
    }
    else
    {
        CMSG_LOG_DEBUG ("[CLIENT] client is not connected, error: %d)", ret);
    }
    return CMSG_RET_ERR;
}

void
cmsg_client_queue_filter_set_all (cmsg_client *client, cmsg_queue_filter_type filter_type)
{
    cmsg_queue_filter_set_all (client->queue_filter_hash_table, client->descriptor,
                               filter_type);
}

void
cmsg_client_queue_filter_clear_all (cmsg_client *client)
{
    cmsg_queue_filter_clear_all (client->queue_filter_hash_table, client->descriptor);
}

int32_t
cmsg_client_queue_filter_set (cmsg_client *client, const char *method,
                              cmsg_queue_filter_type filter_type)
{
    return cmsg_queue_filter_set (client->queue_filter_hash_table, method, filter_type);
}

int32_t
cmsg_client_queue_filter_clear (cmsg_client *client, const char *method)
{
    return cmsg_queue_filter_clear (client->queue_filter_hash_table, method);
}

void
cmsg_client_msg_queue_filter_func_set (cmsg_client *client, cmsg_queue_filter_func_t func)
{
    if (client)
    {
        client->queue_filter_func = func;
    }
}

void
cmsg_client_msg_queue_callback_func_set (cmsg_client *client,
                                         cmsg_queue_callback_func_t func)
{
    if (client)
    {
        client->queue_callback_func = func;
    }
}

void
cmsg_client_queue_filter_init (cmsg_client *client)
{
    cmsg_queue_filter_init (client->queue_filter_hash_table, client->descriptor);
}

cmsg_queue_filter_type
cmsg_client_queue_filter_lookup (cmsg_client *client, const char *method)
{
    return cmsg_queue_filter_lookup (client->queue_filter_hash_table, method);
}

void
cmsg_client_suppress_error (cmsg_client *client, cmsg_bool_t enable)
{
    client->suppress_errors = enable;
}

/* Create a cmsg client and its transport with TIPC (RPC) */
static cmsg_client *
_cmsg_create_client_tipc (const char *server, int member_id, int scope,
                          ProtobufCServiceDescriptor *descriptor,
                          cmsg_transport_type transport_type)
{
    cmsg_transport *transport;
    cmsg_client *client;

    transport = cmsg_create_transport_tipc (server, member_id, scope, transport_type);
    if (!transport)
    {
        return NULL;
    }

    client = cmsg_client_new (transport, descriptor);
    if (!client)
    {
        cmsg_transport_destroy (transport);
        CMSG_LOG_GEN_ERROR ("No TIPC client to member %d", member_id);
        return NULL;
    }
    return client;
}

cmsg_client *
cmsg_create_client_tipc_rpc (const char *server_name, int member_id, int scope,
                             ProtobufCServiceDescriptor *descriptor)
{
    CMSG_ASSERT_RETURN_VAL (server_name != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (descriptor != NULL, NULL);

    return _cmsg_create_client_tipc (server_name, member_id, scope, descriptor,
                                     CMSG_TRANSPORT_RPC_TIPC);
}

cmsg_client *
cmsg_create_client_tipc_oneway (const char *server_name, int member_id, int scope,
                                ProtobufCServiceDescriptor *descriptor)
{
    CMSG_ASSERT_RETURN_VAL (server_name != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (descriptor != NULL, NULL);

    return _cmsg_create_client_tipc (server_name, member_id, scope, descriptor,
                                     CMSG_TRANSPORT_ONEWAY_TIPC);
}

/**
 * Creates a TIPC RPC client, transport and attempts to connect to the destination.
 *
 * If connection fails the client and transport are destroyed and NULL returned.
 */
cmsg_client *
cmsg_create_and_connect_client_tipc_rpc (const char *server_name, int member_id, int scope,
                                         ProtobufCServiceDescriptor *descriptor)
{
    int ret;
    cmsg_client *client;

    client = cmsg_create_client_tipc_rpc (server_name, member_id, scope, descriptor);

    if (client == NULL)
    {
        return NULL;
    }

    ret = cmsg_client_connect (client);
    if (ret < 0)
    {
        cmsg_destroy_client_and_transport (client);
        client = NULL;
    }

    return client;
}

/* Create a cmsg client and its transport over a UNIX socket */
static cmsg_client *
_cmsg_create_client_unix (const ProtobufCServiceDescriptor *descriptor,
                          cmsg_transport_type transport_type)
{
    cmsg_transport *transport;
    cmsg_client *client;

    transport = cmsg_create_transport_unix (descriptor, transport_type);
    if (!transport)
    {
        CMSG_LOG_GEN_ERROR ("Failed to create UNIX CMSG client for service: %s",
                            descriptor->name);
        return NULL;
    }

    client = cmsg_client_new (transport, descriptor);
    if (!client)
    {
        cmsg_transport_destroy (transport);
        CMSG_LOG_GEN_ERROR ("Failed to create UNIX CMSG client for service: %s",
                            descriptor->name);
        return NULL;
    }
    return client;
}

cmsg_client *
cmsg_create_client_unix (const ProtobufCServiceDescriptor *descriptor)
{
    CMSG_ASSERT_RETURN_VAL (descriptor != NULL, NULL);

    return _cmsg_create_client_unix (descriptor, CMSG_TRANSPORT_RPC_UNIX);
}

cmsg_client *
cmsg_create_client_unix_oneway (const ProtobufCServiceDescriptor *descriptor)
{
    CMSG_ASSERT_RETURN_VAL (descriptor != NULL, NULL);

    return _cmsg_create_client_unix (descriptor, CMSG_TRANSPORT_ONEWAY_UNIX);
}

int32_t
cmsg_client_unix_server_ready (const ProtobufCServiceDescriptor *descriptor)
{
    CMSG_ASSERT_RETURN_VAL (descriptor != NULL, CMSG_RET_ERR);

    int ret;
    char *sun_path = cmsg_transport_unix_sun_path (descriptor);

    ret = access (sun_path, F_OK);
    free (sun_path);

    return ret;
}

/* Create a cmsg client and its transport over a TCP socket */
static cmsg_client *
_cmsg_create_client_tcp (cmsg_socket *config, ProtobufCServiceDescriptor *descriptor,
                         cmsg_transport_type transport_type)
{
    cmsg_transport *transport;
    cmsg_client *client;

    transport = cmsg_create_transport_tcp (config, transport_type);

    if (!transport)
    {
        return NULL;
    }

    client = cmsg_client_new (transport, descriptor);
    if (!client)
    {
        cmsg_transport_destroy (transport);
        CMSG_LOG_GEN_ERROR ("No TCP IPC client on %s", descriptor->name);
        return NULL;
    }

    return client;
}

cmsg_client *
cmsg_create_client_tcp_rpc (cmsg_socket *config, ProtobufCServiceDescriptor *descriptor)
{
    CMSG_ASSERT_RETURN_VAL (config != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (descriptor != NULL, NULL);

    return _cmsg_create_client_tcp (config, descriptor, CMSG_TRANSPORT_RPC_TCP);
}

cmsg_client *
cmsg_create_client_tcp_oneway (cmsg_socket *config, ProtobufCServiceDescriptor *descriptor)
{
    CMSG_ASSERT_RETURN_VAL (config != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (descriptor != NULL, NULL);

    return _cmsg_create_client_tcp (config, descriptor, CMSG_TRANSPORT_ONEWAY_TCP);
}

/**
 * Creates a client of type Loopback and sets all the correct fields.
 *
 * Returns NULL if failed to create anything - malloc problems.
 */
cmsg_client *
cmsg_create_client_loopback (ProtobufCService *service)
{
    cmsg_transport *server_transport = NULL;
    cmsg_transport *client_transport = NULL;
    cmsg_server *server = NULL;
    cmsg_client *client = NULL;
    int pipe_fds[2] = { -1, -1 };

    server_transport = cmsg_transport_new (CMSG_TRANSPORT_LOOPBACK);
    if (server_transport == NULL)
    {
        CMSG_LOG_GEN_ERROR ("Could not create transport for loopback server\n");
        return NULL;
    }

    /* The point of the loopback is to process the CMSG within the same process-
     * space, without using RPC. So the client actually does the server-side
     * processing as well */
    server = cmsg_server_new (server_transport, service);
    if (server == NULL)
    {
        CMSG_LOG_GEN_ERROR ("Could not create server for loopback transport\n");
        cmsg_transport_destroy (server_transport);
        return NULL;
    }

    /* When using a loopback client/server the server_invoke gets given the
     * memory that the client declared the message in. We don't want the server
     * trying to free this memory (often it is on the stack) so let it know that
     * it does not own the memory for the messages. */
    cmsg_server_app_owns_all_msgs_set (server, true);

    client_transport = cmsg_transport_new (CMSG_TRANSPORT_LOOPBACK);
    if (client_transport == NULL)
    {
        CMSG_LOG_GEN_ERROR ("Could not create transport for loopback client\n");
        cmsg_destroy_server_and_transport (server);
        return NULL;
    }

    client = cmsg_client_new (client_transport, service->descriptor);

    if (client == NULL)
    {
        syslog (LOG_ERR, "Could not create loopback client");
        cmsg_destroy_server_and_transport (server);
        cmsg_transport_destroy (client_transport);
        return NULL;
    }

    /* the client stores a pointer to the server so we can access it later to
     * invoke the implementation function directly. */
    client->loopback_server = server;

    /* Create a pipe to allow the server to send RPC replies back to the client.
     * The server writes to the pipe and the client reads the reply off the pipe.
     * This is slightly inefficient, but code-wise it's the easiest way to get
     * the _impl_ response back to the client code */
    if (pipe (pipe_fds) == -1)
    {
        CMSG_LOG_GEN_ERROR ("Could not create pipe for loopback transport");
        cmsg_destroy_client_and_transport (client);
        return NULL;
    }

    /* don't block on either socket */
    fcntl (pipe_fds[0], F_SETFL, O_NONBLOCK);
    fcntl (pipe_fds[1], F_SETFL, O_NONBLOCK);

    /* client uses the pipe's read socket, server uses the write socket */
    client->_transport->connection.sockets.client_socket = pipe_fds[0];
    server->_transport->connection.sockets.client_socket = pipe_fds[1];

    return client;
}

/**
 * Close the client transport layer.
 *
 * NOTE user applications should not call this routine directly
 *
 */
void
cmsg_client_close_wrapper (cmsg_transport *transport)
{
    transport->tport_funcs.client_close (transport);
}

/**
 * Destroy a cmsg client and its transport
 *
 * @param client - the cmsg client to destroy
 */
void
cmsg_destroy_client_and_transport (cmsg_client *client)
{
    cmsg_transport *transport;

    if (client)
    {
        transport = client->_transport;
        cmsg_client_destroy (client);

        cmsg_transport_destroy (transport);
    }
}
