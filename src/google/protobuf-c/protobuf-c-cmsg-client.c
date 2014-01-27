#include "protobuf-c-cmsg-client.h"


cmsg_client *
cmsg_client_new (cmsg_transport *transport, const ProtobufCServiceDescriptor *descriptor)
{
    CMSG_ASSERT (transport);
    CMSG_ASSERT (descriptor);

    cmsg_client *client = (cmsg_client *) CMSG_CALLOC (1, sizeof (cmsg_client));
    if (client)
    {
        client->base_service.destroy = NULL;
        client->allocator = &protobuf_c_default_allocator;
        client->_transport = transport;
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

        client->parent.object_type = CMSG_OBJ_TYPE_NONE;
        client->parent.object = NULL;

        client->queue_enabled_from_parent = 0;

        if (pthread_mutex_init (&client->queue_mutex, NULL) != 0)
        {
            DEBUG (CMSG_ERROR, "[CLIENT] error: queue mutex init failed\n");
            CMSG_FREE (client);
            return NULL;
        }

        client->queue = g_queue_new ();
        client->queue_filter_hash_table = g_hash_table_new (cmsg_queue_filter_hash_function,
                                                            cmsg_queue_filter_hash_equal_function);

        if (pthread_cond_init (&client->queue_process_cond, NULL) != 0)
        {
            DEBUG (CMSG_ERROR, "[CLIENT] error: queue_process_cond init failed\n");
            return 0;
        }

        if (pthread_mutex_init (&client->queue_process_mutex, NULL) != 0)
        {
            DEBUG (CMSG_ERROR, "[CLIENT] error: queue_process_mutex init failed\n");
            return 0;
        }

        if (pthread_mutex_init (&client->connection_mutex, NULL) != 0)
        {
            DEBUG (CMSG_ERROR, "[CLIENT] error: connection_mutex init failed\n");
            return 0;
        }

        client->self_thread_id = pthread_self ();

        cmsg_client_queue_filter_init (client);

#ifdef HAVE_CMSG_PROFILING
        memset (&client->prof, 0, sizeof (cmsg_prof));
#endif
    }
    else
    {
        syslog (LOG_CRIT | LOG_LOCAL6,
                "[CLIENT] error: unable to create client. line(%d)\n", __LINE__);
    }

    return client;
}


void
cmsg_client_destroy (cmsg_client *client)
{
    CMSG_ASSERT (client);

    cmsg_queue_filter_free (client->queue_filter_hash_table, client->descriptor);

    g_hash_table_destroy (client->queue_filter_hash_table);

    cmsg_send_queue_free_all (client->queue);

    pthread_mutex_destroy (&client->queue_mutex);

    // close the connection before destroying the client
    client->state = CMSG_CLIENT_STATE_CLOSED;
    if (client->_transport)
    {
        client->_transport->client_close (client);
    }
    client->_transport->client_destroy (client);

    pthread_mutex_destroy (&client->connection_mutex);

    CMSG_FREE (client);
}


