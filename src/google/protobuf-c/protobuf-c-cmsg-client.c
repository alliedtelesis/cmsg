#include "protobuf-c-cmsg-client.h"


cmsg_client *
cmsg_client_new (cmsg_transport                   *transport,
                 const ProtobufCServiceDescriptor *descriptor)
{
    CMSG_ASSERT (transport);
    CMSG_ASSERT (descriptor);

    cmsg_client *client = malloc (sizeof (cmsg_client));
    if (client)
    {
        client->base_service.destroy = NULL;
        client->allocator = &protobuf_c_default_allocator;
        client->transport = transport;
        client->request_id = 0;

        //for compatibility with current generated code
        //this is a hack to get around a check when a client method is called
        client->descriptor = descriptor;
        client->base_service.descriptor = descriptor;

        client->invoke = transport->invoke;
        client->base_service.invoke = transport->invoke;
        client->parent = NULL;
        client->queue_enabled = 0;

        if (pthread_mutex_init (&client->queue_mutex, NULL) != 0)
        {
            DEBUG (CMSG_ERROR, "[CLIENT] error: queue mutex init failed\n");
            free (client);
            return NULL;
        }

        client->queue = g_queue_new ();
        g_queue_init (client->queue);
    }
    else
    {
        syslog(LOG_CRIT | LOG_LOCAL6, "[CLIENT] error: unable to create client. line(%d)\n",__LINE__);
    }

    return client;
}


void
cmsg_client_destroy (cmsg_client **client)
{
    CMSG_ASSERT (client);

    free (*client);
    *client = NULL;
}


ProtobufCMessage *
cmsg_client_response_receive (cmsg_client *client)
{
    CMSG_ASSERT (client);
    CMSG_ASSERT (client->transport);

    return (client->transport->client_recv (client));
}


int32_t
cmsg_client_connect (cmsg_client *client)
{
    int32_t ret;

    CMSG_ASSERT (client);

    DEBUG (CMSG_INFO, "[CLIENT] connecting\n");

    if (client->state == CMSG_CLIENT_STATE_CONNECTED)
    {
        DEBUG (CMSG_INFO, "[CLIENT] already connected\n");
        ret = CMSG_RET_OK;
    }
    else
    {
        ret = client->transport->connect (client);
    }

    return ret;
}


void
cmsg_client_invoke_rpc (ProtobufCService       *service,
                        unsigned                method_index,
                        const ProtobufCMessage *input,
                        ProtobufCClosure        closure,
                        void                   *closure_data)
{
    int ret = 0;
    cmsg_client *client = (cmsg_client *)service;

    /* pack the data */
    /* send */
    /* depending upon transport wait for response */
    /* unpack response */
    /* return response */

    CMSG_ASSERT (client);
    CMSG_ASSERT (input);

    DEBUG (CMSG_INFO, "[CLIENT] method: %s\n",
           service->descriptor->methods[method_index].name);

    cmsg_client_connect (client);

    if (client->state != CMSG_CLIENT_STATE_CONNECTED)
    {
        DEBUG (CMSG_ERROR, "[CLIENT] error: client is not connected\n");
        return;
    }

    const ProtobufCServiceDescriptor *desc = service->descriptor;
    const ProtobufCMethodDescriptor *method = desc->methods + method_index;

    uint32_t packed_size = protobuf_c_message_get_packed_size (input);

    client->request_id++;

    cmsg_header_request header;
    header.method_index = cmsg_common_uint32_to_le (method_index);
    header.message_length = cmsg_common_uint32_to_le (packed_size);
    header.request_id = client->request_id;
    uint8_t *buffer = malloc (packed_size + sizeof (header));
    if (!buffer)
    {
        syslog(LOG_CRIT | LOG_LOCAL6, "[CLIENT] error: unable to allocate buffer. line(%d)\n", __LINE__);
        return;
    }
    uint8_t *buffer_data = malloc (packed_size);
    if (!buffer_data)
    {
        free (buffer);
        syslog(LOG_CRIT | LOG_LOCAL6, "[CLIENT] error: unable to allocate data buffer. line(%d)\n", __LINE__);
        return;
    }
    memcpy ((void *)buffer, &header, sizeof (header));

    DEBUG (CMSG_INFO, "[CLIENT] header\n");
    cmsg_buffer_print (&header, sizeof (header));

    ret = protobuf_c_message_pack (input, buffer_data);
    if (ret < packed_size)
    {
        DEBUG (CMSG_ERROR,
               "[CLIENT] error: packing message data failed packet:%d of %d\n",
               ret, packed_size);

        free (buffer);
        free (buffer_data);
        return;
    }

    memcpy ((void *)buffer + sizeof (header), (void *)buffer_data, packed_size);

    DEBUG (CMSG_INFO, "[CLIENT] packet data\n");
    cmsg_buffer_print (buffer_data, packed_size);

    ret = client->transport->client_send (client, buffer, packed_size + sizeof (header), 0);
    if (ret < packed_size + sizeof (header))
    {
        DEBUG (CMSG_ERROR,
               "[CLIENT] error: sending response failed send:%d of %d\n",
               ret, packed_size + sizeof (header));

        free (buffer);
        free (buffer_data);
        return;
    }

    //lets go hackety hack
    //todo: recv response
    //todo: process response
    ProtobufCMessage *message = cmsg_client_response_receive (client);

    client->state = CMSG_CLIENT_STATE_DESTROYED;
    client->transport->client_close (client);

    free (buffer);
    free (buffer_data);

    if (!message)
    {
        DEBUG (CMSG_ERROR, "[CLIENT] error: response message not valid or empty\n");
        return;
    }

    //call closure
    if (closure) //check if closure is not zero, can be the case when we use empty messages
        closure (message, closure_data);

    protobuf_c_message_free_unpacked (message, client->allocator);

    return;
}


