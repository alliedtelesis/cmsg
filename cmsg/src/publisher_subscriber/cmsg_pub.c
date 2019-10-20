/**
 * cmsg_pub.c
 *
 * Implements the CMSG publisher which can be used to publish messages
 * to interested subscribers.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include "cmsg_pub_private.h"
#include "cmsg_pub.h"
#include "cmsg_ps_api_private.h"
#include "cmsg_error.h"
#include "cmsg_pthread_helpers.h"
#include "update_impl_auto.h"
#include "transport/cmsg_transport_private.h"
#include "cmsg_composite_client.h"
#include "cmsg_client_private.h"

typedef struct
{
    char *method_name;
    uint32_t packet_len;
    uint8_t *packet;
} cmsg_pub_queue_entry;

static const ProtobufCServiceDescriptor cmsg_psd_pub_descriptor = {
    PROTOBUF_C__SERVICE_DESCRIPTOR_MAGIC,
    "cmsg_psd.pub",
    "pub",
    "cmsg_psd_pub",
    "cmsg_psd",
    0,
    NULL,
    NULL,
};

/**
 * Get the composite client for the subscribers of the given method, or
 * optionally create it first if it does not already exist.
 *
 * @param publisher - The publisher to get the composite client from.
 * @param method_name - The method name to get the composite client for.
 * @param create - Whether to create the composite client if one didn't already
 *                 exist or not.
 *
 * @returns A pointer to the composite client or NULL.
 */
static cmsg_client *
cmsg_publisher_get_client_for_method (cmsg_publisher *publisher, const char *method_name,
                                      bool create)
{
    cmsg_client *client = NULL;

    client = (cmsg_client *) g_hash_table_lookup (publisher->subscribed_methods,
                                                  method_name);
    if (!client && create)
    {
        client = cmsg_composite_client_new (&cmsg_psd_pub_descriptor);
        g_hash_table_insert (publisher->subscribed_methods, CMSG_STRDUP (method_name),
                             client);
    }

    return client;
}

/**
 * Queue a CMSG packet on the send queue of the publisher so that it
 * can be sent by the send thread.
 *
 * @param publisher - The publisher to queue the packet for.
 * @param packet - The packet to queue.
 * @param packet_len - The length of the packet.
 * @param method_name - The name of the method the packet is for.
 *
 * @returns CMSG_RET_OK on success, CMSG_RET_ERR on failure.
 */
static int32_t
cmsg_publisher_queue_packet (cmsg_publisher *publisher, uint8_t *packet,
                             uint32_t packet_len, const char *method_name)
{
    cmsg_pub_queue_entry *queue_entry = NULL;

    queue_entry = (cmsg_pub_queue_entry *) CMSG_CALLOC (1, sizeof (*queue_entry));
    if (!queue_entry)
    {
        return CMSG_RET_ERR;
    }

    queue_entry->packet = packet;
    queue_entry->packet_len = packet_len;
    queue_entry->method_name = CMSG_STRDUP (method_name);

    if (!queue_entry->method_name)
    {
        return CMSG_RET_ERR;
    }

    pthread_mutex_lock (&publisher->send_queue_mutex);
    g_queue_push_head (publisher->send_queue, queue_entry);
    pthread_cond_signal (&publisher->send_queue_process_cond);
    pthread_mutex_unlock (&publisher->send_queue_mutex);

    return CMSG_RET_OK;
}

/**
 * Free the memory used by a 'cmsg_pub_queue_entry' structure.
 *
 * @param queue_entry - The queue entry to free.
 */
static void
cmsg_publisher_queue_entry_free (cmsg_pub_queue_entry *queue_entry)
{
    CMSG_FREE (queue_entry->packet);
    CMSG_FREE (queue_entry->method_name);
    CMSG_FREE (queue_entry);
}

/**
 * Invoke function for the cmsg publisher. Simply creates the cmsg packet
 * for the given message and sends this to cmsg_psd to be published to all
 * subscribers.
 */
