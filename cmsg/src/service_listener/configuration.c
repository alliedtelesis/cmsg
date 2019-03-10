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
    configuration_server_delete_hostSend (service);
}

/**
 * todo
 */
void
configuration_impl_subscribe (const void *service, const subscription_info *recv_msg)
{
    configuration_server_subscribeSend (service);
}

/**
 * todo
 */
void
configuration_impl_unsubscribe (const void *service, const subscription_info *recv_msg)
{
    configuration_server_unsubscribeSend (service);
}

/**
 * todo
 */
void
configuration_impl_add_server (const void *service, const cmsg_service_info *recv_msg)
{
    configuration_server_add_serverSend (service);
}

/**
 * todo
 */
void
configuration_impl_remove_server (const void *service, const cmsg_service_info *recv_msg)
{
    configuration_server_remove_serverSend (service);
}

/**
 * Initialise the configuration functionality.
 */
void
configuration_server_init (void)
{
    server = cmsg_create_server_unix_oneway (CMSG_SERVICE_NOPACKAGE (configuration));
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
