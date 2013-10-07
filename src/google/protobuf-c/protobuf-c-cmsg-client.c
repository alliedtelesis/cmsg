#include "protobuf-c-cmsg-client.h"


cmsg_client *
cmsg_client_new (cmsg_transport *transport, const ProtobufCServiceDescriptor *descriptor)
{
    CMSG_ASSERT (transport);
    CMSG_ASSERT (descriptor);

    cmsg_client *client = malloc (sizeof (cmsg_client));
    if (client)
    {
        client->base_service.destroy = NULL;
        client->allocator = &protobuf_c_default_allocator;
        client->_transport = transport;
        client->request_id = 0;
        client->state = CMSG_CLIENT_STATE_INIT;

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
            free (client);
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

        client->self_thread_id = pthread_self ();


        cmsg_client_queue_filter_init (client);
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

    client->_transport->client_destroy (client);

    free (client);
}


cmsg_status_code
cmsg_client_response_receive (cmsg_client *client, ProtobufCMessage **message)
{
    CMSG_ASSERT (client);
    CMSG_ASSERT (client->_transport);

    return (client->_transport->client_recv (client, message));
}


int32_t
cmsg_client_connect (cmsg_client *client)
{
    int32_t ret = 0;

    CMSG_ASSERT (client);

    DEBUG (CMSG_INFO, "[CLIENT] connecting\n");

    if (client->state == CMSG_CLIENT_STATE_CONNECTED)
    {
        DEBUG (CMSG_INFO, "[CLIENT] already connected\n");
        ret = CMSG_RET_OK;
    }
    else
    {
        ret = client->_transport->connect (client);
    }

    return ret;
}


void
cmsg_client_invoke_rpc (ProtobufCService *service, unsigned method_index,
                        const ProtobufCMessage *input, ProtobufCClosure closure,
                        void *closure_data)
{
    int ret = 0;
    cmsg_client *client = (cmsg_client *) service;
    cmsg_status_code status_code;
    ProtobufCMessage *message_pt;

    /* pack the data */
    /* send */
    /* depending upon transport wait for response */
    /* unpack response */
    /* return response */

    CMSG_ASSERT (client);
    CMSG_ASSERT (input);

    DEBUG (CMSG_INFO, "[CLIENT] method: %s\n",
           service->descriptor->methods[method_index].name);

    // open connection (if it is already open this will just return)
    cmsg_client_connect (client);

    if (client->state != CMSG_CLIENT_STATE_CONNECTED)
    {
        CMSG_LOG_USER_ERROR ("[CLIENT] error: client is not connected");
        return;
    }

    const ProtobufCServiceDescriptor *desc = service->descriptor;
    const ProtobufCMethodDescriptor *method = desc->methods + method_index;

    uint32_t packed_size = protobuf_c_message_get_packed_size (input);

    client->request_id++;

    cmsg_header header = cmsg_header_create (CMSG_MSG_TYPE_METHOD_REQ, packed_size,
                                             method_index, 0);
    uint8_t *buffer = malloc (packed_size + sizeof (header));
    if (!buffer)
    {
        CMSG_LOG_USER_ERROR (
                "[CLIENT] error: unable to allocate buffer. line(%d)", __LINE__);
        return;
    }
    uint8_t *buffer_data = malloc (packed_size);
    if (!buffer_data)
    {
        free (buffer);
        CMSG_LOG_USER_ERROR (
                "[CLIENT] error: unable to allocate data buffer. line(%d)", __LINE__);
        return;
    }
    memcpy ((void *) buffer, &header, sizeof (header));

    DEBUG (CMSG_INFO, "[CLIENT] header\n");
    cmsg_buffer_print (&header, sizeof (header));

    ret = protobuf_c_message_pack (input, buffer_data);
    if (ret < packed_size)
    {
        CMSG_LOG_USER_ERROR (
               "[CLIENT] error: packing message data failed packet:%d of %d",
               ret, packed_size);

        free (buffer);
        free (buffer_data);
        return;
    }

    memcpy ((void *) buffer + sizeof (header), (void *) buffer_data, packed_size);

    DEBUG (CMSG_INFO, "[CLIENT] packet data\n");
    cmsg_buffer_print (buffer_data, packed_size);

    ret = client->_transport->client_send (client, buffer, packed_size + sizeof (header),
                                           0);
    if (ret < packed_size + sizeof (header))
    {
        // close the connection as something must be wrong
        client->state = CMSG_CLIENT_STATE_CLOSED;
        client->_transport->client_close (client);
        // the connection may be down due to a problem since the last send
        // attempt once to reconnect and send
        cmsg_client_connect (client);

        if (client->state == CMSG_CLIENT_STATE_CONNECTED)
        {
            ret = client->_transport->client_send (client, buffer,
                                                   packed_size + sizeof (header), 0);
            if (ret < packed_size + sizeof (header))
            {
                CMSG_LOG_USER_ERROR (
                       "[CLIENT] error: sending response failed send:%d of %d",
                       ret, packed_size + sizeof (header));
                free (buffer);
                free (buffer_data);
                return;
            }
        }
        else
        {
            CMSG_LOG_USER_ERROR ("[CLIENT] error: couldn't reconnect client!");
            free (buffer);
            free (buffer_data);
            return;
        }
    }

    /* message_pt is filled in by the response receive.  It may be NULL or a valid pointer.
     * status_code will tell us whether it is a valid pointer.
     */
    status_code = cmsg_client_response_receive (client, &message_pt);

    if (status_code == CMSG_STATUS_CODE_SERVICE_FAILED)
    {
        CMSG_LOG_USER_ERROR ("[CLIENT] No response from server");

        // close the connection and return early
        client->state = CMSG_CLIENT_STATE_CLOSED;
        client->_transport->client_close (client);

        free (buffer);
        free (buffer_data);
        return;
    }

    free (buffer);
    free (buffer_data);

    /* If the call was queued then no point in calling closure as there is no message.
     * Need to exit.
     */
    if (status_code == CMSG_STATUS_CODE_SERVICE_QUEUED ||
        status_code == CMSG_STATUS_CODE_SERVICE_DROPPED)
    {
        DEBUG (CMSG_INFO, "[CLIENT] info: response message %s\n",
               status_code == CMSG_STATUS_CODE_SERVICE_QUEUED ? "QUEUED" : "DROPPED");
        return;
    }
    else if (message_pt == NULL)
    {
        CMSG_LOG_USER_ERROR ("[CLIENT] error: response message not valid or empty\n");
        return;
    }

    //call closure
    if (closure)    //check if closure is not zero, can be the case when we use empty messages
        closure (message_pt, closure_data);

    protobuf_c_message_free_unpacked (message_pt, client->allocator);

    return;
}


void
cmsg_client_invoke_oneway (ProtobufCService *service, unsigned method_index,
                           const ProtobufCMessage *input, ProtobufCClosure closure,
                           void *closure_data)
{
    int ret = 0;
    cmsg_client *client = (cmsg_client *) service;
    int do_queue = 0;

    /* pack the data */
    /* send */
    /* depending upon transport wait for response */
    /* unpack response */
    /* return response */

    CMSG_ASSERT (client);
    CMSG_ASSERT (input);

    DEBUG (CMSG_INFO, "[CLIENT] method: %s\n",
           service->descriptor->methods[method_index].name);

    if (client->queue_enabled_from_parent)
    {
        // queuing has been enable from parent publisher
        // so don't do client queue filter lookup
        do_queue = 1;
    }
    else
    {
        cmsg_queue_filter_type action = cmsg_client_queue_filter_lookup (client,
                                                                         service->descriptor->methods[method_index].name);

        if (action == CMSG_QUEUE_FILTER_ERROR)
        {
            CMSG_LOG_USER_ERROR (
                   "[CLIENT] error: queue_lookup_filter returned CMSG_QUEUE_FILTER_ERROR for: %s",
                   service->descriptor->methods[method_index].name);
            return;
        }
        else if (action == CMSG_QUEUE_FILTER_DROP)
        {
            DEBUG (CMSG_INFO,
                   "[CLIENT] dropping message: %s\n",
                   service->descriptor->methods[method_index].name);
            return;
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
        cmsg_client_connect (client);

        if (client->state != CMSG_CLIENT_STATE_CONNECTED)
        {
            CMSG_LOG_USER_ERROR ("[CLIENT] error: client is not connected");
            return;
        }
    }

    const ProtobufCServiceDescriptor *desc = service->descriptor;
    const ProtobufCMethodDescriptor *method = desc->methods + method_index;

    uint32_t packed_size = protobuf_c_message_get_packed_size (input);


    client->request_id++;

    cmsg_header header = cmsg_header_create (CMSG_MSG_TYPE_METHOD_REQ, packed_size,
                                             method_index, 0);

    uint8_t *buffer = malloc (packed_size + sizeof (header));
    if (!buffer)
    {
        CMSG_LOG_USER_ERROR (
                "[CLIENT] error: unable to allocate buffer. line(%d)", __LINE__);
        return;
    }
    uint8_t *buffer_data = malloc (packed_size);
    if (!buffer_data)
    {
        CMSG_LOG_USER_ERROR (
                "[CLIENT] error: unable to allocate data buffer. line(%d)", __LINE__);
        free (buffer);
        return;
    }
    memcpy ((void *) buffer, &header, sizeof (header));

    DEBUG (CMSG_INFO, "[CLIENT] header\n");
    cmsg_buffer_print (&header, sizeof (header));

    ret = protobuf_c_message_pack (input, buffer_data);
    if (ret < packed_size)
    {
        CMSG_LOG_USER_ERROR (
               "[CLIENT] error: packing message data failed packet:%d of %d",
               ret, packed_size);

        free (buffer);
        free (buffer_data);
        return;
    }

    memcpy ((void *) buffer + sizeof (header), (void *) buffer_data, packed_size);

    DEBUG (CMSG_INFO, "[CLIENT] packet data\n");
    cmsg_buffer_print (buffer_data, packed_size);

    //we don't connect to the server when we queue messages
    if (!do_queue)
    {
        ret = client->_transport->client_send (client, buffer,
                                               packed_size + sizeof (header), 0);
        if (ret < packed_size + sizeof (header))
        {
            // close the connection as something must be wrong
            client->state = CMSG_CLIENT_STATE_CLOSED;
            client->_transport->client_close (client);
            // the connection may be down due to a problem since the last send
            // attempt once to reconnect and send
            cmsg_client_connect (client);

            if (client->state == CMSG_CLIENT_STATE_CONNECTED)
            {
                ret = client->_transport->client_send (client, buffer,
                                                       packed_size + sizeof (header), 0);
                if (ret < packed_size + sizeof (header))
                {
                    // Having retried connecting and now failed again this is
                    // an actual problem.
                    CMSG_LOG_USER_ERROR (
                           "[CLIENT] error: sending response failed send:%d of %d",
                           ret, packed_size + sizeof (header));
                    free (buffer);
                    free (buffer_data);
                    return;
                }
            }
            else
            {
                CMSG_LOG_USER_ERROR ("[CLIENT] error: couldn't reconnect client!");
                free (buffer);
                free (buffer_data);
                return;
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
                                  packed_size + sizeof (header), client->_transport);

            pthread_mutex_unlock (&publisher->queue_mutex);

            //send signal to  cmsg_pub_queue_process_all
            pthread_mutex_lock (&publisher->queue_process_mutex);
            if (client->queue_process_count == 0)
                pthread_cond_signal (&publisher->queue_process_cond);
            publisher->queue_process_count = publisher->queue_process_count + 1;
            pthread_mutex_unlock (&publisher->queue_process_mutex);


            unsigned int queue_length = g_queue_get_length (publisher->queue);
            DEBUG (CMSG_INFO, "[PUBLISHER] queue length: %d\n", queue_length);

        }
        else if (client->parent.object_type == CMSG_OBJ_TYPE_NONE)
        {
            pthread_mutex_lock (&client->queue_mutex);

            //todo: check return
            cmsg_send_queue_push (client->queue, buffer,
                                  packed_size + sizeof (header), client->_transport);

            pthread_mutex_unlock (&client->queue_mutex);

            //send signal to cmsg_client_queue_process_all
            pthread_mutex_lock (&client->queue_process_mutex);
            if (client->queue_process_count == 0)
                pthread_cond_signal (&client->queue_process_cond);
            client->queue_process_count = client->queue_process_count + 1;
            pthread_mutex_unlock (&client->queue_process_mutex);

            unsigned int queue_length = g_queue_get_length (client->queue);
            DEBUG (CMSG_INFO, "[CLIENT] queue length: %d\n", queue_length);
        }
    }

    free (buffer);
    free (buffer_data);

    return;
}


int32_t
cmsg_client_get_socket (cmsg_client *client)
{
    int32_t sock = -1;

    if (!client)
    {
        CMSG_LOG_USER_ERROR ("Attempted to get a socket where the client didn't exist");
        return -1;
    }

    if (client->state == CMSG_CLIENT_STATE_CONNECTED)
    {
        sock = client->_transport->c_socket (client);
    }
    else
    {
        CMSG_LOG_USER_ERROR ("The client isn't connect so the socket cannot be gotten");
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

    // if not connected connect now
    cmsg_client_connect (client);

    // create header
    cmsg_header header = cmsg_header_create (CMSG_MSG_TYPE_ECHO_REQ, 0, 0, 0);

    // send
    DEBUG (CMSG_INFO, "[CLIENT] header\n");
    cmsg_buffer_print (&header, sizeof (header));

    ret = client->_transport->client_send (client, &header,
                                           sizeof (header), 0);
    if (ret < sizeof (header))
    {
        CMSG_LOG_USER_ERROR ("Failed sending all data");

        // close the connection as something must be wrong
        client->state = CMSG_CLIENT_STATE_CLOSED;
        client->_transport->client_close (client);

        // the connection may be down due to a problem since the last send
        // attempt once to reconnect and send
        cmsg_client_connect (client);

        if (client->state == CMSG_CLIENT_STATE_CONNECTED)
        {
            ret = client->_transport->client_send (client, &header,
                                                   sizeof (header), 0);
            if (ret < sizeof (header))
            {
                CMSG_LOG_USER_ERROR (
                       "[CLIENT] error: sending echo req failed sent:%d of %d",
                       ret, sizeof (header));
                return -1;
            }
        }
        else
        {
            CMSG_LOG_USER_ERROR ("[CLIENT] error: couldn't reconnect client!");
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
    int ret = 0;
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

unsigned int
cmsg_client_queue_get_length (cmsg_client *client)
{
    return cmsg_queue_get_length (client->queue);
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
cmsg_client *
cmsg_create_client_tipc_rpc (const char *server, int member_id, int scope,
                         ProtobufCServiceDescriptor *descriptor)
{
    uint32_t server_port;
    cmsg_transport *transport;
    cmsg_client *client;

    server_port = cmsg_service_port_get (server, "tipc");
    if (server_port <= 0)
    {
        CMSG_LOG_USER_ERROR ("Unknown TIPC service %s", server);
        return NULL;
    }

    transport = cmsg_transport_new (CMSG_TRANSPORT_RPC_TIPC);

    if (!transport)
    {
        CMSG_LOG_USER_ERROR ("No TIPC transport to %d", member_id);
        return NULL;
    }

    transport->config.socket.family = AF_TIPC;
    transport->config.socket.sockaddr.tipc.family = AF_TIPC;
    transport->config.socket.sockaddr.tipc.addrtype = TIPC_ADDR_NAME;
    transport->config.socket.sockaddr.tipc.addr.name.name.type = server_port;
    transport->config.socket.sockaddr.tipc.addr.name.name.instance = member_id;
    transport->config.socket.sockaddr.tipc.scope = scope;

    client = cmsg_client_new (transport, descriptor);

    if (!client)
    {
        cmsg_transport_destroy (transport);
        CMSG_LOG_USER_ERROR ("No TIPC client to %d", member_id);
        return NULL;
    }

    return client;
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
