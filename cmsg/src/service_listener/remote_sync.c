/**
 * remote_sync.c
 *
 * Implements the functionality for syncing the service information between
 * the service listener daemon running on multiple remote hosts.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include <cmsg/cmsg_glib_helpers.h>
#include <cmsg/cmsg_composite_client.h>
#include "remote_sync_impl_auto.h"

static cmsg_server *server = NULL;
static cmsg_server_accept_thread_info *info = NULL;
static cmsg_client *comp_client = NULL;

/**
 * todo
 */
void
remote_sync_impl_bulk_sync (const void *service, const bulk_sync_data *recv_msg)
{
    remote_sync_server_bulk_syncSend (service);
}

/**
 * todo
 */
void
remote_sync_impl_add_server (const void *service, const cmsg_service_info *recv_msg)
{
    remote_sync_server_add_serverSend (service);
}

/**
 * todo
 */
void
remote_sync_impl_remove_server (const void *service, const cmsg_service_info *recv_msg)
{
    remote_sync_server_remove_serverSend (service);
}

/**
 * Create the CMSG server for remote service listener daemons to connect to and
 * sync their local service information to.
 *
 * @param addr - The address to use for the CMSG server.
 */
void
remote_sync_address_set (struct in_addr addr)
{
    if (!server)
    {
        server = cmsg_create_server_tcp_ipv4_oneway ("cmsg_sld_sync", &addr,
                                                     CMSG_SERVICE_NOPACKAGE (remote_sync));
        if (!server)
        {
            syslog (LOG_ERR, "Failed to initialize remote sync server");
            return;
        }

        info = cmsg_glib_server_init (server);
        if (!info)
        {
            syslog (LOG_ERR, "Failed to initialize remote sync server");
            cmsg_destroy_server_and_transport (server);
        }
    }
}

/**
 * Add a remote host to synchronise the local service information to.
 *
 * @param addr - The address of the remote host.
 */
void
remote_sync_add_host (struct in_addr addr)
{
    cmsg_client *client = NULL;

    client = cmsg_create_client_tcp_ipv4_oneway ("cmsg_sld_sync", &addr,
                                                 CMSG_DESCRIPTOR_NOPACKAGE (remote_sync));

    /* todo bulk sync */

    if (!comp_client)
    {
        comp_client = cmsg_composite_client_new (CMSG_DESCRIPTOR_NOPACKAGE (remote_sync));
    }
    cmsg_composite_client_add_child (comp_client, client);
}

/**
 * Remove a remote host from the list of remote hosts to synchronise
 * the local service information to.
 *
 * @param addr - The address of the remote host.
 */
void
remote_sync_delete_host (struct in_addr addr)
{
    /* todo, remove all services for given host */
    /* todo, remove client from composite */
}
