/**
 * cmsg_broadcast_client.c
 *
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */

#include <sys/eventfd.h>
#include "cmsg_broadcast_client_private.h"
#include "cmsg_broadcast_client.h"
#include "cmsg_error.h"
#include "cmsg_composite_client.h"
#include "transport/cmsg_transport_private.h"

/**
 * When destroying the broadcast client event queue there may still be
 * events on there. Simply free them as required to avoid leaking memory.
 */
static void
_clear_event_queue (gpointer data)
{
    free (data);
}

/**
 * Create a broadcast client structure. Simply allocate the required memory
 * and initialise all fields to their default values.
 *
 * @param descriptor - The service descriptor the broadcast client is for.
 *
 * @returns A pointer to the created client on success. NULL otherwise.
 */
static cmsg_broadcast_client *
cmsg_broadcast_client_create (const ProtobufCServiceDescriptor *descriptor)
{
    cmsg_broadcast_client *broadcast_client = NULL;
    int ret;

    broadcast_client =
        (cmsg_broadcast_client *) CMSG_CALLOC (1, sizeof (cmsg_broadcast_client));

    if (broadcast_client)
    {
        ret = cmsg_composite_client_init (&broadcast_client->base_client, descriptor);
        if (ret != CMSG_RET_OK)
        {
            CMSG_FREE (broadcast_client);
            return NULL;
        }

        broadcast_client->oneway_children = false;
        broadcast_client->service_entry_name = NULL;
        broadcast_client->connect_to_self = false;
        broadcast_client->event_queue.queue = NULL;
        broadcast_client->event_queue.eventfd = -1;
        broadcast_client->event_queue.handler = NULL;
    }
    else
    {
        CMSG_LOG_GEN_ERROR ("Unable to create broadcast client.");
    }

    return broadcast_client;
}

/**
 * Deinitialise the event handling functionality for the given broadcast client.
 *
 * @param broadcast_client - The broadcast client to deinitialise event handling for.
 */
static void
cmsg_broadcast_client_deinit_events (cmsg_broadcast_client *broadcast_client)
{
    broadcast_client->event_queue.handler = NULL;

    if (broadcast_client->event_queue.eventfd >= 0)
    {
        close (broadcast_client->event_queue.eventfd);
        broadcast_client->event_queue.eventfd = -1;
    }
    if (broadcast_client->event_queue.queue)
    {
        g_async_queue_unref (broadcast_client->event_queue.queue);
        broadcast_client->event_queue.queue = NULL;
    }
}

/**
 * Initialise the event handling functionality for the given broadcast client.
 *
 * @param broadcast_client - The broadcast client to initialise event handling for.
 * @param event_handler - The function to call for each event that occurs.
 *
 * @returns CMSG_RET_OK if initialisation is successful, CMSG_RET_ERR otherwise.
 */
