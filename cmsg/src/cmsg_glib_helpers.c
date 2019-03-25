/**
 * @file cmsg_glib_helpers.c
 *
 * Simple helper functions for using CMSG with glib event loops.
 *
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 *
 */

#include <glib.h>
#include <sys/eventfd.h>
#include "cmsg_glib_helpers.h"
#include "cmsg_error.h"

/**
 * Callback function to read an accepted socket on a CMSG server.
 */
static int
cmsg_glib_server_read (GIOChannel *source, GIOCondition condition, gpointer data)
{
    int sd = g_io_channel_unix_get_fd (source);
    cmsg_server_accept_thread_info *info = (cmsg_server_accept_thread_info *) data;

    if (cmsg_server_receive (info->server, sd) < 0)
    {
        g_io_channel_shutdown (source, TRUE, NULL);
        g_io_channel_unref (source);
        return FALSE;
    }

    return TRUE;
}

/**
 * Callback function that fires once a socket is accepted for
 * a CMSG server. The function then schedules the socket to
 * be read via 'cmsg_glib_server_read'.
 */
static int
cmsg_glib_server_accepted (GIOChannel *source, GIOCondition condition, gpointer data)
{
    cmsg_server_accept_thread_info *info = (cmsg_server_accept_thread_info *) data;
    eventfd_t value;
    int *newfd_ptr = NULL;
    GIOChannel *read_channel = NULL;

    /* clear notification */
    TEMP_FAILURE_RETRY (eventfd_read (info->accept_sd_eventfd, &value));

    while ((newfd_ptr = g_async_queue_try_pop (info->accept_sd_queue)))
    {
        read_channel = g_io_channel_unix_new (*newfd_ptr);
        g_io_add_watch (read_channel, G_IO_IN, cmsg_glib_server_read, info);
        CMSG_FREE (newfd_ptr);
    }

    return TRUE;
}

/**
 * Start the processing of the accepted connections for a CMSG server.
 *
 * @param info - The 'cmsg_server_accept_thread_info' structure holding the
 *               information about the CMSG server listening in a separate thread.
 */
void
cmsg_glib_server_processing_start (cmsg_server_accept_thread_info *info)
{
    GIOChannel *accept_channel = g_io_channel_unix_new (info->accept_sd_eventfd);
    g_io_add_watch (accept_channel, G_IO_IN, cmsg_glib_server_accepted, info);
}

/**
 * init and start processing for the given CMSG server.
 *
 * @param server Created server to manage with an accept thread and glib.
 *
 * @returns Pointer to a 'cmsg_server_accept_thread_info' structure.
 *          NULL on failure.
 */
cmsg_server_accept_thread_info *
cmsg_glib_server_init (cmsg_server *server)
{
    cmsg_server_accept_thread_info *info = NULL;

    info = cmsg_server_accept_thread_init (server);
    if (!info)
    {
        CMSG_LOG_GEN_ERROR ("Failed to initialize CMSG server accept thread for %s",
                            cmsg_service_name_get (server->service->descriptor));
        return NULL;
    }

    cmsg_glib_server_processing_start (info);
    return info;
}

/**
 * deinit and destroy the given cmsg glib subscriber. It is advisable to unsubscribe from
 * events before calling this.
 *
 * @param sub to deinit and destroy
 */
void
cmsg_glib_subscriber_deinit (cmsg_sub *sub)
{
    cmsg_server_accept_thread_info *info = sub->pub_server_thread_info;

    cmsg_server_accept_thread_deinit (info);
    cmsg_destroy_subscriber_and_transport (sub);
}

/**
 * Start a unix subscriber and subscribe for events.
 * @param service - service to subscribe to.
 * @param events - Array of strings of events to subscribe to. Last entry must be NULL.
 * @returns 'atl_cmsg_server_info' handle that can be used to deinit.
 */
