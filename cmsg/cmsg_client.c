#include "cmsg_private.h"
#include "cmsg_client.h"
#include "cmsg_error.h"

#ifdef HAVE_COUNTERD
#include "cntrd_app_defines.h"
#include "cntrd_app_api.h"
#endif

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

int32_t cmsg_client_counter_create (cmsg_client *client, char *app_name);


/*
 * This is an internal function which can be called from CMSG library.
 * Applications should use cmsg_client_new() instead.
 *
 * Create a new CMSG client (but without creating counters).
 */
cmsg_client *
cmsg_client_create (cmsg_transport *transport, const ProtobufCServiceDescriptor *descriptor)
{
    CMSG_ASSERT_RETURN_VAL (transport != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (descriptor != NULL, NULL);

    cmsg_client *client = (cmsg_client *) CMSG_CALLOC (1, sizeof (cmsg_client));

    if (client)
    {
        client->base_service.destroy = NULL;
        client->allocator = &protobuf_c_default_allocator;
        client->_transport = transport;
        cmsg_transport_write_id (transport);
        client->state = CMSG_CLIENT_STATE_INIT;
        client->connection.socket = -1;

        //for compatibility with current generated code
        //this is a hack to get around a check when a client method is called
        client->descriptor = descriptor;
        client->base_service.descriptor = descriptor;

        client->invoke = transport->invoke;
        client->base_service.invoke = transport->invoke;

        client->self.object_type = CMSG_OBJ_TYPE_CLIENT;
        client->self.object = client;
        strncpy (client->self.obj_id, descriptor->name, CMSG_MAX_OBJ_ID_LEN);

        client->parent.object_type = CMSG_OBJ_TYPE_NONE;
        client->parent.object = NULL;

        client->queue_enabled_from_parent = 0;

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
            return NULL;
        }

        if (pthread_mutex_init (&client->queue_process_mutex, NULL) != 0)
        {
            CMSG_LOG_CLIENT_ERROR (client, "Init failed for queue_process_mutex.");
            return NULL;
        }

        if (pthread_mutex_init (&client->connection_mutex, NULL) != 0)
        {
            CMSG_LOG_CLIENT_ERROR (client, "Init failed for connection_mutex.");
            return NULL;
        }

        client->self_thread_id = pthread_self ();

        cmsg_client_queue_filter_init (client);

#ifdef HAVE_CMSG_PROFILING
        memset (&client->prof, 0, sizeof (cmsg_prof));
#endif
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
                  CMSG_COUNTER_APP_NAME_PREFIX, descriptor->name, transport->tport_id);

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
        client->_transport->client_close (client);
        client->_transport->client_destroy (client);
    }

    pthread_mutex_destroy (&client->connection_mutex);

    CMSG_FREE (client);
}

// create counters
int32_t
cmsg_client_counter_create (cmsg_client *client, char *app_name)
{
    int32_t ret = CMSG_RET_ERR;

#ifdef HAVE_COUNTERD
    if (cntrd_app_init_app (app_name, CNTRD_APP_PERSISTENT,
                            (void **)&(client->cntr_session))
        == CNTRD_APP_SUCCESS )
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
    return (client->_transport->client_recv (client, message));
}


/**
 * Connect the transport unless it's already connected.
 * Returns 0 on success or a negative integer on failure.
 */
int32_t
cmsg_client_connect (cmsg_client *client)
{
    int32_t ret = 0;

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

        ret = client->_transport->connect (client);

        if (ret < 0)
        {
            // count the connection failure
            CMSG_COUNTER_INC (client, cntr_connect_failures);
        }
    }

    return ret;
}


