/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#include "cmsg_private.h"
#include "cmsg_queue.h"
#include "cmsg_error.h"

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
}


void
cmsg_send_queue_destroy (GQueue *queue)
{
    cmsg_send_queue_free_all (queue);

    g_queue_free (queue);
}

/**
 * Wrapper function to remove and free all messages for a specific transport
 * and method name (if specified).
 * If 'exact' is true, then only the messages having the exact same transport
 * are removed.
 * If 'exact is false, then all messages for the application of the specified transport
 * are removed. This is useful if sending a message to an application fails, then
 * assume the application is not reachable and remove all messages for the application
 * including message for other subscriptions.
 * @param queue         CMSG queue to remove messages from
 * @param transport     CMSG transport to search
 * @param method_name   Message name to search
 * @param exact         Indicate whether transport comparison to be exact match or not
 */
static void
_cmsg_send_queue_free_all_by_transport_method (GQueue *queue, cmsg_transport *transport,
                                               const char *method_name, bool exact)
{
    cmsg_send_queue_entry *queue_entry = 0;
    uint32_t queue_length = g_queue_get_length (queue);
    uint32_t i = 0;

    for (i = 0; i < queue_length; i++)
    {
        queue_entry = (cmsg_send_queue_entry *) g_queue_pop_tail (queue);
        if (queue_entry)
        {
            /* For non-exact match, find any transport from the same application.
             * If method name is specified, then check the method name of the queue entry
             * is same as the specified one */
            if (((exact && queue_entry->transport == transport) ||
                 (!exact && cmsg_transport_compare (queue_entry->transport, transport))) &&
                (!method_name || strcmp (queue_entry->method_name, method_name) == 0))
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

/**
 * Remove and free all messages for the application identified by the specified transport.
 * This means if an application has subscribed multiple methods, then the messages
 * using different transport but heading to the same application are removed and freed.
 * @param queue     CMSG queue to remove messages from
 * @param transport CMSG transport
 */
void
cmsg_send_queue_free_all_by_transport (GQueue *queue, cmsg_transport *transport)
{
    _cmsg_send_queue_free_all_by_transport_method (queue, transport, NULL, false);
}

/**
 * Remove and free the messages for the specified transport from the queue.
 * Messages to be sent to the same application but for different subscription
 * will not be touched.
 * @param queue     CMSG queue to remove messages from
 * @param transport CMSG transport
 */
void
cmsg_send_queue_free_all_by_single_transport (GQueue *queue, cmsg_transport *transport)
{
    _cmsg_send_queue_free_all_by_transport_method (queue, transport, NULL, true);
}

/**
 * Remove and free all messages for the application identified by the specified transport
 * and method name.
 * This means if an application has subscribed multiple methods, then the messages
 * using different transport but heading to the same application are removed and freed.
 * @param queue     CMSG queue to remove messages from
 * @param transport CMSG transport
 * @param method_name   Method name
 */
void
cmsg_send_queue_free_by_transport_method (GQueue *queue, cmsg_transport *transport,
                                          char *method_name)
{
    _cmsg_send_queue_free_all_by_transport_method (queue, transport, method_name, false);
}

/**
 * Remove and free the messages for the specified transport and method name from the queue.
 * Messages to be sent to the same application but for different subscription
 * will not be touched.
 * @param queue     CMSG queue to remove messages from
 * @param transport CMSG transport
 * @param method_name   Method name
 */
void
cmsg_send_queue_free_by_single_transport_method (GQueue *queue, cmsg_transport *transport,
                                                 char *method_name)
{
    _cmsg_send_queue_free_all_by_transport_method (queue, transport, method_name, true);
}


/*****************************************************************************/
/*****************  Receive Queue Functions  *********************************/
/*****************************************************************************/

/**
 * Process a given number of items on the queue.
 *
 * Assumes that nothing else is processing messages at this time.
 */
int32_t
cmsg_receive_queue_process_some (GQueue *queue, pthread_mutex_t *queue_mutex,
                                 cmsg_server *server, uint32_t num_to_process)
{
    uint32_t processed = 0;
    cmsg_receive_queue_entry *queue_entry = 0;
    cmsg_server_request server_request;
    uint32_t queue_length = 0;
    int socket;

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

    /* Initialise the socket value, it doesn't matter as when we invoke from a
     * server queue we don't actually send a reply on the socket. */
    socket = -1;

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
        cmsg_server_invoke (socket, &server_request, server,
                            queue_entry->method_index,
                            (ProtobufCMessage *) queue_entry->queue_buffer,
                            CMSG_METHOD_INVOKING_FROM_QUEUE);

        CMSG_FREE (queue_entry);
        queue_entry = NULL;
    }

    return processed;
}


int32_t
cmsg_receive_queue_process_all (GQueue *queue, pthread_mutex_t *queue_mutex,
                                cmsg_server *server)
{
    int32_t processed = -1;
    int32_t total_processed = 0;

    while (processed != 0)
    {
        processed = cmsg_receive_queue_process_some (queue, queue_mutex, server, 50);
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
    cmsg_receive_queue_entry *queue_entry =
        (cmsg_receive_queue_entry *) CMSG_CALLOC (1, sizeof (cmsg_receive_queue_entry));
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
        // ATL_1716_TODO queue_buffer should be freed by the cmsg_memory_allocator as this
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
                                                                 descriptor->
                                                                 methods[i].name);

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
                                                                 descriptor->
                                                                 methods[i].name);

        entry->type = CMSG_QUEUE_FILTER_PROCESS;
    }
}

int32_t
cmsg_queue_filter_set (GHashTable *queue_filter_hash_table, const char *method,
                       cmsg_queue_filter_type filter_type)
{
    //add filter for single method with filter type
    cmsg_queue_filter_entry *entry;
    entry =
        (cmsg_queue_filter_entry *) g_hash_table_lookup (queue_filter_hash_table, method);

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
    entry =
        (cmsg_queue_filter_entry *) g_hash_table_lookup (queue_filter_hash_table, method);

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
        cmsg_queue_filter_entry *entry =
            (cmsg_queue_filter_entry *) CMSG_CALLOC (1, sizeof (cmsg_queue_filter_entry));
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
                                                                 descriptor->
                                                                 methods[i].name);

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
                                                                 descriptor->
                                                                 methods[i].name);

        if (entry->type == CMSG_QUEUE_FILTER_QUEUE)
        {
            type = CMSG_QUEUE_STATE_ENABLED;

        }
    }
    return type;
}
