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
        oop_socket_deregister (g_hash_table_lookup (server->event_loop_data,
                                                    GINT_TO_POINTER (sd)));
        g_hash_table_remove (server->event_loop_data, GINT_TO_POINTER (sd));
        cmsg_server_close_accepted_socket (server, sd);
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

    server->event_loop_data = g_hash_table_new (g_direct_hash, g_direct_equal);

    handle = oop_socket_register (info->accept_sd_eventfd, cmsg_liboop_server_accepted,
                                  server);
    g_hash_table_insert (server->event_loop_data, GINT_TO_POINTER (info->accept_sd_eventfd),
                         handle);
}

/**
 * Helper function called for each socket in the hash table. This closes the
 * socket (if it is not the accept thread eventfd) and deregisters the liboop
 * functionality used for processing the socket.
 *
 * @param key - The file descriptor (socket).
 * @param value - The liboop socket pointer.
 * @param user_data - The CMSG server.
 *
 * @returns TRUE always (so that the entry is removed from the hash table).
 */
static gboolean
cmsg_liboop_clear_sockets (gpointer key, gpointer value, gpointer user_data)
{
    cmsg_server *server = user_data;
    int accept_eventfd = server->accept_thread_info->accept_sd_eventfd;
    int sd = GPOINTER_TO_INT (key);

    oop_socket_deregister (value);
    if (sd != accept_eventfd)
    {
        cmsg_server_close_accepted_socket (server, sd);
    }

    return TRUE;
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
        g_hash_table_foreach_remove (server->event_loop_data, cmsg_liboop_clear_sockets,
                                     server);
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
 * @param service - The protobuf service for this connection.
 * @param service_entry_name - The name in the /etc/services file to get the TCP port number
 * @param this_node_addr - The IP address of this local node
 * @param type - The type of mesh connection to create.
 * @param oneway - Whether the connections are oneway or rpc.
 *
 * @returns Pointer to a 'cmsg_mesh_conn' structure.
 *          NULL on failure.
 */
cmsg_mesh_conn *
cmsg_liboop_mesh_init (ProtobufCService *service, const char *service_entry_name,
                       struct in_addr this_node_addr, cmsg_mesh_local_type type,
                       bool oneway)
{
    cmsg_mesh_conn *mesh = cmsg_mesh_connection_init (service, service_entry_name,
                                                      this_node_addr, type, oneway, NULL);
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
cmsg_liboop_mesh_destroy (cmsg_mesh_conn *mesh)
{
    if (mesh)
    {
        cmsg_liboop_server_processing_stop (mesh->server);
        cmsg_mesh_connection_destroy (mesh);
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
 * Create and start processing a TCP transport based CMSG server for the given
 * CMSG service for the given stack node ID.
 *
 * @param server_name - The service name in the /etc/services file to get
 *                      the port number.
 * @param addr - The IPv4 address to listen on (in network byte order).
 * @param service - The CMSG service.
 *
 * @returns Pointer to the 'cmsg_server' structure or NULL on failure.
 */
cmsg_server *
cmsg_liboop_tcp_rpc_server_init (const char *server_name, struct in_addr *addr,
                                 ProtobufCService *service)
{
    cmsg_server *server = NULL;

    server = cmsg_create_server_tcp_ipv4_rpc (server_name, addr, NULL, service);
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
 * Create and start processing a oneway TCP transport based CMSG server for the given
 * CMSG service for the given stack node ID.
 *
 * @param server_name - The service name in the /etc/services file to get
 *                      the port number.
 * @param addr - The IPv4 address to listen on (in network byte order).
 * @param service - The CMSG service.
 *
 * @returns Pointer to the 'cmsg_server' structure or NULL on failure.
 */
cmsg_server *
cmsg_liboop_tcp_oneway_server_init (const char *server_name, struct in_addr *addr,
                                    ProtobufCService *service)
{
    cmsg_server *server = NULL;

    server = cmsg_create_server_tcp_ipv4_oneway (server_name, addr, NULL, service);
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
 * Callback function for service listener events.
 */
static void
cmsg_liboop_sl_event_process (int sd, void *data)
{
    const cmsg_sl_info *info = (const cmsg_sl_info *) data;

    if (!cmsg_service_listener_event_queue_process (info))
    {
        oop_socket_deregister (cmsg_service_listener_event_loop_data_get (info));
        cmsg_service_listener_unlisten (info);
    }
}

/**
 * Begin listening for events for the given service.
 *
 * @param service_name - The service to listen for.
 * @param handler - The function to call when a server is added or removed
 *                  for the given service.
 * @param user_data - Pointer to user supplied data that will be passed into the
 *                    supplied handler function.
 *
 * @returns The service listener subscription information.
 */
const cmsg_sl_info *
cmsg_liboop_service_listener_listen (const char *service_name,
                                     cmsg_sl_event_handler_t handler, void *user_data)
{
    const cmsg_sl_info *info = NULL;
    oop_socket_hdl handle = NULL;

    info = cmsg_service_listener_listen (service_name, handler, user_data);
    if (!info)
    {
        CMSG_LOG_GEN_ERROR ("Failed to initialise service listener functionality");
        return false;
    }

    handle = oop_socket_register (cmsg_service_listener_get_event_fd (info),
                                  cmsg_liboop_sl_event_process, (void *) info);

    cmsg_service_listener_event_loop_data_set (info, handle);

    return info;
}