void
cmsg_client_invoke_oneway (ProtobufCService       *service,
                           unsigned                method_index,
                           const ProtobufCMessage *input,
                           ProtobufCClosure        closure,
                           void                   *closure_data)
{
    int ret = 0;
    cmsg_client *client = (cmsg_client *)service;

    /* pack the data */
    /* send */
    /* depending upon transport wait for response */
    /* unpack response */
    /* return response */

    CMSG_ASSERT (client);
    CMSG_ASSERT (input);

    DEBUG (CMSG_INFO, "[CLIENT] method: %s\n",
           service->descriptor->methods[method_index].name);

    //we don't connect to the server when we queue messages
    if (!client->queue_enabled)
    {
        DEBUG (CMSG_ERROR, "[CLIENT] error: queueing is disabled, connecting\n");
        cmsg_client_connect (client);

        if (client->state != CMSG_CLIENT_STATE_CONNECTED)
        {
            DEBUG (CMSG_ERROR, "[CLIENT] error: client is not connected\n");
            return;
        }
    }

    const ProtobufCServiceDescriptor *desc = service->descriptor;
    const ProtobufCMethodDescriptor *method = desc->methods + method_index;

    uint32_t packed_size = protobuf_c_message_get_packed_size (input);


    client->request_id++;

    cmsg_header_request header;
    header.method_index = cmsg_common_uint32_to_le (method_index);
    header.message_length = cmsg_common_uint32_to_le (packed_size);
    header.request_id = client->request_id;
    uint8_t *buffer = malloc (packed_size + sizeof (header));
    if (!buffer)
    {
        syslog(LOG_CRIT | LOG_LOCAL6, "[CLIENT] error: unable to allocate buffer. line(%d)\n", __LINE__);
        return;
    }
    uint8_t *buffer_data = malloc (packed_size);
    if (!buffer_data)
    {
        syslog(LOG_CRIT | LOG_LOCAL6, "[CLIENT] error: unable to allocate data buffer. line(%d)\n", __LINE__);
        free (buffer);
        return;
    }
    memcpy ((void *)buffer, &header, sizeof (header));

    DEBUG (CMSG_INFO, "[CLIENT] header\n");
    cmsg_buffer_print (&header, sizeof (header));

    ret = protobuf_c_message_pack (input, buffer_data);
    if (ret < packed_size)
    {
        DEBUG (CMSG_ERROR,
               "[CLIENT] error: packing message data failed packet:%d of %d\n",
               ret, packed_size);

        free (buffer);
        free (buffer_data);
        return;
    }

    memcpy ((void *)buffer + sizeof (header), (void *)buffer_data, packed_size);

    DEBUG (CMSG_INFO, "[CLIENT] packet data\n");
    cmsg_buffer_print (buffer_data, packed_size);

    //we don't connect to the server when we queue messages
    if (!client->queue_enabled)
    {
        DEBUG (CMSG_ERROR, "[CLIENT] error: sending message to server\n");
        ret = client->transport->client_send (client, buffer, packed_size + sizeof (header), 0);
        if (ret < packed_size + sizeof (header))
        {
            DEBUG (CMSG_ERROR,
               "[CLIENT] error: sending response failed send:%d of %d\n",
                   ret, packed_size + sizeof (header));

            free (buffer);
            free (buffer_data);
            return;
        }

        client->state = CMSG_CLIENT_STATE_DESTROYED;
        client->transport->client_close (client);
    }
    else
    {
        //add to queue
        cmsg_pub *publisher = 0;

        DEBUG (CMSG_INFO, "[CLIENT] adding message to queue\n");
        if (client->parent_type == CMSG_PARENT_TYPE_PUB)
        {
            publisher = (cmsg_pub *)client->parent;
        }
        else
        {
            DEBUG (CMSG_ERROR, "[CLIENT] parent type is wrong\n");
            return;
        }

        if (publisher && (client->parent_type == CMSG_PARENT_TYPE_PUB))
        {
            pthread_mutex_lock (&publisher->queue_mutex);

            cmsg_queue_entry *queue_entry = g_malloc (sizeof (cmsg_queue_entry));
            if (!queue_entry)
            {
		syslog(LOG_CRIT | LOG_LOCAL6, "[CLIENT] error: unable to allocate queue entry. line(%d)\n", __LINE__);
                free (buffer);
                free (buffer_data);
                pthread_mutex_unlock (&publisher->queue_mutex);
                return;
            }

            //copy buffer
            queue_entry->queue_buffer_size = packed_size + sizeof (header);
            queue_entry->queue_buffer = malloc (queue_entry->queue_buffer_size);
            if (!queue_entry->queue_buffer)
            {
		syslog(LOG_CRIT | LOG_LOCAL6, "[CLIENT] error: unable to allocate queue buffer. line(%d)\n", __LINE__);
                free (buffer);
                free (buffer_data);
                g_free (queue_entry);
                pthread_mutex_unlock (&publisher->queue_mutex);
                return;
            }
            memcpy ((void *)queue_entry->queue_buffer, (void *)buffer, queue_entry->queue_buffer_size);

            //copy client transport config
            queue_entry->transport.type = client->transport->type;
            queue_entry->transport.config.socket = client->transport->config.socket;

            g_queue_push_head (publisher->queue, queue_entry);

            publisher->queue_total_size += packed_size;

            pthread_mutex_unlock (&publisher->queue_mutex);

            unsigned int queue_length = g_queue_get_length (publisher->queue);
            DEBUG (CMSG_ERROR, "[CLIENT] queue length: %d\n", queue_length);
            DEBUG (CMSG_ERROR, "[CLIENT] queue total size: %d\n", publisher->queue_total_size);
        }
    }

    free (buffer);
    free (buffer_data);

    return;
}