cmsg_sub *
cmsg_glib_unix_subscriber_init (ProtobufCService *service, const char **events)
{
    cmsg_sub *sub = NULL;
    cmsg_transport *transport_r = NULL;
    cmsg_server_accept_thread_info *info;

    sub = cmsg_create_subscriber_unix_oneway (service);
    if (!sub)
    {
        return NULL;
    }

    info = cmsg_glib_server_init (sub->pub_server);
    if (!info)
    {
        cmsg_destroy_subscriber_and_transport (sub);
        return NULL;
    }
    sub->pub_server_thread_info = info;

    /* Subscribe to relevant events */
    if (events)
    {
        transport_r = cmsg_create_transport_unix (service->descriptor,
                                                  CMSG_TRANSPORT_RPC_UNIX);
        cmsg_sub_subscribe_events (sub, transport_r, events);
        cmsg_transport_destroy (transport_r);
    }

    return sub;
}

/**
 * Create and start processing a UNIX transport based CMSG server for the given
 * CMSG service.
 *
 * @param service - The protobuf-c service the server is to implement.
 *
 * @returns Pointer to a 'cmsg_server_accept_thread_info' structure.
 *          NULL on failure.
 */
cmsg_server_accept_thread_info *
cmsg_glib_unix_server_init (ProtobufCService *service)
{
    cmsg_server *server = NULL;
    cmsg_server_accept_thread_info *info = NULL;

    server = cmsg_create_server_unix_rpc (service);
    if (!server)
    {
        CMSG_LOG_GEN_ERROR ("Failed to initialize CMSG server for %s",
                            cmsg_service_name_get (service->descriptor));
        return NULL;
    }

    info = cmsg_glib_server_init (server);
    if (!info)
    {
        cmsg_destroy_server_and_transport (server);
    }

    return info;
}

/**
 * Create and initialise a CMSG mesh connection. This function automatically
 * starts the processing of the server that is part of the mesh connection.
 *
 * @param service - The protobuf-c service the mesh connection is for.
 * @param service_entry_name - The name in the services file corresponding to the
 *                             TIPC port to use for the protobuf-c service.
 *
 * @returns Pointer to a 'cmsg_tipc_mesh_conn' structure.
 *          NULL on failure.
 */
cmsg_tipc_mesh_conn *
cmsg_glib_tipc_mesh_init (ProtobufCService *service, const char *service_entry_name,
                          int this_node_id, int min_node_id, int max_node_id,
                          cmsg_mesh_local_type type, bool oneway)
{
    cmsg_tipc_mesh_conn *mesh =
        cmsg_tipc_mesh_connection_init (service, service_entry_name, this_node_id,
                                        min_node_id, max_node_id, type, oneway, NULL);
    if (mesh == NULL)
    {
        CMSG_LOG_GEN_ERROR ("Failed to create mesh connection for %s",
                            cmsg_service_name_get (service->descriptor));
        return NULL;
    }

    cmsg_glib_server_processing_start (mesh->server_thread_info);

    return mesh;
}

/**
 * Callback function that fires when an event is generated from a CMSG
 * broadcast client.
 */
static int
cmsg_glib_broadcast_event_process (GIOChannel *source, GIOCondition condition,
                                   gpointer data)
{
    cmsg_client *broadcast_client = (cmsg_client *) data;

    cmsg_broadcast_event_queue_process (broadcast_client);

    return TRUE;
}

/**
 * Start the processing of the generated events from a CMSG broadcast client.
 *
 * @param info - The broadcast client generating events.
 */
void
cmsg_glib_bcast_client_processing_start (cmsg_client *broadcast_client)
{
    int event_fd = cmsg_broadcast_client_get_event_fd (broadcast_client);

    GIOChannel *event_channel = g_io_channel_unix_new (event_fd);
    g_io_add_watch (event_channel, G_IO_IN, cmsg_glib_broadcast_event_process,
                    broadcast_client);
}
