
#include "protobuf-c-cmsg-queue.h"

unsigned int
cmsg_queue_get_length (GQueue *queue)
{
    return g_queue_get_length (queue);
}

/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/

int32_t
cmsg_send_queue_process_all (cmsg_object obj)
{
    uint32_t processed = 0;
    uint32_t create_client = 0;
    cmsg_send_queue_entry *queue_entry = 0;
    GQueue *queue;
    pthread_mutex_t queue_mutex;
    const ProtobufCServiceDescriptor *descriptor;
    cmsg_pub *publisher = 0;
    cmsg_client *client = 0;

    if (obj.object_type == CMSG_OBJ_TYPE_CLIENT)
    {
        client = (cmsg_client *) obj.object;
        queue = client->queue;
        queue_mutex = client->queue_mutex;
        descriptor = client->descriptor;
    }
    else if (obj.object_type == CMSG_OBJ_TYPE_PUB)
    {
        publisher = (cmsg_pub *) obj.object;
        queue = publisher->queue;
        queue_mutex = publisher->queue_mutex;
        descriptor = publisher->descriptor;
    }

    if (g_queue_get_length (queue))
    {
        pthread_mutex_lock (&queue_mutex);
        queue_entry = g_queue_pop_tail (queue);
        pthread_mutex_unlock (&queue_mutex);
    }

    while (queue_entry)
    {
        if (!client)
        {
            //init transport
            if (queue_entry->transport.type == CMSG_TRANSPORT_ONEWAY_TIPC)
            {
                DEBUG (CMSG_INFO, "[PUB QUEUE] queue_entry: transport tipc_init\n");
                cmsg_transport_oneway_tipc_init (&queue_entry->transport);
            }
            else if (queue_entry->transport.type == CMSG_TRANSPORT_ONEWAY_TCP)
            {
                DEBUG (CMSG_INFO, "[PUB QUEUE] queue_entry: transport tipc_init\n");
                cmsg_transport_oneway_tcp_init (&queue_entry->transport);
            }
            else
            {
                DEBUG (CMSG_ERROR,
                       "[PUB QUEUE] queue_entry: transport unknown, transport: %d",
                       queue_entry->transport.type);
            }

            client = cmsg_client_new (&queue_entry->transport, descriptor);

            create_client = 1;
        }

        int c = 0;
        for (c = 0; c <= CMSG_TRANSPORT_CLIENT_SEND_TRIES; c++)
        {
            cmsg_client_connect (client);

            if (client->state == CMSG_CLIENT_STATE_CONNECTED)
            {
                DEBUG (CMSG_INFO, "[PUB QUEUE] sending message to server\n");
                int ret = client->_transport->client_send (client,
                                                           queue_entry->queue_buffer,
                                                           queue_entry->queue_buffer_size,
                                                           0);

                if (ret < queue_entry->queue_buffer_size)
                {
                    DEBUG (CMSG_ERROR,
                           "[PUB QUEUE] sending response failed send:%d of %d, queue message dropped\n",
                           ret, queue_entry->queue_buffer_size);
                }

                client->state = CMSG_CLIENT_STATE_CLOSED;
                client->_transport->client_close (client);

                queue_entry->transport.client_send_tries = 0;

                CMSG_FREE (queue_entry->queue_buffer);
                g_free (queue_entry);
                processed++;
                break;
            }
            else if (client->state == CMSG_CLIENT_STATE_FAILED)
            {
                queue_entry->transport.client_send_tries++;
                DEBUG (CMSG_WARN, "[PUB QUEUE] tries %d\n",
                       queue_entry->transport.client_send_tries);
            }
            else
            {
                syslog (LOG_CRIT | LOG_LOCAL6, "[PUB QUEUE] error: unknown client state\n");
            }
        }

        //handle timeouts
        if (queue_entry->transport.client_send_tries >= CMSG_TRANSPORT_CLIENT_SEND_TRIES)
        {
            if (obj.object_type == CMSG_OBJ_TYPE_PUB)
            {
                //remove subscriber from subscribtion list
                pthread_mutex_lock (&queue_mutex);
                cmsg_pub_subscriber_remove_all_with_transport (publisher,
                                                               &queue_entry->transport);
                pthread_mutex_unlock (&queue_mutex);

                //delete all messages for this subscriber from queue
                pthread_mutex_lock (&queue_mutex);
                cmsg_send_queue_free_all_by_transport (publisher->queue,
                                                       &queue_entry->transport);
                pthread_mutex_unlock (&queue_mutex);
            }
            else if (obj.object_type == CMSG_OBJ_TYPE_CLIENT)
            {
                //delete all messages for this client from queue
                pthread_mutex_lock (&queue_mutex);
                cmsg_send_queue_free_all_by_transport (client->queue,
                                                       &queue_entry->transport);
                pthread_mutex_unlock (&queue_mutex);
            }

            //free current item
            CMSG_FREE (queue_entry->queue_buffer);
            g_free (queue_entry);

            syslog (LOG_CRIT | LOG_LOCAL6,
                    "[PUB QUEUE] error: subscriber not reachable after %d tries, removing subscriber from list\n",
                    CMSG_TRANSPORT_CLIENT_SEND_TRIES);
        }

        if (client && create_client)
        {
            cmsg_client_destroy (client);
            client = NULL;
        }

        //get the next entry
        pthread_mutex_lock (&queue_mutex);
        queue_entry = g_queue_pop_tail (queue);
        pthread_mutex_unlock (&queue_mutex);
    }

    return processed;
}


