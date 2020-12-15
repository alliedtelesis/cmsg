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
#include "publisher_subscriber/cmsg_sub_private.h"
#include "cmsg_sl.h"

typedef struct cmsg_glib_data_s
{
    GMainContext *context;
    GHashTable *sockets;
} cmsg_glib_data;

/**
 * Callback function to read an accepted socket on a CMSG server.
 */
static int
cmsg_glib_server_read (GIOChannel *source, GIOCondition condition, gpointer data)
{
    int sd = g_io_channel_unix_get_fd (source);
    cmsg_server *server = (cmsg_server *) data;
    cmsg_glib_data *glib_data = server->event_loop_data;

    if (cmsg_server_receive (server, sd) < 0)
    {
        shutdown (sd, SHUT_RDWR);
        close (sd);
        g_hash_table_remove (glib_data->sockets, GINT_TO_POINTER (sd));
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
    cmsg_server *server = (cmsg_server *) data;
    eventfd_t value;
    int *newfd_ptr = NULL;
    GSource *read_source;
    GIOChannel *read_channel = NULL;
    cmsg_server_accept_thread_info *info = server->accept_thread_info;
    cmsg_glib_data *glib_data = server->event_loop_data;

    /* clear notification */
    TEMP_FAILURE_RETRY (eventfd_read (info->accept_sd_eventfd, &value));

    while ((newfd_ptr = g_async_queue_try_pop (info->accept_sd_queue)))
    {
        read_channel = g_io_channel_unix_new (*newfd_ptr);
        read_source = g_io_create_watch (read_channel, G_IO_IN);
        g_io_channel_unref (read_channel);
        g_source_set_callback (read_source, (GSourceFunc) cmsg_glib_server_read,
                               server, NULL);
        g_source_attach (read_source, glib_data->context);
        g_source_unref (read_source);

        g_hash_table_insert (glib_data->sockets, GINT_TO_POINTER (*newfd_ptr), read_source);

        CMSG_FREE (newfd_ptr);
    }

    return TRUE;
}

/**
 * Start the processing of the accepted connections for a CMSG server.
 *
 * @param server - The CMSG server to start processing.
 * @param context - The main context to attach the accept thread to.
 */
void
_cmsg_glib_server_processing_start (cmsg_server *server, GMainContext *context)
{
    cmsg_server_accept_thread_info *info = server->accept_thread_info;
    GSource *source;
    GIOChannel *accept_channel = NULL;
    cmsg_glib_data *glib_data = CMSG_CALLOC (1, sizeof (cmsg_glib_data));

    if (!glib_data)
    {
        CMSG_LOG_GEN_ERROR ("Failed to start glib CMSG server processing");
        return;
    }

    glib_data->context = context;
    glib_data->sockets = g_hash_table_new (g_direct_hash, g_direct_equal);

    if (!glib_data->sockets)
    {
        CMSG_FREE (glib_data);
        CMSG_LOG_GEN_ERROR ("Failed to start glib CMSG server processing");
        return;
    }
    server->event_loop_data = glib_data;

    accept_channel = g_io_channel_unix_new (info->accept_sd_eventfd);
    source = g_io_create_watch (accept_channel, G_IO_IN);
    g_io_channel_unref (accept_channel);
    g_source_set_callback (source, (GSourceFunc) cmsg_glib_server_accepted, server, NULL);
    g_source_attach (source, context);
    g_source_unref (source);

    g_hash_table_insert (glib_data->sockets, GINT_TO_POINTER (info->accept_sd_eventfd),
                         source);
}

/**
 * Start the processing of the accepted connections for a CMSG server.
 *
 * @param server - The CMSG server to start processing.
 */
void
cmsg_glib_server_processing_start (cmsg_server *server)
{
    _cmsg_glib_server_processing_start (server, g_main_context_default ());
}

/**
 * Helper function called for each socket in the hash table. This closes the
 * socket (if it is not the accept thread eventfd) and destroys the glib source
 * used for processing the socket.
 *
 * @param key - The file descriptor (socket).
 * @param value - The glib source for the socket.
 * @param user_data - The CMSG server.
 *
 * @returns TRUE always (so that the entry is removed from the hash table).
 */
static gboolean
cmsg_glib_clear_sockets (gpointer key, gpointer value, gpointer user_data)
{
    cmsg_server *server = user_data;
    int accept_eventfd = server->accept_thread_info->accept_sd_eventfd;
    int sd = GPOINTER_TO_INT (key);

    g_source_destroy (value);
    if (sd != accept_eventfd)
    {
        shutdown (sd, SHUT_RDWR);
        close (sd);
    }

    return TRUE;
}

/**
 * Stop the processing of the accepted connections for a CMSG server.
 *
 * @param server - The CMSG server to stop processing.
 */
static void
cmsg_glib_server_processing_stop (cmsg_server *server)
{
    cmsg_glib_data *glib_data = NULL;

    if (server && server->event_loop_data)
    {
        glib_data = server->event_loop_data;
        g_hash_table_foreach_remove (glib_data->sockets, cmsg_glib_clear_sockets, server);
        g_hash_table_unref (glib_data->sockets);
        CMSG_FREE (glib_data);
        server->event_loop_data = NULL;
    }
}

/**
 * Init and start processing for the given CMSG server, on a given
 * main context.
 *
 * @param server - The server to manage with an accept thread and glib.
 * @param context - The main context to attach the accept thread to.
 *
 * @returns CMSG_RET_OK on success, CMSG_RET_ERR on failure.
 */
int32_t
cmsg_glib_thread_server_init (cmsg_server *server, GMainContext *context)
{
    int32_t ret;

    ret = cmsg_server_accept_thread_init (server);
    if (ret != CMSG_RET_OK)
    {
        CMSG_LOG_GEN_ERROR ("Failed to initialize CMSG server accept thread for %s",
                            cmsg_service_name_get (server->service->descriptor));
        return ret;
    }

    _cmsg_glib_server_processing_start (server, context);
    return CMSG_RET_OK;
}

/**
 * Init and start processing for the given CMSG server, on the default
 * main context.
 *
 * @param server - The server to manage with an accept thread and glib.
 *
 * @returns CMSG_RET_OK on success, CMSG_RET_ERR on failure.
 */
int32_t
cmsg_glib_server_init (cmsg_server *server)
{
    return cmsg_glib_thread_server_init (server, g_main_context_default ());
}

/**
 * Destroy a CMSG server created via the glib helper functions.
 *
 * @param server - The server to destroy.
 */
void
cmsg_glib_server_destroy (cmsg_server *server)
{
    if (server)
    {
        cmsg_glib_server_processing_stop (server);
        cmsg_destroy_server_and_transport (server);
    }
}

/**
 * deinit and destroy the given cmsg glib subscriber. It is advisable to unsubscribe from
 * events before calling this.
 *
 * @param sub to deinit and destroy
 */
void
cmsg_glib_subscriber_deinit (cmsg_subscriber *sub)
{
    if (sub)
    {
        cmsg_server_accept_thread_deinit (sub->local_server);
        cmsg_server_accept_thread_deinit (sub->remote_server);
        cmsg_subscriber_destroy (sub);
    }
}

/**
 * Start a unix subscriber and subscribe for events.
 * @param service - service to subscribe to.
 * @param events - Array of strings of events to subscribe to. Last entry must be NULL.
 * @returns A pointer to the subscriber on success, NULL on failure.
 */
cmsg_subscriber *
cmsg_glib_unix_subscriber_init (ProtobufCService *service, const char **events)
{
    cmsg_subscriber *sub = NULL;
    int32_t ret;

    sub = cmsg_subscriber_create_unix (service);
    if (!sub)
    {
        return NULL;
    }

    ret = cmsg_glib_server_init (cmsg_sub_unix_server_get (sub));
    if (ret != CMSG_RET_OK)
    {
        cmsg_subscriber_destroy (sub);
        return NULL;
    }

    /* Subscribe to relevant events */
    if (events)
    {
        cmsg_sub_subscribe_events_local (sub, events);
    }

    return sub;
}

/**
 * Start a tcp subscriber. Subscriptions are left for the caller to do.
 * @param service_name - The service name in the /etc/services file to get
 *                       the port number.
 * @param addr - The IPv4 address to use (in network byte order).
 * @param service - The service to subscribe to.
 * @returns A pointer to the subscriber on success, NULL on failure.
 */
cmsg_subscriber *
cmsg_glib_tcp_subscriber_init (const char *service_name, struct in_addr addr,
                               const ProtobufCService *service)
{
    cmsg_subscriber *sub = NULL;
    int32_t ret;

    sub = cmsg_subscriber_create_tcp (service_name, addr, NULL, service);
    if (!sub)
    {
        return NULL;
    }

    ret = cmsg_glib_server_init (cmsg_sub_unix_server_get (sub));
    if (ret != CMSG_RET_OK)
    {
        cmsg_subscriber_destroy (sub);
        return NULL;
    }

    ret = cmsg_glib_server_init (cmsg_sub_tcp_server_get (sub));
    if (ret != CMSG_RET_OK)
    {
        cmsg_subscriber_destroy (sub);
        return NULL;
    }

    return sub;
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
cmsg_glib_unix_server_init (ProtobufCService *service)
{
    cmsg_server *server = NULL;

    server = cmsg_create_server_unix_rpc (service);
    if (!server)
    {
        CMSG_LOG_GEN_ERROR ("Failed to initialize CMSG server for %s",
                            cmsg_service_name_get (service->descriptor));
        return NULL;
    }

    if (cmsg_glib_server_init (server) != CMSG_RET_OK)
    {
        cmsg_destroy_server_and_transport (server);
        server = NULL;
    }

    return server;
}

/**
 * Create and start processing a UNIX transport based oneway CMSG server for the given
 * CMSG service.
 *
 * @param service - The protobuf-c service the server is to implement.
 *
 * @returns Pointer to the CMSG server. NULL on failure.
 */
cmsg_server *
cmsg_glib_unix_server_init_oneway (ProtobufCService *service)
{
    cmsg_server *server = NULL;

    server = cmsg_create_server_unix_oneway (service);
    if (!server)
    {
        CMSG_LOG_GEN_ERROR ("Failed to initialize CMSG server for %s",
                            cmsg_service_name_get (service->descriptor));
        return NULL;
    }

    if (cmsg_glib_server_init (server) != CMSG_RET_OK)
    {
        cmsg_destroy_server_and_transport (server);
        server = NULL;
    }

    return server;
}

/**
 * Create and start processing a TCP transport based CMSG server for the given
 * CMSG service.
 *
 * @param service_name - The service name in the /etc/services file to get
 *                       the port number.
 * @param addr - The IPv4 address to listen on (in network byte order).
 * @param service - The CMSG service.
 *
 * @returns Pointer to a 'cmsg_server' structure.
 *          NULL on failure.
 */
cmsg_server *
cmsg_glib_tcp_server_init_oneway (const char *service_name, struct in_addr *addr,
                                  ProtobufCService *service)
{
    cmsg_server *server = NULL;
    int32_t ret;

    server = cmsg_create_server_tcp_ipv4_oneway (service_name, addr, NULL, service);
    if (!server)
    {
        CMSG_LOG_GEN_ERROR ("Failed to initialize CMSG server for %s",
                            cmsg_service_name_get (service->descriptor));
        return NULL;
    }

    ret = cmsg_glib_server_init (server);
    if (ret != CMSG_RET_OK)
    {
        cmsg_destroy_server_and_transport (server);
        server = NULL;
    }

    return server;
}

/**
 * Create and start processing a TCP transport based CMSG server for the given
 * CMSG service.
 *
 * @param service_name - The service name in the /etc/services file to get
 *                       the port number.
 * @param addr - The IPv4 address to listen on (in network byte order).
 * @param service - The CMSG service.
 *
 * @returns Pointer to a 'cmsg_server' structure.
 *          NULL on failure.
 */
cmsg_server *
cmsg_glib_tcp_server_init_rpc (const char *service_name, struct in_addr *addr,
                               ProtobufCService *service)
{
    cmsg_server *server = NULL;
    int32_t ret;

    server = cmsg_create_server_tcp_ipv4_rpc (service_name, addr, NULL, service);
    if (!server)
    {
        CMSG_LOG_GEN_ERROR ("Failed to initialize CMSG server for %s",
                            cmsg_service_name_get (service->descriptor));
        return NULL;
    }

    ret = cmsg_glib_server_init (server);
    if (ret != CMSG_RET_OK)
    {
        cmsg_destroy_server_and_transport (server);
        server = NULL;
    }

    return server;
}

/**
 * Create and start processing a TCP transport based CMSG server for the given
 * CMSG service.
 *
 * @param service_name - The service name in the /etc/services file to get
 *                       the port number.
 * @param addr - The IPv6 address to listen on (in network byte order).
 * @param scope_id - The scope id if a link local address is used, zero otherwise
 * @param vrf_bind_dev - For VRF support, the device to bind to the socket (NULL if not relevant)
 * @param service - The CMSG service.
 *
 * @returns Pointer to a 'cmsg_server' structure.
 *          NULL on failure.
 */
cmsg_server *
cmsg_glib_tcp_ipv6_server_init_oneway (const char *service_name, struct in6_addr *addr,
                                       uint32_t scope_id, const char *vrf_bind_dev,
                                       ProtobufCService *service)
{
    cmsg_server *server = NULL;
    int32_t ret;

    server = cmsg_create_server_tcp_ipv6_oneway (service_name, addr, scope_id, vrf_bind_dev,
                                                 service);
    if (!server)
    {
        CMSG_LOG_GEN_ERROR ("Failed to initialize CMSG server for %s",
                            cmsg_service_name_get (service->descriptor));
        return NULL;
    }

    ret = cmsg_glib_server_init (server);
    if (ret != CMSG_RET_OK)
    {
        cmsg_destroy_server_and_transport (server);
        server = NULL;
    }

    return server;
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

    cmsg_glib_server_processing_start (mesh->server);

    return mesh;
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
cmsg_glib_mesh_init (ProtobufCService *service, const char *service_entry_name,
                     struct in_addr this_node_addr, cmsg_mesh_local_type type, bool oneway)
{
    cmsg_mesh_conn *mesh =
        cmsg_mesh_connection_init (service, service_entry_name, this_node_addr,
                                   type, oneway, NULL);
    if (mesh == NULL)
    {
        CMSG_LOG_GEN_ERROR ("Failed to create mesh connection for %s",
                            cmsg_service_name_get (service->descriptor));
        return NULL;
    }

    cmsg_glib_server_processing_start (mesh->server);

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

/**
 * Callback function that can be used to process events generated from the CMSG
 * service listener functionality.
 */
static gboolean
cmsg_glib_sl_event_process (GIOChannel *source, GIOCondition condition, gpointer data)
{
    const cmsg_sl_info *info = (const cmsg_sl_info *) data;
    gboolean ret = G_SOURCE_CONTINUE;

    if (!cmsg_service_listener_event_queue_process (info))
    {
        cmsg_service_listener_unlisten (info);
        ret = G_SOURCE_REMOVE;
    }

    return ret;
}

/**
 * Begin listening for events for the given service.
 *
 * @param service_name - The service to listen for.
 * @param func - The function to call when a server is added or removed
 *               for the given service.
 * @param user_data - Pointer to user supplied data that will be passed into the
 *                    supplied handler function.
 */
void
cmsg_glib_service_listener_listen (const char *service_name,
                                   cmsg_sl_event_handler_t handler, void *user_data)
{
    int event_fd;
    GIOChannel *event_channel = NULL;
    const cmsg_sl_info *info = NULL;

    info = cmsg_service_listener_listen (service_name, handler, user_data);
    event_fd = cmsg_service_listener_get_event_fd (info);
    event_channel = g_io_channel_unix_new (event_fd);
    g_io_add_watch (event_channel, G_IO_IN, cmsg_glib_sl_event_process, (void *) info);
}

/**
 * Create and start processing a TIPC transport based CMSG server for the given
 * CMSG service for the given stack node ID.
 *
 * @param server_name - TIPC server name
 * @param member_id - VCS node ID
 * @param scope - TIPC scope
 * @param service - The protobuf-c service the server is to implement.
 *
 * @returns Pointer to the 'cmsg_server' structure or NULL on failure.
 */
cmsg_server *
cmsg_glib_tipc_rpc_server_init (const char *server_name, int member_id, int scope,
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

    if (cmsg_glib_server_init (server) != CMSG_RET_OK)
    {
        cmsg_destroy_server_and_transport (server);
        server = NULL;
    }

    return server;
}
