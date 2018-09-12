/**
 * cmsg_broadcast_client_conn_mgmt.c
 *
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */

#include <sys/eventfd.h>
#include <glib.h>
#include "cmsg_broadcast_client_private.h"
#include "cmsg_transport.h"
#include "cmsg_error.h"
#include "cmsg_composite_client.h"

/**
 * Generate an event for the node/leave join.
 *
 * @param broadcast_client - The broadcast client to queue the event on.
 * @param node_id - The id of the node that has joined/left.
 * @param joined - true if the node has joined, false if it has left.
 */
static void
cmsg_broadcast_client_generate_event (cmsg_broadcast_client *broadcast_client,
                                      uint32_t node_id, bool joined)
{
    cmsg_broadcast_client_event *event = NULL;

    if (broadcast_client->event_queue.queue)
    {
        event = CMSG_MALLOC (sizeof (cmsg_broadcast_client_event));
        if (event)
        {
            event->node_id = node_id;
            event->joined = joined;
            g_async_queue_push (broadcast_client->event_queue.queue, event);
            TEMP_FAILURE_RETRY (eventfd_write (broadcast_client->event_queue.eventfd, 1));
        }
    }
}

/**
 * Create a TIPC client to the given node id, connect the client,
 * and then add the client to the broadcast composite client.
 *
 * @param tipc_node_id - The TIPC node id to connect the client to.
 */
static void
cmsg_broadcast_client_add_child (cmsg_broadcast_client *broadcast_client,
                                 uint32_t tipc_node_id)
{
    cmsg_client *child = NULL;
    const char *service_entry_name = broadcast_client->service_entry_name;
    const ProtobufCServiceDescriptor *descriptor =
        broadcast_client->base_client.base_client.descriptor;
    cmsg_client *comp_client = (cmsg_client *) &broadcast_client->base_client;

    if (!broadcast_client->connect_to_self && broadcast_client->my_node_id == tipc_node_id)
    {
        /* Only connect to the server on this node if the user has configured
         * their broadcast client to do so. */
        return;
    }

    if (broadcast_client->oneway_children)
    {
        child = cmsg_create_client_tipc_oneway (service_entry_name, tipc_node_id,
                                                TIPC_CLUSTER_SCOPE,
                                                (ProtobufCServiceDescriptor *) descriptor);
    }
    else
    {
        child = cmsg_create_client_tipc_rpc (service_entry_name, tipc_node_id,
                                             TIPC_CLUSTER_SCOPE,
                                             (ProtobufCServiceDescriptor *) descriptor);
    }

    if (!child)
    {
        CMSG_LOG_GEN_ERROR ("Failed to create child for broadcast client (Node id = %u).",
                            tipc_node_id);
        return;
    }

    if (cmsg_client_connect (child) < 0)
    {
        CMSG_LOG_GEN_ERROR ("Failed to connect child for broadcast client (Node id = %u).",
                            tipc_node_id);
        cmsg_destroy_client_and_transport (child);
        return;
    }

    cmsg_composite_client_add_child (comp_client, child);

    cmsg_broadcast_client_generate_event (broadcast_client, tipc_node_id, true);
}

/**
 * Remove the TIPC client to the given node id from the broadcast
 * composite client and then free the memory of the removed client.
 *
 * @param tipc_node_id - The TIPC node id to remove the client to.
 */
static void
cmsg_broadcast_client_delete_child (cmsg_broadcast_client *broadcast_client,
                                    uint32_t tipc_node_id)
{
    cmsg_client *child = NULL;
    cmsg_client *comp_client = (cmsg_client *) &broadcast_client->base_client;

    if (!broadcast_client->connect_to_self && broadcast_client->my_node_id == tipc_node_id)
    {
        /* If the user has not configured the broadcast client to connect to
         * the local TIPC server then return early here. This avoids printing
         * an error that the child client can't be found. */
        return;
    }

    child = cmsg_composite_client_lookup_by_tipc_id (comp_client, tipc_node_id);
    if (!child)
    {
        /* This shouldn't occur but if it does it suggests there is a bug in the
         * implementation of the broadcast client functionality. */
        CMSG_LOG_GEN_ERROR
            ("Failed to find child client in broadcast client (Node id = %u).",
             tipc_node_id);
        return;
    }

    if (cmsg_composite_client_delete_child (comp_client, child) < 0)
    {
        CMSG_LOG_GEN_ERROR
            ("Failed to remove child client from broadcast client (Node id = %u).",
             tipc_node_id);
        return;
    }

    cmsg_destroy_client_and_transport (child);

    cmsg_broadcast_client_generate_event (broadcast_client, tipc_node_id, false);
}

