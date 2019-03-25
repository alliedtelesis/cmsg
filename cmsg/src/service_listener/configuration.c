/**
 * configuration.c
 *
 * Implements the APIs for configuring the service listener daemon.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include <cmsg/cmsg_glib_helpers.h>
#include "configuration_impl_auto.h"
#include "remote_sync.h"
#include "cmsg_server_private.h"
#include "data.h"

static cmsg_server *server = NULL;
static cmsg_server_accept_thread_info *info = NULL;

/**
 * Configures the IP address of the CMSG server running in the service listener
 * daemon for syncing to remote hosts.
 */
void
configuration_impl_address_set (const void *service, const cmsg_uint32 *recv_msg)
{
    struct in_addr addr = { };

    addr.s_addr = recv_msg->value;

    remote_sync_address_set (addr);
    configuration_server_address_setSend (service);
}

/**
 * Configures a remote host for the service listener daemon.
 */
void
configuration_impl_add_host (const void *service, const cmsg_uint32 *recv_msg)
{
    struct in_addr addr = { };

    addr.s_addr = recv_msg->value;

    remote_sync_add_host (addr);
    configuration_server_add_hostSend (service);
}

/**
 * Removes a remote host from the service listener daemon.
 */
void
configuration_impl_delete_host (const void *service, const cmsg_uint32 *recv_msg)
{
    struct in_addr addr = { };

    addr.s_addr = recv_msg->value;

    remote_sync_delete_host (addr);
    data_remove_servers_by_addr (addr);
    configuration_server_delete_hostSend (service);
}

/**
 * Tell the service listener daemon that a listener wishes to receive
 * events about a given service.
 */
void
configuration_impl_listen (const void *service, const listener_info *recv_msg)
{
    configuration_server_listenSend (service);
}

/**
 * Tell the service listener daemon that a listener no longer wishes to receive
 * events about a given service.
 */
void
configuration_impl_unlisten (const void *service, const listener_info *recv_msg)
{
    configuration_server_unlistenSend (service);
}

/**
 * Tell the service listener daemon that a server implementing a specific service
 * is now running.
 */
void
configuration_impl_add_server (const void *service, const cmsg_service_info *recv_msg)
{
    if (recv_msg->server_info->type == CMSG_TRANSPORT_INFO_TYPE_NOT_SET)
    {
        /* Ignore everything except TCP and UNIX services for now */
        configuration_server_add_serverSend (service);
        return;
    }

    /* We hold onto the message to store in the data hash table */
    cmsg_server_app_owns_current_msg_set (server);
    data_add_server (recv_msg);

    configuration_server_add_serverSend (service);
}

/**
 * Tell the service listener daemon that a server implementing a specific service
 * is no longer running.
 */
void
configuration_impl_remove_server (const void *service, const cmsg_service_info *recv_msg)
{
    if (recv_msg->server_info->type == CMSG_TRANSPORT_INFO_TYPE_NOT_SET)
    {
        /* Ignore everything except TCP and UNIX services for now */
        configuration_server_add_serverSend (service);
        return;
    }

    data_remove_server (recv_msg);

    configuration_server_remove_serverSend (service);
}

/**
 * Initialise the configuration functionality.
 */
void
configuration_server_init (void)
{
    cmsg_transport *transport = NULL;

    transport = cmsg_create_transport_unix (CMSG_DESCRIPTOR_NOPACKAGE (configuration),
                                            CMSG_TRANSPORT_ONEWAY_UNIX);
    if (transport == NULL)
    {
        syslog (LOG_ERR, "Failed to initialize configuration server");
        return;
    }

    /* Use 'cmsg_server_create' directly, rather than 'cmsg_server_new' to avoid
     * calling the function for sending the service information to the service listener
     * daemon which would deadlock. */
    server = cmsg_server_create (transport, CMSG_SERVICE_NOPACKAGE (configuration));
    if (!server)
    {
        syslog (LOG_ERR, "Failed to initialize configuration server");
        return;
    }

    info = cmsg_glib_server_init (server);
    if (!info)
    {
        syslog (LOG_ERR, "Failed to initialize configuration server");
        cmsg_destroy_server_and_transport (server);
    }
}
