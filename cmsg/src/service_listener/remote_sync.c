/**
 * remote_sync.c
 *
 * Implements the functionality for syncing the service information between
 * the service listener daemon running on multiple remote hosts.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include <arpa/inet.h>
#include <cmsg/cmsg_glib_helpers.h>
#include <cmsg/cmsg_composite_client.h>
#include "remote_sync_api_auto.h"
#include "remote_sync_impl_auto.h"
#include "remote_sync.h"

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
 * Tell the service listener daemon that a server on a remote host has started.
 */
void
remote_sync_impl_add_server (const void *service, const cmsg_service_info *recv_msg)
{
    /* We hold onto the message to store in the data hash table */
    cmsg_server_app_owns_current_msg_set (server);

    data_add_server (recv_msg);
    remote_sync_server_add_serverSend (service);
}

/**
 * Tell the service listener daemon that a server running on a remote host
 * is no longer running.
 */
void
remote_sync_impl_remove_server (const void *service, const cmsg_service_info *recv_msg)
{
    data_remove_server (recv_msg);
    remote_sync_server_remove_serverSend (service);
}

/**
 * Get the IP address of the remote sync server running locally.
 *
 * @returns The IPv4 address of the server.
 */
static uint32_t
remote_sync_get_local_ip (void)
{
    uint32_t addr = 0;

    if (server)
    {
        addr = server->_transport->config.socket.sockaddr.in.sin_addr.s_addr;
    }

    return addr;
}

/**
 * Helper function to notify remote hosts when a server is added or removed.
 *
 * @param server_info - The 'cmsg_service_info' message describing the server.
 * @param added - Whether the server has been added or removed.
 */
static void
remote_sync_server_added_removed (const cmsg_service_info *server_info, bool added)
{
    uint32_t addr;
    cmsg_transport_info *transport_info = server_info->server_info;

    /* Don't sync if:
     * - The composite client is not created, i.e. no remote hosts yet
     * - The server is not using a TCP transport
     * - The server is using a TCP transport but it uses an ipv6 address */
    if (!comp_client || transport_info->type != CMSG_TRANSPORT_INFO_TYPE_TCP ||
        !transport_info->tcp_info->ipv4)
    {
        return;
    }

    /* Only sync TCP servers that use the same IP address as the address that
     * we sync to remote nodes using. */
    memcpy (&addr, transport_info->tcp_info->addr.data, sizeof (uint32_t));
    if (addr != remote_sync_get_local_ip ())
    {
        return;
    }

    if (added)
    {
        remote_sync_api_add_server (comp_client, server_info);
    }
    else
    {
        remote_sync_api_remove_server (comp_client, server_info);
    }
}

/**
 * Notify all remote hosts of the server that has been added locally.
 */
void
remote_sync_server_added (const cmsg_service_info *server_info)
{
    remote_sync_server_added_removed (server_info, true);
}

/**
 * Notify all remote hosts of the server that has been removed locally.
 */
void
remote_sync_server_removed (const cmsg_service_info *server_info)
{
    remote_sync_server_added_removed (server_info, false);
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
    cmsg_client *child_client = NULL;

    if (!comp_client)
    {
        return;
    }

    child_client = cmsg_composite_client_lookup_by_tcp_ipv4_addr (comp_client, addr.s_addr);
    cmsg_composite_client_delete_child (comp_client, child_client);
    cmsg_destroy_client_and_transport (child_client);
}

/**
 * Prints the IP address used by the given TCP transport.
 *
 * @param fp - The file to print to.
 * @param transport - The TCP transport to print the IP address for.
 */
static void
remote_sync_debug_print_transport_ip (FILE *fp, cmsg_transport *transport)
{
    char ip[INET6_ADDRSTRLEN] = { };
    uint32_t addr;

    addr = transport->config.socket.sockaddr.in.sin_addr.s_addr;
    addr = ntohl (addr);
    inet_ntop (AF_INET, &addr, ip, INET6_ADDRSTRLEN);

    fprintf (fp, "%s", ip);
}

/**
 * Helper function called for each client to a remote host. Prints the
 * IP address of the remote host.
 *
 * @param data - The cmsg client.
 * @param user_data - The file to print to.
 */
static void
data_debug_server_dump (gpointer data, gpointer user_data)
{
    const cmsg_client *client = (const cmsg_client *) data;
    FILE *fp = (FILE *) user_data;

    remote_sync_debug_print_transport_ip (fp, client->_transport);
    fprintf (fp, " ");
}

/**
 * Dump the current information about all known hosts to the debug file.
 *
 * @param fp - The file to print to.
 */
void
remote_sync_debug_dump (FILE *fp)
{
    GList *child_clients = NULL;

    fprintf (fp, "Hosts:\n");
    fprintf (fp, " local: ");
    if (server)
    {
        remote_sync_debug_print_transport_ip (fp, server->_transport);
    }
    else
    {
        fprintf (fp, "none");
    }
    fprintf (fp, "\n");

    fprintf (fp, " remote: ");
    if (comp_client)
    {
        child_clients = cmsg_composite_client_get_children (comp_client);
        g_list_foreach (child_clients, data_debug_server_dump, fp);
    }

    fprintf (fp, "\n");
}
