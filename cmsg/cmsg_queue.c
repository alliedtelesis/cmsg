#include "cmsg_private.h"
#include "cmsg_queue.h"
#include "cmsg_error.h"

uint32_t
cmsg_queue_get_length (GQueue *queue)
{
    return g_queue_get_length (queue);
}

int32_t
cmsg_send_queue_push (GQueue *queue, uint8_t *buffer, uint32_t buffer_size,
                      cmsg_client *client, cmsg_transport *transport, char *method_name)
{

    cmsg_send_queue_entry *queue_entry = NULL;
    queue_entry = (cmsg_send_queue_entry *) CMSG_CALLOC (1, sizeof (cmsg_send_queue_entry));
    if (!queue_entry)
    {
        CMSG_LOG_CLIENT_ERROR (client, "Unable to allocate queue entry. Method:%s",
                               method_name);
        return CMSG_RET_ERR;
    }

    //copy buffer
    queue_entry->queue_buffer_size = buffer_size;   //should be data + header
    queue_entry->queue_buffer = (uint8_t *) CMSG_CALLOC (1, queue_entry->queue_buffer_size);
    if (!queue_entry->queue_buffer)
    {
        CMSG_LOG_CLIENT_ERROR (client, "Unable to allocate queue buffer. Method:%s",
                               method_name);
        CMSG_FREE (queue_entry);
        return CMSG_RET_ERR;
    }

    memcpy ((void *) queue_entry->queue_buffer, (void *) buffer,
            queue_entry->queue_buffer_size);

    queue_entry->client = client;
    queue_entry->transport = transport;
    strcpy (queue_entry->method_name, method_name ? method_name : "");
    g_queue_push_head (queue, queue_entry);

    return CMSG_RET_OK;
}


void
cmsg_send_queue_free_all (GQueue *queue)
{
    cmsg_send_queue_entry *queue_entry = 0;

    queue_entry = (cmsg_send_queue_entry *) g_queue_pop_tail (queue);

    while (queue_entry)
    {
        CMSG_FREE (queue_entry->queue_buffer);
        CMSG_FREE (queue_entry);
        //get the next entry
        queue_entry = (cmsg_send_queue_entry *) g_queue_pop_tail (queue);
    }

    g_queue_free (queue);
}

void
cmsg_send_queue_free_all_by_transport (GQueue *queue, cmsg_transport *transport)
{
    cmsg_send_queue_entry *queue_entry = 0;
    uint32_t queue_length = g_queue_get_length (queue);
    uint32_t i = 0;

    for (i = 0; i < queue_length; i++)
    {
        queue_entry = (cmsg_send_queue_entry *) g_queue_pop_tail (queue);
        if (queue_entry)
        {
            if (cmsg_transport_compare (queue_entry->transport, transport))
            {
                CMSG_FREE (queue_entry->queue_buffer);
                CMSG_FREE (queue_entry);
            }
            else
            {
                g_queue_push_head (queue, queue_entry);
            }
        }
    }
}

void
cmsg_send_queue_free_by_transport_method (GQueue *queue, cmsg_transport *transport,
                                          char *method_name)
{
    cmsg_send_queue_entry *queue_entry = 0;
    uint32_t queue_length = g_queue_get_length (queue);
    uint32_t i = 0;

    for (i = 0; i < queue_length; i++)
    {
        queue_entry = (cmsg_send_queue_entry *) g_queue_pop_tail (queue);
        if (queue_entry)
        {
            if (cmsg_transport_compare (queue_entry->transport, transport) &&
                (strcmp (queue_entry->method_name, method_name) == 0))
            {
                CMSG_FREE (queue_entry->queue_buffer);
                CMSG_FREE (queue_entry);
            }
            else
            {
                g_queue_push_head (queue, queue_entry);
            }
        }
    }
}


/*****************************************************************************/
/*****************  Receive Queue Functions  *********************************/
/*****************************************************************************/

int32_t
cmsg_receive_queue_process_one (GQueue *queue, pthread_mutex_t *queue_mutex,
                                const ProtobufCServiceDescriptor *descriptor,
                                cmsg_server *server)
{

    // NOT IMPLEMENTED YET
    CMSG_LOG_SERVER_ERROR (server, "%s not implemented.", __FUNCTION__);

    return 0;
}


