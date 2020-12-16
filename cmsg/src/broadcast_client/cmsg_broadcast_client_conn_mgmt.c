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
#include "cmsg_pthread_helpers.h"

/**
 * Generate an event for the node/leave join.
 *
 * @param broadcast_client - The broadcast client to queue the event on.
 * @param node_addr - The address of the node that has joined/left.
 * @param joined - true if the node has joined, false if it has left.
 */
static void
cmsg_broadcast_client_generate_event (cmsg_broadcast_client *broadcast_client,
                                      struct in_addr node_addr, bool joined)
{
    cmsg_broadcast_client_event *event = NULL;

    if (broadcast_client->event_queue.queue)
    {
        event = CMSG_MALLOC (sizeof (cmsg_broadcast_client_event));
        if (event)
        {
            event->node_addr = node_addr;
            event->joined = joined;
            g_async_queue_push (broadcast_client->event_queue.queue, event);
            TEMP_FAILURE_RETRY (eventfd_write (broadcast_client->event_queue.eventfd, 1));
        }
    }
}

/**
 * Create a client using the given transport and then add the
 * client to the broadcast composite client.
 *
 * @param broadcast_client - The broadcast client.
 * @param transport - The transport to connect the client with.
 */
static void
cmsg_broadcast_client_add_child (cmsg_broadcast_client *broadcast_client,
                                 const cmsg_transport *transport)
{
    cmsg_client *child = NULL;
    const ProtobufCServiceDescriptor *descriptor =
        broadcast_client->base_client.base_client.descriptor;
    cmsg_client *comp_client = (cmsg_client *) &broadcast_client->base_client;
    cmsg_transport *transport_copy = cmsg_transport_copy (transport);
    struct in_addr address;

    child = cmsg_client_new (transport_copy, descriptor);
    if (!child)
    {
        CMSG_LOG_GEN_ERROR ("Failed to create child for broadcast client (service %s).",
                            broadcast_client->service_entry_name);
        cmsg_transport_destroy (transport_copy);
        return;
    }

    cmsg_composite_client_add_child (comp_client, child);

    address = transport->config.socket.sockaddr.in.sin_addr;
    cmsg_broadcast_client_generate_event (broadcast_client, address, true);
}

/**
 * Remove the client using the given transport from the broadcast
 * composite client and then free the memory of the removed client.
 *
 * @param broadcast_client - The broadcast client.
 * @param transport - The transport to remove the connection to.
 */
static void
cmsg_broadcast_client_delete_child (cmsg_broadcast_client *broadcast_client,
                                    const cmsg_transport *transport)
{
    cmsg_client *child = NULL;
    cmsg_client *comp_client = (cmsg_client *) &broadcast_client->base_client;
    struct in_addr address;

    child = cmsg_composite_client_lookup_by_transport (comp_client, transport);
    if (!child)
    {
        /* This shouldn't occur but if it does it suggests there is a bug in the
         * implementation of the broadcast client functionality. */
        CMSG_LOG_GEN_ERROR ("Failed to find child client in broadcast client (service %s).",
                            broadcast_client->service_entry_name);
        return;
    }

    if (cmsg_composite_client_delete_child (comp_client, child) < 0)
    {
        CMSG_LOG_GEN_ERROR
            ("Failed to remove child client from broadcast client (service %s).",
             broadcast_client->service_entry_name);
        return;
    }

    cmsg_destroy_client_and_transport (child);

    address = transport->config.socket.sockaddr.in.sin_addr;
    cmsg_broadcast_client_generate_event (broadcast_client, address, false);
}

/**
 * Processes a notification that a server has been added or removed
 *
 * @param transport    - cmsg_transport structure containing information about the server that
 *                       was added or removed.
 * @param added        - whether the server has added or removed
 * @param user_cb_data - the broadcase_client
 *
 */
static bool
server_event_callback (const cmsg_transport *transport, bool added, void *user_cb_data)
{
    uint32_t port;
    struct in_addr addr;
    cmsg_broadcast_client *broadcast_client = (cmsg_broadcast_client *) user_cb_data;
    const char *service_entry_name = broadcast_client->service_entry_name;

    /* Unix transports are not supported at this stage */
    if (transport->type == CMSG_TRANSPORT_RPC_UNIX ||
        transport->type == CMSG_TRANSPORT_ONEWAY_UNIX)
    {
        return true;
    }

    addr = transport->config.socket.sockaddr.in.sin_addr;
    port = transport->config.socket.sockaddr.in.sin_port;

    /* Some CMSG descriptors are not unique (e.g. "ffo.health" is used by multiple daemons)
     * Ensure we only connect to servers that are on the port we expect for this client.
     */
    if (port != htons (cmsg_service_port_get (service_entry_name, "tcp")))
    {
        /* Silently ignore */
        return true;
    }

    if (!broadcast_client->connect_to_self &&
        broadcast_client->my_node_addr.s_addr == addr.s_addr)
    {
        /* Only connect to the server on this node if the user has configured
         * their broadcast client to do so. */
        return true;
    }

    if (added)
    {
        cmsg_broadcast_client_add_child (broadcast_client, transport);
    }
    else
    {
        cmsg_broadcast_client_delete_child (broadcast_client, transport);
    }

    return true;
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
    const char *service_name =
        cmsg_service_name_get (broadcast_client->base_client.base_client.descriptor);

    if (!cmsg_pthread_service_listener_listen
        (&broadcast_client->topology_thread, service_name, server_event_callback,
         broadcast_client))
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
}