int32_t
cmsg_send_queue_push (GQueue *queue, uint8_t *buffer, uint32_t buffer_size,
                      cmsg_transport *transport)
{
    cmsg_send_queue_entry *queue_entry = g_malloc (sizeof (cmsg_send_queue_entry));
    if (!queue_entry)
    {
        syslog (LOG_CRIT | LOG_LOCAL6,
                "[CLIENT] error: unable to allocate queue entry. line(%d)\n", __LINE__);
        return CMSG_RET_ERR;
    }

    //copy buffer
    queue_entry->queue_buffer_size = buffer_size;   //should be data + header
    queue_entry->queue_buffer = CMSG_MALLOC (queue_entry->queue_buffer_size);
    if (!queue_entry->queue_buffer)
    {
        syslog (LOG_CRIT | LOG_LOCAL6,
                "[CLIENT] error: unable to allocate queue buffer. line(%d)\n", __LINE__);
        g_free (queue_entry);
        return CMSG_RET_ERR;
    }

    memcpy ((void *) queue_entry->queue_buffer, (void *) buffer,
            queue_entry->queue_buffer_size);

    //copy client transport config
    queue_entry->transport.type = transport->type;
    queue_entry->transport.config.socket.family = transport->config.socket.family;
    queue_entry->transport.config.socket.sockaddr.tipc =
        transport->config.socket.sockaddr.tipc;

    g_queue_push_head (queue, queue_entry);

    return CMSG_RET_OK;
}


void
cmsg_send_queue_free_all (GQueue *queue)
{
    cmsg_send_queue_entry *queue_entry = 0;

    queue_entry = g_queue_pop_tail (queue);

    while (queue_entry)
    {
        CMSG_FREE (queue_entry->queue_buffer);
        g_free (queue_entry);
        //get the next entry
        queue_entry = g_queue_pop_tail (queue);
    }

    g_queue_free (queue);
}

void
cmsg_send_queue_free_all_by_transport (GQueue *queue, cmsg_transport *transport)
{
    cmsg_send_queue_entry *queue_entry = 0;
    unsigned int queue_length = g_queue_get_length (queue);
    int i = 0;

    syslog (LOG_CRIT | LOG_LOCAL6, "[FILTER] queue_length:%d. line(%d)\n", queue_length,
            __LINE__);

    queue_entry = g_queue_pop_tail (queue);

    for (i = 0; i <= queue_length; i++)
    {
        if (queue_entry)
        {
            if (cmsg_transport_compare (&queue_entry->transport, transport))
            {
                CMSG_FREE (queue_entry->queue_buffer);
                g_free (queue_entry);
                syslog (LOG_CRIT | LOG_LOCAL6, "[FILTER] removed entry. line(%d)\n",
                        __LINE__);
            }
            else
            {
                g_queue_push_head (queue, queue_entry);
                syslog (LOG_CRIT | LOG_LOCAL6, "[FILTER] pushed entry back. line(%d)\n",
                        __LINE__);
            }
        }
        queue_entry = g_queue_pop_tail (queue);
    }
}


/*****************************************************************************/
/*****************  Receive Queue Functions  *********************************/
/*****************************************************************************/

int32_t
cmsg_receive_queue_process_one (GQueue *queue, pthread_mutex_t queue_mutex,
                                const ProtobufCServiceDescriptor *descriptor,
                                cmsg_server *server)
{

    // NOT IMPLEMENTED YET
    syslog (LOG_ERR | LOG_LOCAL6, "%s: not implemented yet", __FUNCTION__);

    return 0;
}