static int32_t
cmsg_pub_invoke (ProtobufCService *service,
                 uint32_t method_index,
                 const ProtobufCMessage *input,
                 ProtobufCClosure closure, void *closure_data)
{
    int32_t ret;
    cmsg_publisher *publisher = (cmsg_publisher *) service;
    const char *method_name;
    uint8_t *packet = NULL;
    uint32_t total_message_size = 0;
    cmsg_client *subscribers_client = NULL;

    CMSG_ASSERT_RETURN_VAL (service != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (service->descriptor != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (input != NULL, CMSG_RET_ERR);

    method_name = service->descriptor->methods[method_index].name;

    pthread_mutex_lock (&publisher->subscribed_methods_mutex);

    subscribers_client = cmsg_publisher_get_client_for_method (publisher, method_name,
                                                               false);

    /* If there are no subscribers for this method then simply return */
    if (!subscribers_client)
    {
        pthread_mutex_unlock (&publisher->subscribed_methods_mutex);
        return CMSG_RET_OK;
    }

    ret = cmsg_client_create_packet (subscribers_client, method_name, input,
                                     &packet, &total_message_size);

    pthread_mutex_unlock (&publisher->subscribed_methods_mutex);

    if (ret != CMSG_RET_OK)
    {
        return CMSG_RET_ERR;
    }

    ret = cmsg_publisher_queue_packet (publisher, packet, total_message_size, method_name);
    if (ret != CMSG_RET_OK)
    {
        CMSG_FREE (packet);
        return CMSG_RET_ERR;
    }

    return CMSG_RET_OK;
}

/**
 * Helper function called for a list of cmsg clients. Compares each client
 * with the given transport.
 *
 * @param a - The client.
 * @param b - The transport.
 *
 * @returns 0 if the given transport matches the transport of the client.
 *          -1 otherwise.
 */
static gint
cmsg_client_transport_compare (gconstpointer a, gconstpointer b)
{
    const cmsg_client *client = (const cmsg_client *) a;
    const cmsg_transport *transport = (const cmsg_transport *) b;

    if (cmsg_transport_compare (client->_transport, transport))
    {
        return 0;
    }

    return -1;
}

/**
 * Add a subscriber to the publisher.
 *
 * @param publisher - The publisher to add the subscriber to.
 * @param method_name - The name of the method the subscriber has subscribed to.
 * @param transport_info - The transport information for the subscriber.
 */
static void
cmsg_publisher_add_subscriber (cmsg_publisher *publisher, const char *method_name,
                               cmsg_transport_info *transport_info)
{
    cmsg_client *comp_client = NULL;
    cmsg_transport *transport = NULL;
    cmsg_client *client = NULL;

    comp_client = cmsg_publisher_get_client_for_method (publisher, method_name, true);

    transport = cmsg_transport_info_to_transport (transport_info);
    client = cmsg_client_create (transport, &cmsg_psd_pub_descriptor);

    cmsg_composite_client_add_child (comp_client, client);
}

/**
 * Remove a subscriber from the publisher.
 *
 * @param publisher - The publisher to remove the subscriber from.
 * @param method_name - The name of the method the subscriber was subscribed to.
 * @param transport_info - The transport information for the subscriber.
 */
static void
cmsg_publisher_remove_subscriber (cmsg_publisher *publisher, const char *method_name,
                                  cmsg_transport_info *transport_info)
{
    cmsg_client *comp_client = NULL;
    GList *client_list = NULL;
    cmsg_transport *transport = NULL;
    cmsg_client *child_client = NULL;
    GList *list_entry = NULL;

    comp_client = cmsg_publisher_get_client_for_method (publisher, method_name, false);
    if (comp_client)
    {
        client_list = cmsg_composite_client_get_children (comp_client);
        transport = cmsg_transport_info_to_transport (transport_info);

        list_entry = g_list_find_custom (client_list, transport,
                                         cmsg_client_transport_compare);
        if (list_entry)
        {
            child_client = (cmsg_client *) list_entry->data;
            cmsg_composite_client_delete_child (comp_client, child_client);
            cmsg_destroy_client_and_transport (child_client);
        }
        cmsg_transport_destroy (transport);
    }

    if (cmsg_composite_client_num_children (comp_client) == 0)
    {
        g_hash_table_remove (publisher->subscribed_methods, method_name);
    }
}

/**
 * Initialises the subscribers for this publisher. The publisher first
 * registers itself with cmsg_psd and is returned the current subscribers
 * for the service the publisher is publishing for.
 *
 * @param publisher - The publisher to initialise the subscribers for.
 *
 * @returns CMSG_RET_OK on success, CMSG_RET_ERR otherwise.
 */
static int32_t
cmsg_publisher_init_subscribers (cmsg_publisher *publisher)
{
    int32_t ret;
    cmsg_subscription_methods *subscribed_methods = NULL;
    cmsg_subscription_method_entry *entry = NULL;
    cmsg_transport_info *transport = NULL;
    int i, j;
    const char *service_name = NULL;

    service_name = cmsg_service_name_get (publisher->descriptor);

    pthread_mutex_lock (&publisher->subscribed_methods_mutex);

    ret = cmsg_ps_register_publisher (service_name, publisher->update_server,
                                      &subscribed_methods);
    if (ret == CMSG_RET_OK)
    {
        CMSG_REPEATED_FOREACH (subscribed_methods, methods, entry, i)
        {
            CMSG_REPEATED_FOREACH (entry, transports, transport, j)
            {
                cmsg_publisher_add_subscriber (publisher, entry->method_name, transport);
            }
        }
        CMSG_FREE_RECV_MSG (subscribed_methods);
    }

    pthread_mutex_unlock (&publisher->subscribed_methods_mutex);

    return ret;
}

/**
 * Process all messages that are currently on the send queue and
 * send them to the subscribers.
 *
 * @param publisher - The publisher to process queued messages for.
 */
static void
cmsg_publisher_process_send_queue (cmsg_publisher *publisher)
{
    cmsg_pub_queue_entry *queue_entry = NULL;
    cmsg_client *client = NULL;

    pthread_mutex_lock (&publisher->send_queue_mutex);
    queue_entry = (cmsg_pub_queue_entry *) g_queue_pop_tail (publisher->send_queue);
    pthread_mutex_unlock (&publisher->send_queue_mutex);

    while (queue_entry)
    {
        pthread_mutex_lock (&publisher->subscribed_methods_mutex);
        client = cmsg_publisher_get_client_for_method (publisher, queue_entry->method_name,
                                                       false);

        if (client)
        {
            cmsg_client_send_bytes (client, queue_entry->packet, queue_entry->packet_len,
                                    queue_entry->method_name);
        }

        pthread_mutex_unlock (&publisher->subscribed_methods_mutex);

        cmsg_publisher_queue_entry_free (queue_entry);

        pthread_mutex_lock (&publisher->send_queue_mutex);
        queue_entry = (cmsg_pub_queue_entry *) g_queue_pop_tail (publisher->send_queue);
        pthread_mutex_unlock (&publisher->send_queue_mutex);
    }
}

/**
 * The thread used to send the published messages to the subscribers.
 */
static void *
cmsg_publisher_send_thread (void *arg)
{
    cmsg_publisher *publisher = arg;
    while (1)
    {
        pthread_mutex_lock (&publisher->send_queue_mutex);

        /* Ensure mutex is unlocked if the thread is are cancelled */
        pthread_cleanup_push ((void (*)(void *)) pthread_mutex_unlock,
                              &publisher->send_queue_mutex);

        while (g_queue_get_length (publisher->send_queue) == 0)
        {
            pthread_cond_wait (&publisher->send_queue_process_cond,
                               &publisher->send_queue_mutex);
        }

        /* Unlock mutex */
        pthread_cleanup_pop (1);

        cmsg_publisher_process_send_queue (publisher);
    }

    return NULL;
}

/**
 * Initialise the pthread structures used by a CMSG publisher. These are done
 * separately as there is no way to represent an unitialised value for these
 * types (so care must be taken in the deinitialisation).
 */
static int32_t
cmsg_publisher_init_pthread_structures (cmsg_publisher *publisher)
{
    if (pthread_mutex_init (&publisher->subscribed_methods_mutex, NULL) != 0)
    {
        return CMSG_RET_ERR;
    }

    if (pthread_mutex_init (&publisher->send_queue_mutex, NULL) != 0)
    {
        pthread_mutex_destroy (&publisher->subscribed_methods_mutex);
        return CMSG_RET_ERR;
    }

    if (pthread_cond_init (&publisher->send_queue_process_cond, NULL) != 0)
    {
        pthread_mutex_destroy (&publisher->subscribed_methods_mutex);
        pthread_mutex_destroy (&publisher->send_queue_mutex);
        return CMSG_RET_ERR;
    }

    return CMSG_RET_OK;
}

/**
 * Create a cmsg publisher for the given service.
 *
 * @param service - The service to create the publisher for.
 *
 * @returns A pointer to the publisher on success, NULL otherwise.
 */
cmsg_publisher *
cmsg_publisher_create (const ProtobufCServiceDescriptor *service)
{
    const char *service_name = NULL;
    GHashTable *hash_table = NULL;

    CMSG_ASSERT_RETURN_VAL (service != NULL, NULL);

    service_name = cmsg_service_name_get (service);

    cmsg_publisher *publisher = (cmsg_publisher *) CMSG_CALLOC (1, sizeof (*publisher));
    if (!publisher)
    {
        CMSG_LOG_GEN_ERROR ("[%s] Unable to create publisher.", service_name);
        return NULL;
    }

    publisher->self.object_type = CMSG_OBJ_TYPE_PUB;
    publisher->self.object = publisher;
    strncpy (publisher->self.obj_id, service_name, CMSG_MAX_OBJ_ID_LEN);

    publisher->parent.object_type = CMSG_OBJ_TYPE_NONE;
    publisher->parent.object = NULL;

    publisher->descriptor = service;
    publisher->invoke = &cmsg_pub_invoke;

    if (cmsg_publisher_init_pthread_structures (publisher) != CMSG_RET_OK)
    {
        CMSG_LOG_GEN_ERROR ("[%s] Unable to create publisher.", service_name);
        CMSG_FREE (publisher);
        return NULL;
    }

    hash_table = g_hash_table_new_full (g_str_hash, g_str_equal, cmsg_free,
                                        (GDestroyNotify)
                                        cmsg_composite_client_destroy_full);
    publisher->subscribed_methods = hash_table;
    if (!publisher->subscribed_methods)
    {
        CMSG_LOG_GEN_ERROR ("[%s] Unable to create publisher.", service_name);
        cmsg_publisher_destroy (publisher);
        return NULL;
    }

    publisher->update_server = cmsg_ps_create_publisher_update_server ();
    if (!publisher->update_server)
    {
        CMSG_LOG_GEN_ERROR ("[%s] Unable to create publisher.", service_name);
        cmsg_publisher_destroy (publisher);
        return NULL;
    }

    publisher->update_server->parent.object_type = CMSG_OBJ_TYPE_PUB;
    publisher->update_server->parent.object = publisher;

    if (!cmsg_pthread_server_init (&publisher->update_thread, publisher->update_server))
    {
        CMSG_LOG_GEN_ERROR ("[%s] Unable to create publisher.", service_name);
        cmsg_publisher_destroy (publisher);
        return NULL;
    }

    publisher->update_thread_running = true;

    publisher->send_queue = g_queue_new ();
    if (!publisher->send_queue)
    {
        CMSG_LOG_GEN_ERROR ("[%s] Unable to create publisher.", service_name);
        cmsg_publisher_destroy (publisher);
        return NULL;
    }

    if (pthread_create (&publisher->send_thread, NULL, cmsg_publisher_send_thread,
                        publisher) != 0)
    {
        CMSG_LOG_GEN_ERROR ("[%s] Unable to create publisher.", service_name);
        cmsg_publisher_destroy (publisher);
        return NULL;
    }

    publisher->send_thread_running = true;

    if (cmsg_publisher_init_subscribers (publisher) != CMSG_RET_OK)
    {
        CMSG_LOG_GEN_ERROR ("[%s] Unable to create publisher.", service_name);
        cmsg_publisher_destroy (publisher);
        return NULL;
    }

    return publisher;
}

/**
 * Destroy a cmsg publisher.
 *
 * @param publisher - The publisher to destroy.
 */
void
cmsg_publisher_destroy (cmsg_publisher *publisher)
{
    CMSG_ASSERT_RETURN_VOID (publisher != NULL);

    cmsg_ps_deregister_publisher (cmsg_service_name_get (publisher->descriptor),
                                  publisher->update_server);

    if (publisher->send_thread_running)
    {
        pthread_cancel (publisher->send_thread);
        pthread_join (publisher->send_thread, NULL);
        publisher->send_thread_running = false;
    }

    if (publisher->update_thread_running)
    {
        pthread_cancel (publisher->update_thread);
        pthread_join (publisher->update_thread, NULL);
        publisher->update_thread_running = false;
    }

    if (publisher->subscribed_methods)
    {
        g_hash_table_remove_all (publisher->subscribed_methods);
        g_hash_table_unref (publisher->subscribed_methods);
        publisher->subscribed_methods = NULL;
    }

    cmsg_destroy_server_and_transport (publisher->update_server);
    pthread_mutex_destroy (&publisher->subscribed_methods_mutex);
    pthread_mutex_destroy (&publisher->send_queue_mutex);
    pthread_cond_destroy (&publisher->send_queue_process_cond);
    g_queue_free_full (publisher->send_queue,
                       (GDestroyNotify) cmsg_publisher_queue_entry_free);

    CMSG_FREE (publisher);
}

void
cmsg_psd_update_impl_subscription_change (const void *service,
                                          const cmsg_psd_subscription_update *recv_msg)
{
    cmsg_publisher *publisher = NULL;
    void *_closure_data = NULL;
    cmsg_server_closure_data *closure_data = NULL;

    _closure_data = ((const cmsg_server_closure_info *) service)->closure_data;
    closure_data = (cmsg_server_closure_data *) _closure_data;

    if (closure_data->server->parent.object_type != CMSG_OBJ_TYPE_PUB)
    {
        CMSG_LOG_GEN_ERROR ("Failed to update subscriptions for CMSG publisher.");
        cmsg_psd_update_server_subscription_changeSend (service);
        return;
    }

    publisher = (cmsg_publisher *) closure_data->server->parent.object;

    pthread_mutex_lock (&publisher->subscribed_methods_mutex);

    if (recv_msg->added)
    {
        cmsg_publisher_add_subscriber (publisher, recv_msg->method_name,
                                       recv_msg->transport);
    }
    else
    {
        cmsg_publisher_remove_subscriber (publisher, recv_msg->method_name,
                                          recv_msg->transport);
    }

    pthread_mutex_unlock (&publisher->subscribed_methods_mutex);

    cmsg_psd_update_server_subscription_changeSend (service);
}