int32_t
cmsg_client_invoke_rpc (ProtobufCService *service, unsigned method_index,
                        const ProtobufCMessage *input, ProtobufCClosure closure,
                        void *closure_data)
{
    uint32_t ret = 0;
    int send_ret = 0;
    int connect_error = 0;
    cmsg_client *client = (cmsg_client *) service;
    cmsg_status_code status_code;
    ProtobufCMessage *message_pt;
    const char *method_name;
    int method_length;
    int type = CMSG_TLV_METHOD_TYPE;
    cmsg_header header;

    CMSG_ASSERT_RETURN_VAL (client != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (input != NULL, CMSG_RET_ERR);

    CMSG_PROF_TIME_TIC (&client->prof);
    // count every rpc call
    CMSG_COUNTER_INC (client, cntr_rpc);

    /* pack the data */
    /* send */
    /* depending upon transport wait for response */
    /* unpack response */
    /* return response */
    method_name = service->descriptor->methods[method_index].name;

    CMSG_DEBUG (CMSG_INFO, "[CLIENT] method: %s\n", method_name);
    // open connection (if it is already open this will just return)
    connect_error = cmsg_client_connect (client);

    CMSG_PROF_TIME_LOG_ADD_TIME (&client->prof, "connect",
                                 cmsg_prof_time_toc (&client->prof));

    if (client->state != CMSG_CLIENT_STATE_CONNECTED)
    {
        CMSG_LOG_DEBUG ("[CLIENT] client is not connected (method: %s, error: %d)",
                        method_name, connect_error);
        return CMSG_RET_ERR;
    }
    method_length = strlen (method_name) + 1;
    CMSG_PROF_TIME_TIC (&client->prof);

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

    CMSG_PROF_TIME_LOG_ADD_TIME (&client->prof, "pack", cmsg_prof_time_toc (&client->prof));

    CMSG_DEBUG (CMSG_INFO, "[CLIENT] packet data\n");
    cmsg_buffer_print (buffer_data, packed_size);

    CMSG_PROF_TIME_TIC (&client->prof);

    send_ret = client->_transport->client_send (client,
                                                buffer, total_message_size, 0);

    if (send_ret < (int) (total_message_size))
    {
        // close the connection as something must be wrong
        client->state = CMSG_CLIENT_STATE_CLOSED;
        client->_transport->client_close (client);
        // the connection may be down due to a problem since the last send
        // attempt once to reconnect and send
        connect_error = cmsg_client_connect (client);

        if (client->state == CMSG_CLIENT_STATE_CONNECTED)
        {
            send_ret = client->_transport->client_send (client,
                                                        buffer,
                                                        total_message_size, 0);

            if (send_ret < (int) (total_message_size))
            {
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
                                               "Client send failed: %s. (method: %s)",
                                               strerror (errno), method_name);
                    }
                }
                else
                {
                    CMSG_LOG_CLIENT_ERROR (client,
                                           "Client send failed: Only sent %d of %u bytes. (method: %s)",
                                           send_ret, (uint32_t) (total_message_size),
                                           method_name);
                }

                CMSG_COUNTER_INC (client, cntr_send_errors);
                CMSG_FREE (buffer);
                return CMSG_RET_ERR;
            }
        }
        else
        {
            CMSG_LOG_DEBUG ("[CLIENT] couldn't reconnect client! (method: %s, error: %d)",
                            method_name, connect_error);

            CMSG_FREE (buffer);
            return CMSG_RET_ERR;
        }
    }

    CMSG_PROF_TIME_LOG_ADD_TIME (&client->prof, "send", cmsg_prof_time_toc (&client->prof));

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
        client->_transport->client_close (client);

        CMSG_COUNTER_INC (client, cntr_recv_errors);
        CMSG_FREE (buffer);
        return CMSG_RET_ERR;
    }

    CMSG_PROF_TIME_TIC (&client->prof);

    CMSG_FREE (buffer);
    buffer = NULL;
    buffer_data = NULL;

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
        ((cmsg_client_closure_data *) (closure_data))->message = (void *) message_pt;
        ((cmsg_client_closure_data *) (closure_data))->allocator = client->allocator;
    }
    else
    {
        /* only cleanup if the message is not passed back to the
         * api through the closure_data (above) */
        protobuf_c_message_free_unpacked (message_pt, client->allocator);
    }

    CMSG_PROF_TIME_LOG_ADD_TIME (&client->prof, "cleanup",
                                 cmsg_prof_time_toc (&client->prof));

    return CMSG_RET_OK;
}


