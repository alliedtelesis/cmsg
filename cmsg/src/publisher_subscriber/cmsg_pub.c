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

void
cmsg_psd_update_impl_subscription_change (const void *service,
                                          const cmsg_psd_subscription_update *recv_msg)
{
    cmsg_publisher *publisher = NULL;
    void *_closure_data = NULL;
    cmsg_server_closure_data *closure_data = NULL;
    GList *entry = NULL;

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
        publisher->subscribed_methods = g_list_append (publisher->subscribed_methods,
                                                       CMSG_STRDUP (recv_msg->method_name));
    }
    else
    {
        entry = g_list_find_custom (publisher->subscribed_methods, recv_msg->method_name,
                                    (GCompareFunc) strcmp);
        CMSG_FREE (entry->data);
        publisher->subscribed_methods = g_list_delete_link (publisher->subscribed_methods,
                                                            entry);
    }

    pthread_mutex_unlock (&publisher->subscribed_methods_mutex);

    cmsg_psd_update_server_subscription_changeSend (service);
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
    int32_t result;
    cmsg_publisher *publisher = (cmsg_publisher *) service;
    const char *method_name;
    uint8_t *packet = NULL;
    uint32_t total_message_size = 0;
    const char *service_name = NULL;

    CMSG_ASSERT_RETURN_VAL (service != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (service->descriptor != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (input != NULL, CMSG_RET_ERR);

    method_name = service->descriptor->methods[method_index].name;

    pthread_mutex_lock (&publisher->subscribed_methods_mutex);

    /* If there are no subscribers for this method then simply return */
    if (!g_list_find_custom (publisher->subscribed_methods, method_name,
                             (GCompareFunc) strcmp))
    {
        pthread_mutex_unlock (&publisher->subscribed_methods_mutex);
        return CMSG_RET_OK;
    }

    pthread_mutex_unlock (&publisher->subscribed_methods_mutex);

    result = cmsg_client_create_packet (publisher->client, method_name, input,
                                        &packet, &total_message_size);
    if (result != CMSG_RET_OK)
    {
        return CMSG_RET_ERR;
    }

    service_name = cmsg_service_name_get (service->descriptor);
    ret = cmsg_ps_publish_message (publisher->client, service_name, method_name,
                                   packet, total_message_size);
    CMSG_FREE (packet);

    return ret;
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
    GList *methods = NULL;

    CMSG_ASSERT_RETURN_VAL (service != NULL, NULL);

    service_name = cmsg_service_name_get (service);

    cmsg_publisher *publisher = (cmsg_publisher *) CMSG_CALLOC (1, sizeof (*publisher));
    if (!publisher)
    {
        CMSG_LOG_GEN_ERROR ("[%s] Unable to create publisher.", service_name);
        return NULL;
    }

    if (pthread_mutex_init (&publisher->subscribed_methods_mutex, NULL) != 0)
    {
        CMSG_LOG_GEN_ERROR ("[%s] Unable to create publisher.", service_name);
        CMSG_FREE (publisher);
        return NULL;
    }

    publisher->self.object_type = CMSG_OBJ_TYPE_PUB;
    publisher->self.object = publisher;
    strncpy (publisher->self.obj_id, service_name, CMSG_MAX_OBJ_ID_LEN);

    publisher->parent.object_type = CMSG_OBJ_TYPE_NONE;
    publisher->parent.object = NULL;

    publisher->descriptor = service;
    publisher->invoke = &cmsg_pub_invoke;

    publisher->client = cmsg_ps_create_publisher_client ();
    if (!publisher->client)
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

    pthread_mutex_lock (&publisher->subscribed_methods_mutex);

    if (cmsg_ps_register_publisher (service_name, publisher->update_server,
                                    &methods) != CMSG_RET_OK)
    {
        pthread_mutex_unlock (&publisher->subscribed_methods_mutex);
        CMSG_LOG_GEN_ERROR ("[%s] Unable to create publisher.", service_name);
        cmsg_publisher_destroy (publisher);
        return NULL;
    }

    publisher->subscribed_methods = methods;

    pthread_mutex_unlock (&publisher->subscribed_methods_mutex);

    return publisher;
}

/**
 * Wrapper around CMSG_FREE that allows it to be called with 'g_list_free_full'.
 */
static void
cmsg_publisher_method_free (gpointer data)
{
    CMSG_FREE (data);
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

    cmsg_ps_unregister_publisher (cmsg_service_name_get (publisher->descriptor),
                                  publisher->update_server);

    if (publisher->update_thread_running)
    {
        pthread_cancel (publisher->update_thread);
        pthread_join (publisher->update_thread, NULL);
        publisher->update_thread_running = false;
    }

    g_list_free_full (publisher->subscribed_methods, cmsg_publisher_method_free);
    cmsg_destroy_client_and_transport (publisher->client);
    cmsg_destroy_server_and_transport (publisher->update_server);
    pthread_mutex_destroy (&publisher->subscribed_methods_mutex);
    CMSG_FREE (publisher);
}