/**
 * Process a given number of items on the queue.
 *
 * Assumes that nothing else is processing messages at this time.
 */
int32_t
cmsg_receive_queue_process_some (GQueue *queue, pthread_mutex_t queue_mutex,
                                 const ProtobufCServiceDescriptor *descriptor,
                                 cmsg_server *server, uint32_t num_to_process)
{
    uint32_t processed = 0;
    uint32_t create_client = 0;
    cmsg_receive_queue_entry *queue_entry = 0;
    cmsg_server_request server_request;

    if (num_to_process == 0)
    {
        return 0;
    }

    if (g_queue_get_length (queue) == 0)
    {
        return 0;
    }

    /* Initialise server_request with some dummy values as it is required to be
     * in place by the invoke and closure calls.
     */
    server_request.message_length = 0;
    server->server_request = &server_request;

    // Go through the whole list invoke the server method for the message,
    // freeing the message and moving to the next.
    while (processed < num_to_process)
    {
        //get the first entry
        pthread_mutex_lock (&queue_mutex);
        queue_entry = g_queue_pop_tail (queue);
        pthread_mutex_unlock (&queue_mutex);

        if (queue_entry == NULL)
        {
            break;
        }

        processed++;

        server_request.method_index = queue_entry->method_index;
        cmsg_server_invoke (server, queue_entry->method_index,
                            (ProtobufCMessage *) queue_entry->queue_buffer,
                            CMSG_METHOD_INVOKING_FROM_QUEUE);

        g_free (queue_entry);
        queue_entry = NULL;
    }

    return processed;
}


int32_t
cmsg_receive_queue_process_all (GQueue *queue,
                                pthread_mutex_t queue_mutex,
                                const ProtobufCServiceDescriptor *descriptor,
                                cmsg_server *server)
{
    int32_t processed = -1;
    int32_t total_processed = 0;

    while (processed != 0)
    {
        processed = cmsg_receive_queue_process_some (queue, queue_mutex, descriptor, server,
                                                     50);
        total_processed += processed;
    }
    return total_processed;
}


/**
 * Must be called with the queue lock already held.
 */
int32_t
cmsg_receive_queue_push (GQueue *queue, uint8_t *buffer, uint32_t method_index)
{
    cmsg_receive_queue_entry *queue_entry = g_malloc (sizeof (cmsg_receive_queue_entry));
    if (!queue_entry)
    {
        syslog (LOG_CRIT | LOG_LOCAL6,
                "[SERVER] error: unable to allocate queue entry. line(%d)\n", __LINE__);
        return CMSG_RET_ERR;
    }

    queue_entry->queue_buffer_size = 0; // Unused field

    // Point to the buffer - it will stay allocated until the processor deallocates it.
    queue_entry->queue_buffer = buffer;

    queue_entry->method_index = method_index;

    g_queue_push_head (queue, queue_entry);

    return CMSG_RET_OK;
}


void
cmsg_receive_queue_free_all (GQueue *queue)
{
    cmsg_send_queue_entry *queue_entry = 0;

    queue_entry = g_queue_pop_tail (queue);

    while (queue_entry)
    {
        // ATL_1716_TODO queue_buffer should be freed by the server->allocator as this
        // is how it was done originally
        CMSG_FREE (queue_entry->queue_buffer);   // free the buffer as it won't be processed

        g_free (queue_entry);
        //get the next entry
        queue_entry = g_queue_pop_tail (queue);
    }

    g_queue_free (queue);
}

/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/

guint
cmsg_queue_filter_hash_function (gconstpointer key)
{
    char *string = (char *) key;
    guint hash = 0;
    int i = 0;

    for (i = 0; string[i] != 0; i++)
        hash += (guint) string[i];

    return (guint) hash;
}

gboolean
cmsg_queue_filter_hash_equal_function (gconstpointer a, gconstpointer b)
{
    return (strcmp ((char *) a, (char *) b) == 0);
}

void
cmsg_queue_filter_set_all (GHashTable *queue_filter_hash_table,
                           const ProtobufCServiceDescriptor *descriptor,
                           cmsg_queue_filter_type filter_type)
{
    //add filter for every method with filter type
    //loop through list first and set if not there create entry

    int i = 0;
    for (i = 0; i < descriptor->n_methods; i++)
    {
        cmsg_queue_filter_entry *entry;
        entry = (cmsg_queue_filter_entry *) g_hash_table_lookup (queue_filter_hash_table,
                                                                 (gconstpointer) descriptor->methods[i].name);

        entry->type = filter_type;
    }

}