int32_t
cmsg_client_invoke_oneway (ProtobufCService *service, unsigned method_index,
                           const ProtobufCMessage *input, ProtobufCClosure closure,
                           void *closure_data)
{
    uint32_t ret = 0;
    cmsg_client *client = (cmsg_client *) service;
    int do_queue = 0;
    const char *method_name;
    uint32_t method_length;
    int type = CMSG_TLV_METHOD_TYPE;
    cmsg_header header;
    int ret_val;

    CMSG_ASSERT_RETURN_VAL (client != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (input != NULL, CMSG_RET_ERR);

    // test - increment the counter on every rpc to see if this is working
    CMSG_COUNTER_INC (client, cntr_rpc);

    method_name = service->descriptor->methods[method_index].name;

    CMSG_DEBUG (CMSG_INFO, "[CLIENT] method: %s\n", method_name);

    if (client->queue_enabled_from_parent)
    {
        // queuing has been enabled from parent publisher
        // so don't do client queue filter lookup
        do_queue = 1;
    }
    else
    {
        cmsg_queue_filter_type action;

        action = cmsg_client_queue_filter_lookup (client, method_name);

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
            do_queue = 1;
            // count this as queued
            CMSG_COUNTER_INC (client, cntr_messages_queued);
        }
        else if (action == CMSG_QUEUE_FILTER_PROCESS)
        {
            do_queue = 0;
        }
    }

    method_length = strlen (method_name) + 1;

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
                               "Unable to allocate memory for message. (method: %s)",
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
        CMSG_COUNTER_INC (client, cntr_pack_errors);
        CMSG_FREE (buffer);
        return CMSG_RET_ERR;
    }
    else if (ret > packed_size)
    {
        CMSG_LOG_CLIENT_ERROR (client,
                               "Overpacked message data. Packed %d of %d bytes. (method: %s)",
                               ret, packed_size, method_name);
        CMSG_COUNTER_INC (client, cntr_pack_errors);
        CMSG_FREE (buffer);
        return CMSG_RET_ERR;
    }

    CMSG_DEBUG (CMSG_INFO, "[CLIENT] packet data\n");
    cmsg_buffer_print (buffer_data, packed_size);

    if (!do_queue)
    {
        ret_val = cmsg_client_buffer_send_retry_once (client,
                                                      buffer,
                                                      total_message_size,
                                                      method_name);
    }
    else
    {
        //add to queue
        if (client->parent.object_type == CMSG_OBJ_TYPE_PUB)
        {
            cmsg_pub *publisher = (cmsg_pub *) client->parent.object;

            pthread_mutex_lock (&publisher->queue_mutex);

            //todo: check return
            cmsg_send_queue_push (publisher->queue, buffer,
                                  total_message_size,
                                  client, client->_transport, (char *) method_name);

            pthread_mutex_unlock (&publisher->queue_mutex);

            //send signal to  cmsg_pub_queue_process_all
            pthread_mutex_lock (&publisher->queue_process_mutex);
            if (client->queue_process_count == 0)
                pthread_cond_signal (&publisher->queue_process_cond);
            publisher->queue_process_count = publisher->queue_process_count + 1;
            pthread_mutex_unlock (&publisher->queue_process_mutex);
        }
        else if (client->parent.object_type == CMSG_OBJ_TYPE_NONE)
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
                pthread_cond_signal (&client->queue_process_cond);
            client->queue_process_count = client->queue_process_count + 1;
            pthread_mutex_unlock (&client->queue_process_mutex);
        }
        ret_val = CMSG_RET_QUEUED;
    }

    CMSG_FREE (buffer);

    return ret_val;
}


/**
 * Invoking like this will call the server invoke directly without packing
 * and sending the rpc.
 *
 * No queuing or filtering is performed.
 *
 * There is nothing to be returned so closure will not get set at all.
 */
