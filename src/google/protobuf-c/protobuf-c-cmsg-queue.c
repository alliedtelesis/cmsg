
#include "protobuf-c-cmsg-queue.h"

unsigned int
cmsg_queue_get_length (GQueue *queue)
{
    return g_queue_get_length (queue);
}


int32_t
cmsg_send_queue_process_one (GQueue *queue,
                             pthread_mutex_t queue_mutex,
                             const ProtobufCServiceDescriptor *descriptor,
                             cmsg_client *client)
{
    uint32_t processed = 0;
    uint32_t create_client = 0;
    cmsg_queue_entry *queue_entry = 0;

    if (g_queue_get_length (queue))
    {
        pthread_mutex_lock (&queue_mutex);
        queue_entry = g_queue_pop_tail (queue);
        pthread_mutex_unlock (&queue_mutex);
    }

    if (queue_entry)
    {
        if (!client)
        {
            //init transport
            if (queue_entry->transport.type == CMSG_TRANSPORT_ONEWAY_TIPC)
            {
                DEBUG (CMSG_ERROR, "[PUB QUEUE] queue_entry: transport tipc_init");
                cmsg_transport_oneway_tipc_init (&queue_entry->transport);
            }
            else if (queue_entry->transport.type == CMSG_TRANSPORT_ONEWAY_TCP)
            {
                DEBUG (CMSG_ERROR, "[PUB QUEUE] queue_entry: transport tcp_init");
                cmsg_transport_oneway_tcp_init (&queue_entry->transport);
            }
            else
            {
                DEBUG (CMSG_ERROR, "[PUB QUEUE] queue_entry: transport unknown");
            }

            client = cmsg_client_new (&queue_entry->transport, descriptor);

            create_client = 1;
        }

        cmsg_client_connect (client);

        if (client->state == CMSG_CLIENT_STATE_CONNECTED)
        {
            DEBUG (CMSG_INFO, "[PUB QUEUE] sending message to server\n");
            int ret = client->transport->client_send (client,
                                                      queue_entry->queue_buffer,
                                                      queue_entry->queue_buffer_size,
                                                      0);

            if (ret < queue_entry->queue_buffer_size)
                DEBUG (CMSG_ERROR,
                       "[PUB QUEUE] sending response failed send:%d of %d\n",
                       ret, queue_entry->queue_buffer_size);

            client->state = CMSG_CLIENT_STATE_DESTROYED;
            client->transport->client_close (client);

            free (queue_entry->queue_buffer);
            g_free (queue_entry);

            processed++;
        }
        else
        {
            DEBUG (CMSG_ERROR, "[PUB QUEUE] error: client is not connected, requeueing message\n");
            pthread_mutex_lock (&queue_mutex);
            g_queue_push_head (queue, queue_entry);
            pthread_mutex_unlock (&queue_mutex);
        }

        if (create_client)
            cmsg_client_destroy (client);
    }

    return processed;
}