/**
 * Processes a TIPC topology service event
 *
 * @param event - tipc_event structure containing information about the
 *                TIPC topology event.
 */
static void
tipc_subscr_callback (struct tipc_event *event, void *user_cb_data)
{
    uint32_t instance;
    cmsg_broadcast_client *broadcast_client = (cmsg_broadcast_client *) user_cb_data;

    instance = event->found_lower;

    /* this should never happen, but sanity-check event is valid */
    if (instance < broadcast_client->lower_node_id ||
        instance > broadcast_client->upper_node_id)
    {
        return;
    }

    switch (event->event)
    {
    case TIPC_PUBLISHED:
        cmsg_broadcast_client_add_child (broadcast_client, instance);
        break;
    case TIPC_WITHDRAWN:
        cmsg_broadcast_client_delete_child (broadcast_client, instance);
        break;
    case TIPC_SUBSCR_TIMEOUT:
        /* Don't care about this event. */
        break;
    default:
        CMSG_LOG_GEN_ERROR ("Unknown TIPC topology event received (%d)", event->event);
        break;
    }
}


/**
 * Reads a TIPC topology service event from the associated socket
 */
static void *
cmsg_broadcast_conn_mgmt_thread (void *_broadcast_client)
{
    fd_set read_fds;
    int maxfd;
    cmsg_broadcast_client *broadcast_client = (cmsg_broadcast_client *) _broadcast_client;
    int sd = -1;

    /*  create a socket and connect with it */
    sd = cmsg_tipc_topology_connect_subscribe (broadcast_client->service_entry_name,
                                               broadcast_client->lower_node_id,
                                               broadcast_client->upper_node_id,
                                               tipc_subscr_callback);
    if (sd < 0)
    {
        CMSG_LOG_GEN_ERROR ("Failed to get socket for tipc topology service.");
        pthread_exit (NULL);
    }

    broadcast_client->tipc_subscription_sd = sd;

    FD_ZERO (&read_fds);
    FD_SET (broadcast_client->tipc_subscription_sd, &read_fds);
    maxfd = broadcast_client->tipc_subscription_sd;

    while (1)
    {
        /* Explicitly set where the thread can be cancelled. This ensures no
         * data can be leaked if the thread is cancelled while handling a TIPC
         * topology event. */
        pthread_setcancelstate (PTHREAD_CANCEL_ENABLE, NULL);
        select (maxfd + 1, &read_fds, NULL, NULL, NULL);
        pthread_setcancelstate (PTHREAD_CANCEL_DISABLE, NULL);

        cmsg_tipc_topology_subscription_read (broadcast_client->tipc_subscription_sd,
                                              broadcast_client);
    }

    pthread_exit (NULL);
}


/**
 * Initialise the cmsg broadcast connection management.
 *
 * @param broadcast_client - The broadcast client to run connection management for.
 *
 * @return CMSG_RET_OK on success, CMSG_RET_ERR otherwise.
 */
int32_t
cmsg_broadcast_conn_mgmt_init (cmsg_broadcast_client *broadcast_client)
{
    if (pthread_create (&broadcast_client->topology_thread, NULL,
                        cmsg_broadcast_conn_mgmt_thread, (void *) broadcast_client) != 0)
    {
        return CMSG_RET_ERR;
    }

    cmsg_pthread_setname (broadcast_client->topology_thread,
                          broadcast_client->service_entry_name, CMSG_BC_CLIENT_PREFIX);

    return CMSG_RET_OK;
}

/**
 * Shutdown the TIPC topology monitoring thread.
 *
 * @param broadcast_client - The broadcast client to shutdown connection management for.
 */
void
cmsg_broadcast_conn_mgmt_deinit (cmsg_broadcast_client *broadcast_client)
{
    pthread_cancel (broadcast_client->topology_thread);
    pthread_join (broadcast_client->topology_thread, NULL);
    close (broadcast_client->tipc_subscription_sd);
}