int32_t
cmsg_client_invoke_oneway_direct (ProtobufCService *service, unsigned method_index,
                                  const ProtobufCMessage *input, ProtobufCClosure closure,
                                  void *closure_data)
{
    int32_t ret;
    const char *method_name;
    uint8_t buf_static[512];
    uint8_t *buffer = NULL;
    uint32_t packed_size;

    method_name = service->descriptor->methods[method_index].name;

    /* The service is actually a cmsg_client so we can typecast here. */
    cmsg_client *client = (cmsg_client *) service;

    packed_size = protobuf_c_message_get_packed_size (input);

    if (packed_size < sizeof (buf_static))
    {
        buffer = &buf_static[0];
    }
    else
    {
        buffer = CMSG_CALLOC (1, packed_size);
    }

    if (!buffer)
    {
        CMSG_LOG_CLIENT_ERROR (client,
                               "Unable to allocate memory for message. (method: %s)",
                               method_name);
        CMSG_COUNTER_INC (client, cntr_memory_errors);

        return CMSG_RET_ERR;
    }

    ret = protobuf_c_message_pack (input, buffer);

    if (ret < packed_size)
    {
        CMSG_LOG_CLIENT_ERROR (client,
                               "Underpacked message data. Packed %d of %d bytes. (method: %s)",
                               ret, packed_size, method_name);
        CMSG_COUNTER_INC (client, cntr_pack_errors);

        if (buffer != &buf_static[0])
        {
            CMSG_FREE (buffer);
        }

        return CMSG_RET_ERR;
    }
    else if (ret > packed_size)
    {
        CMSG_LOG_CLIENT_ERROR (client,
                               "Overpacked message data. Packed %d of %d bytes. (method: %s)",
                               ret, packed_size, method_name);
        CMSG_COUNTER_INC (client, cntr_pack_errors);

        if (buffer != &buf_static[0])
        {
            CMSG_FREE (buffer);
        }

        return CMSG_RET_ERR;
    }

    /* use the client to get the service pointer that the server requires to call
     * the proper function.
     */
    cmsg_server_invoke_oneway_direct (client->_transport->config.lpb_service, method_index,
                                      buffer, packed_size);

    if (buffer != &buf_static[0])
    {
        CMSG_FREE (buffer);
    }

    return CMSG_RET_OK;
}


int32_t
cmsg_client_get_socket (cmsg_client *client)
{
    int32_t sock = -1;

    CMSG_ASSERT_RETURN_VAL (client != NULL, -1);

    if (client->state == CMSG_CLIENT_STATE_CONNECTED)
    {
        sock = client->_transport->c_socket (client);
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
    int connect_error = 0;

    CMSG_ASSERT_RETURN_VAL (client != NULL, -1);

    // if not connected connect now
    connect_error = cmsg_client_connect (client);

    if (client->state != CMSG_CLIENT_STATE_CONNECTED)
    {
        CMSG_LOG_DEBUG ("[CLIENT] client is not connected (error: %d)", connect_error);
        return -1;
    }

    // create header
    cmsg_header header = cmsg_header_create (CMSG_MSG_TYPE_ECHO_REQ,
                                             0, 0,
                                             CMSG_STATUS_CODE_UNSET);

    // send
    CMSG_DEBUG (CMSG_INFO, "[CLIENT] header\n");
    cmsg_buffer_print (&header, sizeof (header));

    ret = client->_transport->client_send (client, &header, sizeof (header), 0);
    if (ret < (int32_t) (sizeof (header)))
    {
        CMSG_LOG_CLIENT_ERROR (client, "Failed sending all data");

        // close the connection as something must be wrong
        client->state = CMSG_CLIENT_STATE_CLOSED;
        client->_transport->client_close (client);

        // the connection may be down due to a problem since the last send
        // attempt once to reconnect and send
        connect_error = cmsg_client_connect (client);

        if (client->state == CMSG_CLIENT_STATE_CONNECTED)
        {
            ret = client->_transport->client_send (client, &header, sizeof (header), 0);
            if (ret < (int32_t) sizeof (header))
            {
                CMSG_LOG_CLIENT_ERROR (client,
                                       "Sending echo request failed. sent:%d of %u bytes.",
                                       ret, (uint32_t) sizeof (header));
                CMSG_COUNTER_INC (client, cntr_send_errors);
                return -1;
            }
        }
        else
        {
            CMSG_LOG_DEBUG ("[CLIENT] couldn't reconnect client! (error: %d)",
                            connect_error);
            return -1;
        }
    }

    // return socket to listen on
    return client->_transport->c_socket (client);
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
        protobuf_c_message_free_unpacked (message_pt, client->allocator);
    }

    return status_code;
}