/**
 * Process a given number of items on the queue.
 *
 * Assumes that nothing else is processing messages at this time.
 */
int32_t
cmsg_receive_queue_process_some (GQueue *queue, pthread_mutex_t *queue_mutex,
                                 const ProtobufCServiceDescriptor *descriptor,
                                 cmsg_server *server, uint32_t num_to_process)
{
    uint32_t processed = 0;
    cmsg_receive_queue_entry *queue_entry = 0;
    cmsg_server_request server_request;
    uint32_t queue_length = 0;

    if (num_to_process == 0)
    {
        return 0;
    }

    pthread_mutex_lock (queue_mutex);
    queue_length = g_queue_get_length (queue);
    pthread_mutex_unlock (queue_mutex);

    if (queue_length == 0)
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
        pthread_mutex_lock (queue_mutex);
        queue_entry = (cmsg_receive_queue_entry *) g_queue_pop_tail (queue);
        pthread_mutex_unlock (queue_mutex);

        if (queue_entry == NULL)
        {
            break;
        }

        processed++;

        server_request.method_index = queue_entry->method_index;
        cmsg_server_invoke (server, queue_entry->method_index,
                            (ProtobufCMessage *) queue_entry->queue_buffer,
                            CMSG_METHOD_INVOKING_FROM_QUEUE);

        CMSG_FREE (queue_entry);
        queue_entry = NULL;
    }

    return processed;
}