int32_t
cmsg_client_queue_process_one (cmsg_client *client)
{
    uint32_t processed = 0;
    pthread_mutex_lock (&client->queue_mutex);

    cmsg_queue_entry *queue_entry = g_queue_pop_tail (client->queue);

    if (queue_entry)
    {
        cmsg_client_connect (client);

        if (client->state == CMSG_CLIENT_STATE_CONNECTED)
        {
            DEBUG (CMSG_INFO, "[CLIENT] sending message to server\n");
            int ret = client->transport->client_send (client, queue_entry->queue_buffer, queue_entry->queue_buffer_size, 0);
            if (ret < queue_entry->queue_buffer_size)
            {
                DEBUG (CMSG_ERROR,
                       "[CLIENT] error: sending response failed send:%d of %d\n, queue message dropped\n",
                       ret, queue_entry->queue_buffer_size);
            }

            client->state = CMSG_CLIENT_STATE_DESTROYED;
            client->transport->client_close (client);

            free (queue_entry->queue_buffer);
            g_free (queue_entry);
            processed++;
        }
        else
        {
            DEBUG (CMSG_ERROR, "[CLIENT] error: client is not connected, requeueing message\n");
            g_queue_push_head (client->queue, queue_entry);
        }
    }

    pthread_mutex_unlock (&client->queue_mutex);

    return processed;
}


int32_t
cmsg_client_queue_process_all (cmsg_client *client)
{
    uint32_t processed = 0;
    pthread_mutex_lock (&client->queue_mutex);

    cmsg_queue_entry *queue_entry = g_queue_pop_tail (client->queue);

    while (queue_entry)
    {
        cmsg_client_connect (client);

        if (client->state == CMSG_CLIENT_STATE_CONNECTED)
        {
            DEBUG (CMSG_INFO, "[CLIENT] sending message to server\n");
            int ret = client->transport->client_send (client,
                                                      queue_entry->queue_buffer,
                                                      queue_entry->queue_buffer_size,
                                                      0);

            if (ret < queue_entry->queue_buffer_size)
            {
                DEBUG (CMSG_ERROR,
                       "[CLIENT] error: sending response failed send:%d of %d, queue message dropped\n",
                       ret, queue_entry->queue_buffer_size);
            }

            client->state = CMSG_CLIENT_STATE_DESTROYED;
            client->transport->client_close (client);

            client->queue_total_size -= queue_entry->queue_buffer_size;

            free (queue_entry->queue_buffer);
            g_free (queue_entry);
            processed++;
        }
        else
        {
            DEBUG (CMSG_ERROR, "[CLIENT] error: client is not connected, requeueing message\n");
            g_queue_push_head (client->queue, queue_entry);

            pthread_mutex_unlock (&client->queue_mutex);

            int sleep_time = rand () % 5 + 1;

            DEBUG (CMSG_ERROR, "[PUB QUEUE] error: retrying in: %d seconds\n", sleep_time);
            sleep (sleep_time);
            DEBUG (CMSG_ERROR, "[PUB QUEUE] error: sleeping done\n");
            return -1;
        }
        cmsg_client_destroy (&client);

        //get the next entry
        queue_entry = g_queue_pop_tail (client->queue);
    }

    pthread_mutex_unlock (&client->queue_mutex);
    return processed;
}


int32_t
cmsg_client_queue_enable (cmsg_client *client)
{
    client->queue_enabled = 1;
    return 0;
}


int32_t
cmsg_client_queue_disable (cmsg_client *client)
{
    client->queue_enabled = 0;

    //todo: process all here
    cmsg_client_queue_process_all (client);

    return 0;
}
