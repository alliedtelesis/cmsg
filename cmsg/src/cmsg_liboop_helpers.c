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
static void
cmsg_liboop_server_processing_stop (cmsg_server *server)
{
    g_hash_table_remove_all (server->event_loop_data);
    g_hash_table_unref (server->event_loop_data);
    server->event_loop_data = NULL;
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