cmsg_status_code
cmsg_client_response_receive (cmsg_client *client, ProtobufCMessage **message)
{
    CMSG_ASSERT (client);
    CMSG_ASSERT (client->_transport);

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

    CMSG_ASSERT (client);

    DEBUG (CMSG_INFO, "[CLIENT] connecting\n");

    if (client->state == CMSG_CLIENT_STATE_CONNECTED)
    {
        DEBUG (CMSG_INFO, "[CLIENT] already connected\n");
    }
    else
    {
        ret = client->_transport->connect (client);
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

    CMSG_PROF_TIME_TIC (&client->prof);

    /* pack the data */
    /* send */
    /* depending upon transport wait for response */
    /* unpack response */
    /* return response */

    CMSG_ASSERT (client);
    CMSG_ASSERT (input);

    method_name = service->descriptor->methods[method_index].name;

    DEBUG (CMSG_INFO, "[CLIENT] method: %s\n", method_name);
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
    uint32_t extra_header_size = TLV_SIZE (method_length);
    uint32_t total_header_size = sizeof (header) + extra_header_size;
    uint32_t total_message_size = total_header_size + packed_size;

    header = cmsg_header_create (CMSG_MSG_TYPE_METHOD_REQ, extra_header_size,
                                 packed_size, CMSG_STATUS_CODE_UNSET);

    uint8_t *buffer = (uint8_t *) CMSG_CALLOC (1, total_message_size);
    if (!buffer)
    {
        CMSG_LOG_ERROR ("[CLIENT] error: unable to allocate buffer (method: %s)",
                        method_name);
        return CMSG_RET_ERR;
    }

    cmsg_tlv_method_header_create (buffer, header, type, method_length, method_name);

    uint8_t *buffer_data = buffer + total_header_size;

    DEBUG (CMSG_INFO, "[CLIENT] header\n");
    cmsg_buffer_print (&header, sizeof (header));

    ret = protobuf_c_message_pack (input, buffer_data);
    if (ret < packed_size)
    {
        CMSG_LOG_ERROR ("[CLIENT] error: packing message data failed, underrun packet:%d of %d (method: %s)",
                        ret, packed_size, method_name);

        CMSG_FREE (buffer);
        return CMSG_RET_ERR;
    }
    else if (ret > packed_size)
    {
        CMSG_LOG_ERROR ("[CLIENT] error: packing message data overwrote buffer: %d of %d (method: %s)",
                        ret, packed_size, method_name);

        CMSG_FREE (buffer);
        return CMSG_RET_ERR;
    }

    CMSG_PROF_TIME_LOG_ADD_TIME (&client->prof, "pack", cmsg_prof_time_toc (&client->prof));

    DEBUG (CMSG_INFO, "[CLIENT] packet data\n");
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
                CMSG_LOG_ERROR ("[CLIENT] error: sending response failed send:%d of %u (method: %s)",
                                send_ret,
                                (uint32_t) (total_message_size),
                                method_name);

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
            CMSG_LOG_ERROR ("[CLIENT] No response from server (method: %s)", method_name);
        }

        // close the connection and return early
        client->state = CMSG_CLIENT_STATE_CLOSED;
        client->_transport->client_close (client);

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
    if (status_code == CMSG_STATUS_CODE_SERVICE_QUEUED ||
        status_code == CMSG_STATUS_CODE_SERVICE_DROPPED)
    {
        DEBUG (CMSG_INFO, "[CLIENT] info: response message %s\n",
               status_code == CMSG_STATUS_CODE_SERVICE_QUEUED ? "QUEUED" : "DROPPED");
        return CMSG_RET_OK;
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
            CMSG_LOG_ERROR ("[CLIENT] error: response message not valid or empty (method: %s)",
                            method_name);
            return CMSG_RET_ERR;
        }
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
    int send_ret = 0;
    int connect_error = 0;
    cmsg_client *client = (cmsg_client *) service;
    int do_queue = 0;
    const char *method_name;
    uint32_t method_length;
    int type = CMSG_TLV_METHOD_TYPE;
    cmsg_header header;

    /* pack the data */
    /* send */
    /* depending upon transport wait for response */
    /* unpack response */
    /* return response */

    CMSG_ASSERT (client);
    CMSG_ASSERT (input);

    method_name = service->descriptor->methods[method_index].name;

    DEBUG (CMSG_INFO, "[CLIENT] method: %s\n", method_name);

    pthread_mutex_lock (&client->connection_mutex);

    if (client->queue_enabled_from_parent)
    {
        // queuing has been enable from parent publisher
        // so don't do client queue filter lookup
        do_queue = 1;
    }
    else
    {
        cmsg_queue_filter_type action;

        action = cmsg_client_queue_filter_lookup (client, method_name);

        if (action == CMSG_QUEUE_FILTER_ERROR)
        {
            CMSG_LOG_ERROR ("[CLIENT] error: queue_lookup_filter returned ERROR (method: %s)",
                            method_name);

            pthread_mutex_unlock (&client->connection_mutex);
            return CMSG_RET_ERR;
        }
        else if (action == CMSG_QUEUE_FILTER_DROP)
        {
            DEBUG (CMSG_INFO, "[CLIENT] dropping message: %s\n", method_name);
            pthread_mutex_unlock (&client->connection_mutex);
            return CMSG_RET_OK;
        }
        else if (action == CMSG_QUEUE_FILTER_QUEUE)
        {
            do_queue = 1;
        }
        else if (action == CMSG_QUEUE_FILTER_PROCESS)
        {
            do_queue = 0;
        }
    }


    //we don't connect to the server when we queue messages
    if (!do_queue)
    {
        DEBUG (CMSG_INFO, "[CLIENT] error: queueing is disabled, connecting\n");
        connect_error = cmsg_client_connect (client);

        if (client->state != CMSG_CLIENT_STATE_CONNECTED)
        {
            CMSG_LOG_DEBUG ("[CLIENT] client is not connected (method: %s, error: %d)",
                            method_name, connect_error);

            pthread_mutex_unlock (&client->connection_mutex);
            return CMSG_RET_ERR;
        }
    }
    method_length = strlen (method_name) + 1;

    uint32_t packed_size = protobuf_c_message_get_packed_size (input);
    uint32_t extra_header_size = TLV_SIZE (method_length);
    uint32_t total_header_size = sizeof (header) + extra_header_size;
    uint32_t total_message_size = total_header_size + packed_size;

    header = cmsg_header_create (CMSG_MSG_TYPE_METHOD_REQ, extra_header_size,
                                 packed_size, CMSG_STATUS_CODE_UNSET);

    uint8_t *buffer = (uint8_t *) CMSG_CALLOC (1, total_message_size);
    if (!buffer)
    {
        CMSG_LOG_ERROR ("[CLIENT] error: unable to allocate buffer (method: %s)",
                        method_name);

        pthread_mutex_unlock (&client->connection_mutex);
        return CMSG_RET_ERR;
    }

    cmsg_tlv_method_header_create (buffer, header, type, method_length, method_name);
    uint8_t *buffer_data = buffer + total_header_size;

    DEBUG (CMSG_INFO, "[CLIENT] header\n");
    cmsg_buffer_print (&header, sizeof (header));

    ret = protobuf_c_message_pack (input, buffer_data);
    if (ret < packed_size)
    {
        CMSG_LOG_ERROR ("[CLIENT] error: packing message data failed with underrun, packet:%d of %d (method: %s)",
                        ret, packed_size, method_name);

        CMSG_FREE (buffer);
        pthread_mutex_unlock (&client->connection_mutex);
        return CMSG_RET_ERR;
    }
    else if (ret > packed_size)
    {
        CMSG_LOG_ERROR ("[CLIENT] error: packing message data overwrote buffer: %d of %d (method: %s)",
                        ret, packed_size, method_name);

        CMSG_FREE (buffer);
        pthread_mutex_unlock (&client->connection_mutex);
        return CMSG_RET_ERR;
    }

    DEBUG (CMSG_INFO, "[CLIENT] packet data\n");
    cmsg_buffer_print (buffer_data, packed_size);

    //we don't connect to the server when we queue messages
    if (!do_queue)
    {
        send_ret = client->_transport->client_send (client,
                                                    buffer,
                                                    total_message_size, 0);

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
                                                            total_message_size,
                                                            0);

                if (send_ret < (int) (total_message_size))
                {
                    // Having retried connecting and now failed again this is
                    // an actual problem.
                    CMSG_LOG_ERROR ("[CLIENT] error: sending response failed send:%d of %u (method: %s)",
                                    send_ret,
                                    (uint32_t) (total_message_size),
                                    method_name);

                    CMSG_FREE (buffer);
                    pthread_mutex_unlock (&client->connection_mutex);
                    return CMSG_RET_ERR;
                }
            }
            else
            {
                CMSG_LOG_DEBUG ("[CLIENT] client is not connected (method: %s, error: %d)",
                                method_name, connect_error);

                CMSG_FREE (buffer);
                pthread_mutex_unlock (&client->connection_mutex);
                return CMSG_RET_ERR;
            }
        }
    }
    else
    {
        //add to queue
        client->state = CMSG_CLIENT_STATE_QUEUED;

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
    }

    CMSG_FREE (buffer);
    pthread_mutex_unlock (&client->connection_mutex);
    return CMSG_RET_OK;
}