int32_t
cmsg_send_queue_process_all (GQueue *queue,
                             pthread_mutex_t queue_mutex,
                             const ProtobufCServiceDescriptor *descriptor,
                             cmsg_client *client)
{
    uint32_t processed = 0;
    uint32_t create_client = 0;
    cmsg_queue_entry *queue_entry = 0;

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

        cmsg_client_connect (client);

        if (client->state == CMSG_CLIENT_STATE_CONNECTED)
        {
            DEBUG (CMSG_INFO, "[PUB QUEUE] sending message to server\n");
            int ret = client->transport->client_send (client,
                                                      queue_entry->queue_buffer,
                                                      queue_entry->queue_buffer_size,
                                                      0);

            if (ret < queue_entry->queue_buffer_size)
            {
                DEBUG (CMSG_ERROR,
                       "[PUB QUEUE] sending response failed send:%d of %d, queue message dropped\n",
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
            DEBUG (CMSG_ERROR, "[PUB QUEUE] error: client is not connected, requeueing message\n");
            pthread_mutex_lock (&queue_mutex);
            g_queue_push_head (queue, queue_entry);
            pthread_mutex_unlock (&queue_mutex);

            unsigned int queue_length = g_queue_get_length (queue);
            DEBUG (CMSG_ERROR, "[PUB QUEUE] queue length: %d\n", queue_length);

            if (client && create_client)
            {
                cmsg_client_destroy (client);
                client = NULL;
            }

            int sleep_time = rand () % 2000000 + 300000;

            DEBUG (CMSG_ERROR, "[PUB QUEUE] retrying in: %d seconds\n", sleep_time);
            usleep (sleep_time);
            DEBUG (CMSG_ERROR, "[PUB QUEUE] sleeping done\n");

            return CMSG_RET_ERR;
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
cmsg_send_queue_push (GQueue *queue,
                      uint8_t *buffer,
                      uint32_t buffer_size,
                      cmsg_transport *transport)
{
    cmsg_queue_entry *queue_entry = g_malloc (sizeof (cmsg_queue_entry));
    if (!queue_entry)
    {
        syslog (LOG_CRIT | LOG_LOCAL6, "[CLIENT] error: unable to allocate queue entry. line(%d)\n", __LINE__);
        return CMSG_RET_ERR;
    }

    //copy buffer
    queue_entry->queue_buffer_size = buffer_size; //should be data + header
    queue_entry->queue_buffer = malloc (queue_entry->queue_buffer_size);
    if (!queue_entry->queue_buffer)
    {
        syslog (LOG_CRIT | LOG_LOCAL6, "[CLIENT] error: unable to allocate queue buffer. line(%d)\n", __LINE__);
        g_free (queue_entry);
        return CMSG_RET_ERR;
    }

    memcpy ((void *)queue_entry->queue_buffer, (void *)buffer, queue_entry->queue_buffer_size);

    //copy client transport config
    queue_entry->transport.type = transport->type;
    queue_entry->transport.config.socket.family = transport->config.socket.family;
    queue_entry->transport.config.socket.sockaddr.tipc = transport->config.socket.sockaddr.tipc;

    g_queue_push_head (queue, queue_entry);

    return CMSG_RET_OK;
}


void
cmsg_send_queue_free_all (GQueue *queue)
{
    cmsg_queue_entry *queue_entry = 0;

    queue_entry = g_queue_pop_tail (queue);

    while (queue_entry)
    {
        free (queue_entry->queue_buffer);
        g_free (queue_entry);
        //get the next entry
        queue_entry = g_queue_pop_tail (queue);
    }

    g_queue_free (queue);
}

guint
cmsg_queue_filter_hash_function (gconstpointer key)
{
    char *string = (char *)key;
    guint hash = 0;
    int i = 0;

    for (i = 0; string[i] != 0; i++)
        hash += (guint)string[i];

    return (guint)hash;
}

gboolean
cmsg_queue_filter_hash_equal_function (gconstpointer a, gconstpointer b)
{
    return (strcmp ((char *)a, (char *)b) == 0);
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
        entry = (cmsg_queue_filter_entry *)g_hash_table_lookup (queue_filter_hash_table,
                                                                (gconstpointer)descriptor->methods[i].name);

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
        entry = (cmsg_queue_filter_entry *)g_hash_table_lookup (queue_filter_hash_table,
                                                                (gconstpointer)descriptor->methods[i].name);

        entry->type = CMSG_QUEUE_FILTER_PROCESS;
    }
}

int32_t
cmsg_queue_filter_set (GHashTable *queue_filter_hash_table,
                       const char *method,
                       cmsg_queue_filter_type filter_type)
{
    char method_pbc[128];
    sprintf (method_pbc, "%s_pbc", method);

    //add filter for single method with filter type
    cmsg_queue_filter_entry *entry;
    entry = (cmsg_queue_filter_entry *)g_hash_table_lookup (queue_filter_hash_table,
                                                            (gconstpointer)method_pbc);

    if (entry)
    {
        entry->type = filter_type;
        return CMSG_RET_OK;
    }

    return CMSG_RET_ERR;
}

int32_t
cmsg_queue_filter_clear (GHashTable *queue_filter_hash_table,
                         const char *method)
{
    char method_pbc[128];
    sprintf (method_pbc, "%s_pbc", method);

    //clear filter for single method
    cmsg_queue_filter_entry *entry;
    entry = (cmsg_queue_filter_entry *)g_hash_table_lookup (queue_filter_hash_table,
                                                            (gconstpointer)method_pbc);

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
                             (gpointer)descriptor->methods[i].name,
                             (gpointer)entry);
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
        entry = (cmsg_queue_filter_entry *)g_hash_table_lookup (queue_filter_hash_table,
                                                                (gconstpointer)descriptor->methods[i].name);

        g_free (entry);

        g_hash_table_remove (queue_filter_hash_table,
                             (gconstpointer)descriptor->methods[i].name);
    }
}

cmsg_queue_filter_type
cmsg_queue_filter_lookup (GHashTable *queue_filter_hash_table,
                          const char *method)
{
    //add filter for single method with filter type
    cmsg_queue_filter_entry *entry;
    entry = (cmsg_queue_filter_entry *)g_hash_table_lookup (queue_filter_hash_table,
                                                            (gconstpointer)method);

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
        entry = (cmsg_queue_filter_entry *)g_hash_table_lookup (queue_filter_hash_table,
                                                                (gconstpointer)descriptor->methods[i].name);

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

