/**
 * @file cmsg_liboop_helpers.c
 *
 * Simple helper functions for using CMSG with liboop event loops.
 *
 * Copyright 2020, Allied Telesis Labs New Zealand, Ltd
 */

#include <oop_util.h>
#include <gmodule.h>
#include <sys/eventfd.h>
#include "cmsg_liboop_helpers.h"
#include "cmsg_error.h"

/**
 * Callback function to read an accepted socket on a CMSG server.
 */
static void
cmsg_liboop_server_read (int sd, void *data)
{
    cmsg_server *server = (cmsg_server *) data;

    if (cmsg_server_receive (server, sd) < 0)
    {
        g_hash_table_remove (server->event_loop_data, GINT_TO_POINTER (sd));
        shutdown (sd, SHUT_RDWR);
        close (sd);
    }
}

/**
 * Callback function that fires once a socket is accepted for
 * a CMSG server. The function then schedules the socket to
 * be read via 'cmsg_liboop_server_read'.
 */
static void
cmsg_liboop_server_accepted (int sd, void *data)
{
    cmsg_server *server = (cmsg_server *) data;
    eventfd_t value;
    int *newfd_ptr = NULL;
    cmsg_server_accept_thread_info *info = server->accept_thread_info;
    oop_socket_hdl handle = NULL;

    /* clear notification */
    TEMP_FAILURE_RETRY (eventfd_read (info->accept_sd_eventfd, &value));

    while ((newfd_ptr = g_async_queue_try_pop (info->accept_sd_queue)))
    {
        handle = oop_socket_register (*newfd_ptr, cmsg_liboop_server_read, server);
        g_hash_table_insert (server->event_loop_data, GINT_TO_POINTER (*newfd_ptr), handle);
        CMSG_FREE (newfd_ptr);
    }
}

/**
 * Start the processing of the accepted connections for a CMSG server.
 *
 * @param server - The CMSG server to start processing.
 */
static void
cmsg_liboop_server_processing_start (cmsg_server *server)
{
    cmsg_server_accept_thread_info *info = server->accept_thread_info;
    oop_socket_hdl handle = NULL;

    server->event_loop_data = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
                                                     oop_socket_deregister);

    handle = oop_socket_register (info->accept_sd_eventfd, cmsg_liboop_server_accepted,
                                  server);
    g_hash_table_insert (server->event_loop_data, GINT_TO_POINTER (info->accept_sd_eventfd),
                         handle);
}

/**
 * Stop the processing of the accepted connections for a CMSG server.
 *
 * @param server - The CMSG server to stop processing.
 */
void
cmsg_liboop_server_processing_stop (cmsg_server *server)
{
    if (server && server->event_loop_data)
    {
        g_hash_table_remove_all (server->event_loop_data);
        g_hash_table_unref (server->event_loop_data);
        server->event_loop_data = NULL;
    }
}

/**
 * Init and start processing for the given CMSG server with liboop.
 *
 * @param server - The server to manage with an accept thread and liboop.
 *
 * @returns CMSG_RET_OK on success, CMSG_RET_ERR on failure.
 */
static int32_t
cmsg_liboop_server_init (cmsg_server *server)
{
    int32_t ret;

    ret = cmsg_server_accept_thread_init (server);
    if (ret != CMSG_RET_OK)
    {
        CMSG_LOG_GEN_ERROR ("Failed to initialize CMSG server accept thread for %s",
                            cmsg_service_name_get (server->service->descriptor));
        return ret;
    }

    cmsg_liboop_server_processing_start (server);
    return CMSG_RET_OK;
}

/**
 * Create and start processing a UNIX transport based CMSG server for the given
 * CMSG service.
 *
 * @param service - The protobuf-c service the server is to implement.
 *
 * @returns Pointer to the CMSG server. NULL on failure.
 */
cmsg_server *
cmsg_liboop_unix_server_init (ProtobufCService *service)
{
    cmsg_server *server = NULL;

    server = cmsg_create_server_unix_rpc (service);
    if (!server)
    {
        CMSG_LOG_GEN_ERROR ("Failed to initialize CMSG server for %s",
                            cmsg_service_name_get (service->descriptor));
        return NULL;
    }

    if (cmsg_liboop_server_init (server) != CMSG_RET_OK)
    {
        cmsg_destroy_server_and_transport (server);
        server = NULL;
    }

    return server;
}

/**
 * Destroy a CMSG server created via the liboop helper functions.
 *
 * @param server - The server to destroy.
 */