static int32_t
cmsg_broadcast_client_init_events (cmsg_broadcast_client *broadcast_client,
                                   cmsg_broadcast_event_handler_t event_handler)
{
    broadcast_client->event_queue.eventfd = eventfd (0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (broadcast_client->event_queue.eventfd < 0)
    {
        return CMSG_RET_ERR;
    }

    broadcast_client->event_queue.queue = g_async_queue_new_full (_clear_event_queue);
    if (!broadcast_client->event_queue.queue)
    {
        close (broadcast_client->event_queue.eventfd);
        return CMSG_RET_ERR;
    }

    broadcast_client->event_queue.handler = event_handler;

    return CMSG_RET_OK;
}

cmsg_client *
cmsg_broadcast_client_new (const ProtobufCServiceDescriptor *descriptor,
                           const char *service_entry_name, struct in_addr my_node_addr,
                           bool connect_to_self, bool oneway,
                           cmsg_broadcast_event_handler_t event_handler)
{
    int ret;
    cmsg_broadcast_client *broadcast_client = NULL;

    CMSG_ASSERT_RETURN_VAL (descriptor != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (service_entry_name != NULL, NULL);

    broadcast_client = cmsg_broadcast_client_create (descriptor);
    if (!broadcast_client)
    {
        return NULL;
    }

    broadcast_client->service_entry_name = service_entry_name;
    broadcast_client->oneway_children = oneway;
    broadcast_client->my_node_addr = my_node_addr;
    broadcast_client->connect_to_self = connect_to_self;

    if (event_handler)
    {
        ret = cmsg_broadcast_client_init_events (broadcast_client, event_handler);
        if (ret != CMSG_RET_OK)
        {
            cmsg_composite_client_deinit (&broadcast_client->base_client);
            CMSG_FREE (broadcast_client);
            return NULL;
        }
    }

    ret = cmsg_broadcast_conn_mgmt_init (broadcast_client);
    if (ret != CMSG_RET_OK)
    {
        cmsg_composite_client_deinit (&broadcast_client->base_client);
        CMSG_FREE (broadcast_client);
        return NULL;
    }

    return (cmsg_client *) broadcast_client;
}

/**
 * Destroy the broadcast client.
 *
 * @param client - The broadcast client to destroy.
 */
void
cmsg_broadcast_client_destroy (cmsg_client *client)
{
    cmsg_broadcast_client *broadcast_client = (cmsg_broadcast_client *) client;
    GList *children = NULL;
    GList *l;
    cmsg_client *child;

    /* Connection management must be stopped before destroying client */
    cmsg_broadcast_conn_mgmt_deinit (broadcast_client);

    children =
        cmsg_composite_client_get_children ((cmsg_client *) &broadcast_client->base_client);
    for (l = children; l != NULL; l = l->next)
    {
        child = (cmsg_client *) l->data;
        cmsg_destroy_client_and_transport (child);
    }

    cmsg_composite_client_deinit (&broadcast_client->base_client);

    cmsg_broadcast_client_deinit_events (broadcast_client);

    CMSG_FREE (broadcast_client);
}

/**
 * Add a loopback client to a broadcast client.
 *
 * @param _broadcast_client - The broadcast client to add a loopback client to.
 * @param loopback_client - The loopback client to add.
 *
 * @returns CMSG_RET_OK on success, CMSG_RET_ERR on failure.
 */
int32_t
cmsg_broadcast_client_add_loopback (cmsg_client *_broadcast_client,
                                    cmsg_client *loopback_client)
{
    cmsg_broadcast_client *broadcast_client = (cmsg_broadcast_client *) _broadcast_client;

    CMSG_ASSERT_RETURN_VAL (broadcast_client != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (loopback_client->_transport->type == CMSG_TRANSPORT_LOOPBACK,
                            CMSG_RET_ERR);

    cmsg_client *comp_client = (cmsg_client *) &broadcast_client->base_client;

    return cmsg_composite_client_add_child (comp_client, loopback_client);
}

/**
 * Add a unix client to a broadcast client.
 *
 * @param _broadcast_client - The broadcast client to add a loopback client to.
 * @param unix_client - The unix client to add.
 *
 * @returns CMSG_RET_OK on success, CMSG_RET_ERR on failure.
 */
int32_t
cmsg_broadcast_client_add_unix (cmsg_client *_broadcast_client, cmsg_client *unix_client)
{
    cmsg_broadcast_client *broadcast_client = (cmsg_broadcast_client *) _broadcast_client;

    CMSG_ASSERT_RETURN_VAL (broadcast_client != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL ((unix_client->_transport->type == CMSG_TRANSPORT_RPC_UNIX ||
                             unix_client->_transport->type == CMSG_TRANSPORT_ONEWAY_UNIX),
                            CMSG_RET_ERR);

    cmsg_client *comp_client = (cmsg_client *) &broadcast_client->base_client;

    return cmsg_composite_client_add_child (comp_client, unix_client);
}

/**
 * Get the eventfd descriptor for the event queue of the given broadcast client
 *
 * @param _broadcast_client - The broadcast client to get the eventfd descriptor for.
 *
 * @returns The eventfd descriptor on success, -1 on failure.
 */
int
cmsg_broadcast_client_get_event_fd (cmsg_client *_broadcast_client)
{
    cmsg_broadcast_client *broadcast_client = (cmsg_broadcast_client *) _broadcast_client;

    CMSG_ASSERT_RETURN_VAL (broadcast_client != NULL, -1);

    return broadcast_client->event_queue.eventfd;
}

/**
 * Process any events on the event queue of the given broadcast client.
 *
 * @param _broadcast_client - The broadcast client to process events for.
 */
void
cmsg_broadcast_event_queue_process (cmsg_client *_broadcast_client)
{
    cmsg_broadcast_client *broadcast_client = (cmsg_broadcast_client *) _broadcast_client;
    cmsg_broadcast_client_event *event = NULL;
    eventfd_t value;
    cmsg_broadcast_event_handler_t handler_func = broadcast_client->event_queue.handler;

    CMSG_ASSERT_RETURN_VOID (broadcast_client != NULL);
    CMSG_ASSERT_RETURN_VOID (handler_func != NULL);

    /* clear notification */
    TEMP_FAILURE_RETRY (eventfd_read (broadcast_client->event_queue.eventfd, &value));

    while ((event = g_async_queue_try_pop (broadcast_client->event_queue.queue)))
    {
        handler_func (event->node_addr, event->joined);
        CMSG_FREE (event);
    }
}