int32_t
cmsg_client_transport_is_congested (cmsg_client *client)
{
    return client->_transport->is_congested (client);
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

        processed = _cmsg_client_queue_process_all_direct (client);
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
        queue_entry = (cmsg_send_queue_entry *) g_queue_pop_tail (queue);
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

    pthread_mutex_lock (&client->connection_mutex);

    ret = _cmsg_client_buffer_send_retry_once (client, queue_buffer,
                                               queue_buffer_size, method_name);

    pthread_mutex_unlock (&client->connection_mutex);

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
        return CMSG_RET_ERR;
    }

    send_ret = client->_transport->client_send (client,
                                                queue_buffer,
                                                queue_buffer_size, 0);

    if (send_ret < (int) (queue_buffer_size))
    {
        // close the connection as something must be wrong
        client->state = CMSG_CLIENT_STATE_CLOSED;
        client->_transport->client_close (client);
        // the connection may be down due to a problem since the last send
        // attempt once to reconnect and send
        connect_error = cmsg_client_connect (client);

        if (client->state == CMSG_CLIENT_STATE_CONNECTED)
        {
            send_ret = client->_transport->client_send (client,
                                                        queue_buffer,
                                                        queue_buffer_size,
                                                        0);

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
        pthread_mutex_lock (&client->connection_mutex);
        int ret = _cmsg_client_buffer_send (client, queue_buffer, queue_buffer_size);
        pthread_mutex_unlock (&client->connection_mutex);

        if (ret == CMSG_RET_OK)
            return CMSG_RET_OK;
        else
            usleep (200000);
    }
    CMSG_DEBUG (CMSG_WARN, "[CLIENT] send tries %d\n", max_tries);

    return CMSG_RET_ERR;
}

int32_t
cmsg_client_buffer_send (cmsg_client *client, uint8_t *buffer, uint32_t buffer_size)
{
    int ret = CMSG_RET_ERR;

    CMSG_ASSERT_RETURN_VAL (client != NULL, CMSG_RET_ERR);

    pthread_mutex_lock (&client->connection_mutex);
    ret = _cmsg_client_buffer_send (client, buffer, buffer_size);
    pthread_mutex_unlock (&client->connection_mutex);

    return ret;
}

static int32_t
_cmsg_client_buffer_send (cmsg_client *client, uint8_t *buffer, uint32_t buffer_size)
{
    int ret = 0;

    ret = cmsg_client_connect (client);

    if (client->state == CMSG_CLIENT_STATE_CONNECTED)
    {
        ret = client->_transport->client_send (client, buffer, buffer_size, 0);

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
cmsg_client_queue_filter_show (cmsg_client *client)
{
    cmsg_queue_filter_show (client->queue_filter_hash_table, client->descriptor);
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
        CMSG_LOG_CLIENT_ERROR (client, "No TIPC client to member %d", member_id);
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
 * Creates a Client of type Loopback Oneway and sets all the correct
 * fields.
 *
 * Returns NULL if failed to create anything - malloc problems.
 */
cmsg_client *
cmsg_create_client_loopback_oneway (ProtobufCService *service)
{
    cmsg_transport *client_transport;

    client_transport = cmsg_transport_new (CMSG_TRANSPORT_LOOPBACK_ONEWAY);
    if (client_transport == NULL)
    {
        return NULL;
    }

    /* point the client transport at the service to allow it call the proper fn */
    client_transport->config.lpb_service = service;

    return cmsg_client_new (client_transport, service->descriptor);
}

/* Destroy a cmsg client and its transport with TIPC */
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
