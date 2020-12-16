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
#include "transport/cmsg_transport_private.h"
#include "data.h"

static cmsg_server *server = NULL;

/**
 * Configures the address information for the CMSG server running in the service listener
 * daemon for syncing to remote hosts.
 */
void
cmsg_sld_configuration_impl_address_set (const void *service,
                                         const cmsg_sld_address_info *recv_msg)
{
    struct in_addr addr = { };

    addr.s_addr = recv_msg->ip_addr;

    remote_sync_address_set (addr);

    cmsg_sld_configuration_server_address_setSend (service);
}

/**
 * Configures a remote host for the service listener daemon.
 */
void
cmsg_sld_configuration_impl_add_host (const void *service, const cmsg_uint32 *recv_msg)
{
    struct in_addr addr = { };

    addr.s_addr = recv_msg->value;

    cmsg_transport_tcp_cache_set (&addr, true);
    remote_sync_add_host (addr);
    cmsg_sld_configuration_server_add_hostSend (service);
}

/**
 * Removes a remote host from the service listener daemon.
 */
void
cmsg_sld_configuration_impl_delete_host (const void *service,
                                         const cmsg_sld_address_info *recv_msg)
{
    struct in_addr addr = { };

    addr.s_addr = recv_msg->ip_addr;

    cmsg_transport_tcp_cache_set (&addr, false);
    remote_sync_delete_host (addr);
    data_remove_servers_by_addr (addr);
    cmsg_sld_configuration_server_delete_hostSend (service);
}

/**
 * Tell the service listener daemon that a listener wishes to receive
 * events about a given service.
 */
void
cmsg_sld_configuration_impl_listen (const void *service,
                                    const cmsg_sld_listener_info *recv_msg)
{
    data_add_listener (recv_msg);
    cmsg_sld_configuration_server_listenSend (service);
}

/**
 * Tell the service listener daemon that a listener no longer wishes to receive
 * events about a given service.
 */
void
cmsg_sld_configuration_impl_unlisten (const void *service,
                                      const cmsg_sld_listener_info *recv_msg)
{
    data_remove_listener (recv_msg);
    cmsg_sld_configuration_server_unlistenSend (service);
}

/**
 * Tell the service listener daemon that a server implementing a specific service
 * is now running.
 */
void
cmsg_sld_configuration_impl_add_server (const void *service,
                                        const cmsg_service_info *recv_msg)
{
    if (recv_msg->server_info->type != CMSG_TRANSPORT_INFO_TYPE_UNIX &&
        recv_msg->server_info->type != CMSG_TRANSPORT_INFO_TYPE_TCP)
    {
        /* Ignore unsupported service types */
        cmsg_sld_configuration_server_add_serverSend (service);
        return;
    }

    /* We hold onto the message to store in the data hash table */
    cmsg_server_app_owns_current_msg_set (server);
    data_add_server ((cmsg_service_info *) recv_msg, true);

    cmsg_sld_configuration_server_add_serverSend (service);
}

/**
 * Tell the service listener daemon that a server implementing a specific service
 * is no longer running.
 */
void
cmsg_sld_configuration_impl_remove_server (const void *service,
                                           const cmsg_service_info *recv_msg)
{
    if (recv_msg->server_info->type != CMSG_TRANSPORT_INFO_TYPE_UNIX &&
        recv_msg->server_info->type != CMSG_TRANSPORT_INFO_TYPE_TCP)
    {
        /* Ignore unsupported service types */
        cmsg_sld_configuration_server_remove_serverSend (service);
        return;
    }

    data_remove_server (recv_msg, true);

    cmsg_sld_configuration_server_remove_serverSend (service);
}

/**
 * Initialise the configuration functionality.
 */
void
configuration_server_init (void)
{
    cmsg_transport *transport = NULL;

    transport = cmsg_create_transport_unix (CMSG_DESCRIPTOR (cmsg_sld, configuration),
                                            CMSG_TRANSPORT_ONEWAY_UNIX);
    if (transport == NULL)
    {
        syslog (LOG_ERR, "Failed to initialize configuration server");
        return;
    }

    /* Use 'cmsg_server_create' directly, rather than 'cmsg_server_new' to avoid
     * calling the function for sending the service information to the service listener
     * daemon which would deadlock. */
    server = cmsg_server_create (transport, CMSG_SERVICE (cmsg_sld, configuration));
    if (!server)
    {
        syslog (LOG_ERR, "Failed to initialize configuration server");
        return;
    }

    if (cmsg_glib_server_init (server) != CMSG_RET_OK)
    {
        syslog (LOG_ERR, "Failed to initialize configuration server");
        cmsg_destroy_server_and_transport (server);
    }
}