void
cmsg_liboop_server_destroy (cmsg_server *server)
{
    if (server)
    {
        cmsg_liboop_server_processing_stop (server);
        cmsg_destroy_server_and_transport (server);
    }
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
cmsg_liboop_tipc_mesh_init (ProtobufCService *service, const char *service_entry_name,
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

    cmsg_liboop_server_processing_start (mesh->server);

    return mesh;
}

/**
 * Destroy a CMSG mesh connection created with the liboop helper.
 *
 * @param mesh - The mesh connection to destroy.
 */
void
cmsg_liboop_mesh_destroy (cmsg_tipc_mesh_conn *mesh)
{
    if (mesh)
    {
        cmsg_liboop_server_processing_stop (mesh->server);
        cmsg_tipc_mesh_connection_destroy (mesh);
    }
}

/**
 * Start a unix subscriber and subscribe for events.
 * @param service - service to subscribe to.
 * @param events - Array of strings of events to subscribe to. Last entry must be NULL.
 * @returns A pointer to the subscriber on success, NULL on failure.
 */
cmsg_subscriber *
cmsg_liboop_unix_subscriber_init (ProtobufCService *service, const char **events)
{
    cmsg_subscriber *sub = NULL;
    int32_t ret;

    sub = cmsg_subscriber_create_unix (service);
    if (!sub)
    {
        return NULL;
    }

    ret = cmsg_server_accept_thread_init (cmsg_sub_unix_server_get (sub));
    if (ret != CMSG_RET_OK)
    {
        CMSG_LOG_GEN_ERROR ("Failed to initialize CMSG server accept thread for %s",
                            cmsg_service_name_get (service->descriptor));
        cmsg_subscriber_destroy (sub);
        return NULL;
    }

    cmsg_liboop_server_processing_start (cmsg_sub_unix_server_get (sub));

    /* Subscribe to relevant events */
    if (events)
    {
        cmsg_sub_subscribe_events_local (sub, events);
    }

    return sub;
}

/**
 * Destroy a CMSG subscriber created with the liboop helper.
 *
 * @param subscriber - The subscriber to destroy.
 */
void
cmsg_liboop_unix_subscriber_destroy (cmsg_subscriber *subscriber)
{
    if (subscriber)
    {
        cmsg_liboop_server_processing_stop (cmsg_sub_unix_server_get (subscriber));
        cmsg_subscriber_destroy (subscriber);
    }
}

/**
 * Create and start processing a TIPC transport based CMSG server for the given
 * CMSG service for the given stack node ID.
 *
 * @param server_name - TIPC server name
 * @param member_id - TIPC node ID
 * @param scope - TIPC scope
 * @param service - The protobuf-c service the server is to implement.
 *
 * @returns Pointer to the 'cmsg_server' structure or NULL on failure.
 */
cmsg_server *
cmsg_liboop_tipc_rpc_server_init (const char *server_name, int member_id, int scope,
                                  ProtobufCService *service)
{
    cmsg_server *server = NULL;

    server = cmsg_create_server_tipc_rpc (server_name, member_id, scope, service);
    if (!server)
    {
        CMSG_LOG_GEN_ERROR ("Failed to initialize CMSG server for %s",
                            cmsg_service_name_get (service->descriptor));
        return NULL;
    }

    if (cmsg_liboop_server_init (server) != CMSG_RET_OK)
    {
        cmsg_destroy_server_and_transport (server);
        server = NULL;
    }

    return server;
}

/**
 * Create and start processing a TIPC transport based CMSG server for the given
 * CMSG service for the given stack node ID.
 *
 * @param server_name - TIPC server name
 * @param member_id - TIPC node ID
 * @param scope - TIPC scope
 * @param service - The protobuf-c service the server is to implement.
 *
 * @returns Pointer to the 'cmsg_server' structure or NULL on failure.
 */
cmsg_server *
cmsg_liboop_tipc_oneway_server_init (const char *server_name, int member_id, int scope,
                                     ProtobufCService *service)
{
    cmsg_server *server = NULL;

    server = cmsg_create_server_tipc_oneway (server_name, member_id, scope, service);
    if (!server)
    {
        CMSG_LOG_GEN_ERROR ("Failed to initialize CMSG server for %s",
                            cmsg_service_name_get (service->descriptor));
        return NULL;
    }

    if (cmsg_liboop_server_init (server) != CMSG_RET_OK)
    {
        cmsg_destroy_server_and_transport (server);
        server = NULL;
    }

    return server;
}