int32_t
cmsg_receive_queue_process_all (GQueue *queue,
                                pthread_mutex_t *queue_mutex,
                                const ProtobufCServiceDescriptor *descriptor,
                                cmsg_server *server)
{
    int32_t processed = -1;
    int32_t total_processed = 0;

    while (processed != 0)
    {
        processed = cmsg_receive_queue_process_some (queue,
                                                     queue_mutex,
                                                     descriptor,
                                                     server,
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
    cmsg_receive_queue_entry *queue_entry = (cmsg_receive_queue_entry *) CMSG_CALLOC (1, sizeof (cmsg_receive_queue_entry));
    if (!queue_entry)
    {
        CMSG_LOG_GEN_ERROR ("Unable to allocate queue entry. Method index:%d",
                            method_index);
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
    cmsg_receive_queue_entry *queue_entry = 0;
    queue_entry = (cmsg_receive_queue_entry *) g_queue_pop_tail (queue);

    while (queue_entry)
    {
        // ATL_1716_TODO queue_buffer should be freed by the server->allocator as this
        // is how it was done originally
        CMSG_FREE (queue_entry->queue_buffer);  // free the buffer as it won't be processed

        CMSG_FREE (queue_entry);
        //get the next entry
        queue_entry = (cmsg_receive_queue_entry *) g_queue_pop_tail (queue);
    }

    g_queue_free (queue);
}

/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/

void
cmsg_queue_filter_set_all (GHashTable *queue_filter_hash_table,
                           const ProtobufCServiceDescriptor *descriptor,
                           cmsg_queue_filter_type filter_type)
{
    //add filter for every method with filter type
    //loop through list first and set if not there create entry

    uint32_t i = 0;
    for (i = 0; i < descriptor->n_methods; i++)
    {
        cmsg_queue_filter_entry *entry;
        entry = (cmsg_queue_filter_entry *) g_hash_table_lookup (queue_filter_hash_table,
                                                                 (gconstpointer)
                                                                 descriptor->methods[i].name);

        entry->type = filter_type;
    }

}

void
cmsg_queue_filter_clear_all (GHashTable *queue_filter_hash_table,
                             const ProtobufCServiceDescriptor *descriptor)
{
    //remove filter for every method

    uint32_t i = 0;
    for (i = 0; i < descriptor->n_methods; i++)
    {
        cmsg_queue_filter_entry *entry;
        entry = (cmsg_queue_filter_entry *) g_hash_table_lookup (queue_filter_hash_table,
                                                                 (gconstpointer)
                                                                 descriptor->methods[i].name);

        entry->type = CMSG_QUEUE_FILTER_PROCESS;
    }
}

int32_t
cmsg_queue_filter_set (GHashTable *queue_filter_hash_table, const char *method,
                       cmsg_queue_filter_type filter_type)
{
    //add filter for single method with filter type
    cmsg_queue_filter_entry *entry;
    entry = (cmsg_queue_filter_entry *) g_hash_table_lookup (queue_filter_hash_table, method);

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
    //clear filter for single method
    cmsg_queue_filter_entry *entry;
    entry = (cmsg_queue_filter_entry *) g_hash_table_lookup (queue_filter_hash_table, method);

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
    uint32_t i = 0;
    for (i = 0; i < descriptor->n_methods; i++)
    {
        cmsg_queue_filter_entry *entry = (cmsg_queue_filter_entry *) CMSG_CALLOC (1, sizeof (cmsg_queue_filter_entry));
        sprintf (entry->method_name, "%s", descriptor->methods[i].name);
        entry->type = CMSG_QUEUE_FILTER_PROCESS;

        g_hash_table_insert (queue_filter_hash_table,
                             (gpointer) descriptor->methods[i].name, entry);
    }
}

void
cmsg_queue_filter_free (GHashTable *queue_filter_hash_table,
                        const ProtobufCServiceDescriptor *descriptor)
{
    uint32_t i = 0;
    for (i = 0; i < descriptor->n_methods; i++)
    {
        cmsg_queue_filter_entry *entry;
        entry = (cmsg_queue_filter_entry *) g_hash_table_lookup (queue_filter_hash_table,
                                                                 (gconstpointer)
                                                                 descriptor->methods[i].name);

        CMSG_FREE (entry);

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
    CMSG_DEBUG (CMSG_INFO, "queue_filter_list:\n");

    uint32_t i = 0;
    for (i = 0; i < descriptor->n_methods; i++)
    {
        cmsg_queue_filter_entry *entry;
        entry = (cmsg_queue_filter_entry *) g_hash_table_lookup (queue_filter_hash_table,
                                                                 (gconstpointer)
                                                                 descriptor->methods[i].name);

        switch (entry->type)
        {
        case 0:
            CMSG_DEBUG (CMSG_INFO, " PROCESS : %s\n", entry->method_name);
            break;
        case 1:
            CMSG_DEBUG (CMSG_INFO, " DROP    : %s\n", entry->method_name);
            break;
        case 2:
            CMSG_DEBUG (CMSG_INFO, " QUEUE   : %s\n", entry->method_name);
            break;
        case 3:
            CMSG_DEBUG (CMSG_INFO, " UNKNOWN : %s\n", entry->method_name);
            break;
        }
    }
}

cmsg_queue_state
cmsg_queue_filter_get_type (GHashTable *queue_filter_hash_table,
                            const ProtobufCServiceDescriptor *descriptor)
{
    cmsg_queue_state type = CMSG_QUEUE_STATE_DISABLED;
    uint32_t i = 0;

    for (i = 0; i < descriptor->n_methods; i++)
    {
        cmsg_queue_filter_entry *entry;
        entry = (cmsg_queue_filter_entry *) g_hash_table_lookup (queue_filter_hash_table,
                                                                 (gconstpointer)
                                                                 descriptor->methods[i].name);

        if (entry->type == CMSG_QUEUE_FILTER_QUEUE)
        {
            type = CMSG_QUEUE_STATE_ENABLED;

        }
    }
    return type;
}


int32_t
cmsg_queue_filter_copy (GHashTable *src_queue_filter_hash_table,
                        GHashTable *dst_queue_filter_hash_table,
                        const ProtobufCServiceDescriptor *descriptor)
{
    uint32_t i = 0;
    for (i = 0; i < descriptor->n_methods; i++)
    {
        cmsg_queue_filter_entry *src_entry;
        cmsg_queue_filter_entry *dst_entry;

        src_entry =
            (cmsg_queue_filter_entry *) g_hash_table_lookup (src_queue_filter_hash_table,
                                                             (gconstpointer)
                                                             descriptor->methods[i].name);

        dst_entry =
            (cmsg_queue_filter_entry *) g_hash_table_lookup (dst_queue_filter_hash_table,
                                                             (gconstpointer)
                                                             descriptor->methods[i].name);

        if (!src_entry || !dst_entry)
            return CMSG_RET_ERR;

        dst_entry = src_entry;
    }

    return CMSG_RET_OK;
}