int32_t
cmsg_client_get_socket (cmsg_client *client)
{
    int32_t sock = -1;

    if (!client)
    {
        CMSG_LOG_ERROR ("Attempted to get a socket where the client didn't exist");
        return -1;
    }

    if (client->state == CMSG_CLIENT_STATE_CONNECTED)
    {
        sock = client->_transport->c_socket (client);
    }
    else
    {
        CMSG_LOG_ERROR ("The client isn't connect so the socket cannot be gotten");
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
    DEBUG (CMSG_INFO, "[CLIENT] header\n");
    cmsg_buffer_print (&header, sizeof (header));

    ret = client->_transport->client_send (client, &header, sizeof (header), 0);
    if (ret < (int32_t) (sizeof (header)))
    {
        CMSG_LOG_ERROR ("Failed sending all data");

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
                CMSG_LOG_ERROR ("[CLIENT] error: sending echo req failed sent:%d of %u",
                                ret, (uint32_t) sizeof (header));
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
    cmsg_object obj;

    clock_gettime (CLOCK_REALTIME, &time_to_wait);

    obj.object_type = CMSG_OBJ_TYPE_CLIENT;
    obj.object = client;

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

        processed = cmsg_send_queue_process_all (obj);
        client->queue_process_count = client->queue_process_count - 1;
        pthread_mutex_unlock (&client->queue_process_mutex);

    }
    else
    {
        processed = cmsg_send_queue_process_all (obj);
    }

    return processed;
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
cmsg_create_client_tipc (const char *server, int member_id, int scope,
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
        CMSG_LOG_ERROR ("No TIPC client to %d", member_id);
        return NULL;
    }
    return client;
}

cmsg_client *
cmsg_create_client_tipc_rpc (const char *server_name, int member_id, int scope,
                             ProtobufCServiceDescriptor *descriptor)
{
    return cmsg_create_client_tipc (server_name, member_id, scope, descriptor,
                                    CMSG_TRANSPORT_RPC_TIPC);
}

cmsg_client *
cmsg_create_client_tipc_oneway (const char *server_name, int member_id, int scope,
                                ProtobufCServiceDescriptor *descriptor)
{
    return cmsg_create_client_tipc (server_name, member_id, scope, descriptor,
                                    CMSG_TRANSPORT_ONEWAY_TIPC);
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