void
cmsg_queue_filter_clear_all (GHashTable *queue_filter_hash_table,
                             const ProtobufCServiceDescriptor *descriptor)
{
    //remove filter for every method

    int i = 0;
    for (i = 0; i < descriptor->n_methods; i++)
    {
        cmsg_queue_filter_entry *entry;
        entry = (cmsg_queue_filter_entry *) g_hash_table_lookup (queue_filter_hash_table,
                                                                 (gconstpointer) descriptor->methods[i].name);

        entry->type = CMSG_QUEUE_FILTER_PROCESS;
    }
}

int32_t
cmsg_queue_filter_set (GHashTable *queue_filter_hash_table, const char *method,
                       cmsg_queue_filter_type filter_type)
{
    char method_pbc[128];
    sprintf (method_pbc, "%s_pbc", method);

    //add filter for single method with filter type
    cmsg_queue_filter_entry *entry;
    entry = (cmsg_queue_filter_entry *) g_hash_table_lookup (queue_filter_hash_table,
                                                             (gconstpointer) method_pbc);

    if (entry)
    {
        entry->type = filter_type;
        return CMSG_RET_OK;
    }

    return CMSG_RET_ERR;
}

int32_t
cmsg_queue_filter_clear (GHashTable *queue_filter_hash_table, const char *method)
{
    char method_pbc[128];
    sprintf (method_pbc, "%s_pbc", method);

    //clear filter for single method
    cmsg_queue_filter_entry *entry;
    entry = (cmsg_queue_filter_entry *) g_hash_table_lookup (queue_filter_hash_table,
                                                             (gconstpointer) method_pbc);

    if (entry)
    {
        entry->type = CMSG_QUEUE_FILTER_PROCESS;
        return CMSG_RET_OK;
    }

    return CMSG_RET_ERR;
}

void
cmsg_queue_filter_init (GHashTable *queue_filter_hash_table,
                        const ProtobufCServiceDescriptor *descriptor)
{
    //clear filter for single method
    int i = 0;
    for (i = 0; i < descriptor->n_methods; i++)
    {
        cmsg_queue_filter_entry *entry = g_malloc (sizeof (cmsg_queue_filter_entry));
        sprintf (entry->method_name, "%s", descriptor->methods[i].name);
        entry->type = CMSG_QUEUE_FILTER_PROCESS;

        g_hash_table_insert (queue_filter_hash_table,
                             (gpointer) descriptor->methods[i].name, (gpointer) entry);
    }
}

void
cmsg_queue_filter_free (GHashTable *queue_filter_hash_table,
                        const ProtobufCServiceDescriptor *descriptor)
{
    int i = 0;
    for (i = 0; i < descriptor->n_methods; i++)
    {
        cmsg_queue_filter_entry *entry;
        entry = (cmsg_queue_filter_entry *) g_hash_table_lookup (queue_filter_hash_table,
                                                                 (gconstpointer) descriptor->methods[i].name);

        g_free (entry);

        g_hash_table_remove (queue_filter_hash_table,
                             (gconstpointer) descriptor->methods[i].name);
    }
}

cmsg_queue_filter_type
cmsg_queue_filter_lookup (GHashTable *queue_filter_hash_table, const char *method)
{
    //add filter for single method with filter type
    cmsg_queue_filter_entry *entry;
    entry = (cmsg_queue_filter_entry *) g_hash_table_lookup (queue_filter_hash_table,
                                                             (gconstpointer) method);

    if (entry)
    {
        return entry->type;
    }

    return CMSG_QUEUE_FILTER_ERROR;
}

void
cmsg_queue_filter_show (GHashTable *queue_filter_hash_table,
                        const ProtobufCServiceDescriptor *descriptor)
{
    DEBUG (CMSG_INFO, "queue_filter_list:\n");

    int i = 0;
    for (i = 0; i < descriptor->n_methods; i++)
    {
        cmsg_queue_filter_entry *entry;
        entry = (cmsg_queue_filter_entry *) g_hash_table_lookup (queue_filter_hash_table,
                                                                 (gconstpointer) descriptor->methods[i].name);

        switch (entry->type)
        {
        case 0:
            DEBUG (CMSG_INFO, " PROCESS : %s\n", entry->method_name);
            break;
        case 1:
            DEBUG (CMSG_INFO, " DROP    : %s\n", entry->method_name);
            break;
        case 2:
            DEBUG (CMSG_INFO, " QUEUE   : %s\n", entry->method_name);
            break;
        case 3:
            DEBUG (CMSG_INFO, " UNKNOWN : %s\n", entry->method_name);
            break;
        }
    }
}
