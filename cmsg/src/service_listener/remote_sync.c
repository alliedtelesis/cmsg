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
#include "data.h"

cmsg_server *remote_sync_server = NULL;
uint32_t local_ip_addr = 0;
cmsg_client *comp_client = NULL;

/**
 * Tell the service listener daemon about all servers running on a remote host.
 */
void
cmsg_sld_remote_sync_impl_bulk_sync (const void *service,
                                     const cmsg_sld_bulk_sync_data *recv_msg)
{
    int index = 0;
    cmsg_service_info *info = NULL;
    cmsg_sld_bulk_sync_data *recv_data = NULL;

    /* Cast away the const so that we can modify the message to keep
     * some internal memory */
    recv_data = (cmsg_sld_bulk_sync_data *) recv_msg;

    for (index = 0; index < recv_data->n_data; index++)
    {
        info = recv_data->data[index];
        data_add_server (info, false);
        /* Set to NULL so that the memory is not freed */
        recv_data->data[index] = NULL;
    }

    cmsg_sld_remote_sync_server_bulk_syncSend (service);
}

/**
 * Tell the service listener daemon that a server on a remote host has started.
 */
void
cmsg_sld_remote_sync_impl_add_server (const void *service,
                                      const cmsg_service_info *recv_msg)
{
    /* We hold onto the message to store in the data hash table */
    cmsg_server_app_owns_current_msg_set (remote_sync_server);

    data_add_server ((cmsg_service_info *) recv_msg, false);
    cmsg_sld_remote_sync_server_add_serverSend (service);
}

/**
 * Tell the service listener daemon that a server running on a remote host
 * is no longer running.
 */
void
cmsg_sld_remote_sync_impl_remove_server (const void *service,
                                         const cmsg_service_info *recv_msg)
{
    data_remove_server (recv_msg);
    cmsg_sld_remote_sync_server_remove_serverSend (service);
}

/**
 * Helper function to notify remote hosts when a server is added or removed.
 *
 * @param server_info - The 'cmsg_service_info' message describing the server.
 * @param added - Whether the server has been added or removed.
 *
 * @returns true if we sent the server information to the remote hosts,
 *          false otherwise.
 */
static bool
remote_sync_server_added_removed (const cmsg_service_info *server_info, bool added)
{
    uint32_t addr;
    cmsg_transport_info *transport_info = server_info->server_info;

    /* Don't sync if the composite client is not created, i.e. no remote hosts yet */
    if (!comp_client)
    {
        return false;
    }

    /* Don't sync if the servers transport type is not supported */
    if (transport_info->type != CMSG_TRANSPORT_INFO_TYPE_TIPC &&
        transport_info->type != CMSG_TRANSPORT_INFO_TYPE_TCP)
    {
        return false;
    }

    /* Don't sync if the server is using a TCP transport but it uses an ipv6 address */
    if (transport_info->type == CMSG_TRANSPORT_INFO_TYPE_TCP &&
        !transport_info->tcp_info->ipv4)
    {
        return false;
    }

    /* Only sync TCP servers that use the same IP address as the address that
     * we sync to remote nodes using. */
    if (transport_info->type == CMSG_TRANSPORT_INFO_TYPE_TCP)
    {
        memcpy (&addr, transport_info->tcp_info->addr.data, sizeof (uint32_t));
        if (addr != local_ip_addr)
        {
            return false;
        }
    }

    /* Only sync TIPC servers that are hosted on the local node. This avoids endless
     * loops of notification around the nodes. */
    if (transport_info->type == CMSG_TRANSPORT_INFO_TYPE_TIPC)
    {
        if (transport_info->tipc_info->addr_name_name_instance !=
            ip_addr_to_tipc_instance (local_ip_addr))
        {
            return false;
        }
    }

    if (added)
    {
        cmsg_sld_remote_sync_api_add_server (comp_client, server_info);
    }
    else
    {
        cmsg_sld_remote_sync_api_remove_server (comp_client, server_info);
    }

    return true;
}

/**
 * Notify all remote hosts of the server that has been added locally.
 */
bool
remote_sync_server_added (const cmsg_service_info *server_info)
{
    return remote_sync_server_added_removed (server_info, true);
}

/**
 * Notify all remote hosts of the server that has been removed locally.
 */
bool
remote_sync_server_removed (const cmsg_service_info *server_info)
{
    return remote_sync_server_added_removed (server_info, false);
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
    if (!remote_sync_server)
    {
        remote_sync_server = cmsg_glib_tcp_server_init_oneway ("cmsg_sld_sync", &addr,
                                                               CMSG_SERVICE (cmsg_sld,
                                                                             remote_sync));
        local_ip_addr = addr.s_addr;
    }
}

/**
 * Helper function called with 'g_list_foreach'. Fills a 'cmsg_sld_bulk_sync_data'
 * message with each entry in the GList.
 *
 * @param data - The service info message.
 * @param user_data - The 'cmsg_sld_bulk_sync_data' message to fill.
 */
static void
fill_bulk_sync_msg (gpointer data, gpointer user_data)
{
    cmsg_sld_bulk_sync_data *send_msg = (cmsg_sld_bulk_sync_data *) user_data;
    cmsg_service_info *service_info = (cmsg_service_info *) data;
    CMSG_REPEATED_APPEND (send_msg, data, service_info);
}

/**
 * Bulk sync all servers running on the local remote sync IP address to a remote node.
 *
 * @param client - The CMSG client connected to the remote host to sync to.
 */
static void
remote_sync_bulk_sync_services (cmsg_client *client)
{
    cmsg_sld_bulk_sync_data send_msg = CMSG_SLD_BULK_SYNC_DATA_INIT;
    GList *services_list = NULL;

    services_list = data_get_servers_by_addr (local_ip_addr);
    g_list_foreach (services_list, fill_bulk_sync_msg, &send_msg);

    cmsg_sld_remote_sync_api_bulk_sync (client, &send_msg);
    CMSG_REPEATED_FREE (send_msg.data);
    g_list_free (services_list);
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

    client = cmsg_create_client_tcp_ipv4_oneway ("cmsg_sld_sync", &addr, NULL,
                                                 CMSG_DESCRIPTOR (cmsg_sld, remote_sync));

    if (!comp_client)
    {
        comp_client = cmsg_composite_client_new (CMSG_DESCRIPTOR (cmsg_sld, remote_sync));
    }
    cmsg_composite_client_add_child (comp_client, client);
    remote_sync_bulk_sync_services (client);
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

    if (cmsg_composite_client_num_children (comp_client) == 0)
    {
        cmsg_client_destroy (comp_client);
        comp_client = NULL;
    }
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
    if (remote_sync_server)
    {
        remote_sync_debug_print_transport_ip (fp, remote_sync_server->_transport);
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
