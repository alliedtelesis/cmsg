/**
 * remote_sync.c
 *
 * Implements the functionality for syncing the subscriptions between
 * the daemons running on multiple remote hosts.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include <arpa/inet.h>
#include <cmsg/cmsg_glib_helpers.h>
#include "remote_sync_api_auto.h"
#include "remote_sync_impl_auto.h"
#include "remote_sync.h"
#include "data.h"
#include <cmsg/cmsg_sl.h>

static cmsg_server *server = NULL;
static cmsg_server_accept_thread_info *info = NULL;
static GList *client_list = NULL;

/**
 * Tell the daemon about all subscriptions from a remote host for services running
 * on this host.
 */
void
cmsg_pssd_remote_sync_impl_bulk_sync (const void *service,
                                      const cmsg_pssd_bulk_sync_data *recv_msg)
{
    int i;
    cmsg_subscription_info *info;

    CMSG_REPEATED_FOREACH (recv_msg, data, info, i)
    {
        data_add_local_subscription (info);
    }

    cmsg_pssd_remote_sync_server_bulk_syncSend (service);
}

/**
 * Tell the daemon about a subscription for a service running on this host that has
 * been added on a remote host.
 */
void
cmsg_pssd_remote_sync_impl_add_subscription (const void *service,
                                             const cmsg_subscription_info *recv_msg)
{
    data_add_local_subscription (recv_msg);
    cmsg_pssd_remote_sync_server_add_subscriptionSend (service);
}

/**
 * Tell the daemon about a subscription for a service running on this host that has
 * been removed on a remote host.
 */
void
cmsg_pssd_remote_sync_impl_remove_subscription (const void *service,
                                                const cmsg_subscription_info *recv_msg)
{
    data_remove_local_subscription (recv_msg);
    cmsg_pssd_remote_sync_server_remove_subscriptionSend (service);
}

/**
 * Helper function called for each client in the GList. Finds the client that
 * matches the input IP address.
 *
 * @param a - A client from the list.
 * @param b - Pointer to the address to compare against.
 *
 * @returns 0 if the client matches, -1 otherwise.
 */
static gint
remote_sync_find_client_by_address (gconstpointer a, gconstpointer b)
{
    cmsg_client *client = (cmsg_client *) a;
    uint32_t *addr = (uint32_t *) b;

    if (client->_transport->config.socket.sockaddr.in.sin_addr.s_addr == *addr)
    {
        return 0;
    }

    return -1;
}

/**
 * Helper function for calling the remote sync API to add or remove a subscription
 * from a remote host.
 *
 * @param subscriber_info - The message containing the subscriber information.
 * @param added - Whether the subscriber is being added or removed.
 */
static void
remote_sync_subscription_added_removed (const cmsg_subscription_info *subscriber_info,
                                        bool added)
{
    cmsg_client *client = NULL;
    GList *link = NULL;

    link = g_list_find_custom (client_list, &subscriber_info->remote_addr,
                               remote_sync_find_client_by_address);
    if (link)
    {
        client = (cmsg_client *) link->data;

        if (added)
        {
            cmsg_pssd_remote_sync_api_add_subscription (client, subscriber_info);
        }
        else
        {
            cmsg_pssd_remote_sync_api_remove_subscription (client, subscriber_info);
        }
    }
}

/**
 * Notify a remote host that a subscription for a service on that host has been added.
 *
 * @param subscriber_info - Information about the subscription that has been added.
 */
void
remote_sync_subscription_added (const cmsg_subscription_info *subscriber_info)
{
    remote_sync_subscription_added_removed (subscriber_info, true);
}

/**
 * Notify a remote host that a subscription for a service on that host has been removed.
 *
 * @param subscriber_info - Information about the subscription that has been removed.
 */
void
remote_sync_subscription_removed (const cmsg_subscription_info *subscriber_info)
{
    remote_sync_subscription_added_removed (subscriber_info, false);
}

/**
 * Send all subscriptions on this node that are for a remote host that has just joined.
 *
 * @param client - The client to the remote host that has just joined.
 */
static void
remote_sync_bulk_sync_subscriptions (cmsg_client *client)
{
    cmsg_pssd_bulk_sync_data send_msg = CMSG_PSSD_BULK_SYNC_DATA_INIT;
    GList *list = NULL;
    GList *list_next = NULL;
    const cmsg_subscription_info *info = NULL;
    uint32_t remote_addr = client->_transport->config.socket.sockaddr.in.sin_addr.s_addr;

    for (list = g_list_first (data_get_remote_subscriptions ()); list; list = list_next)
    {
        info = (const cmsg_subscription_info *) list->data;
        if (info->remote_addr == remote_addr)
        {
            CMSG_REPEATED_APPEND (&send_msg, data, info);
        }
        list_next = g_list_next (list);
    }

    cmsg_pssd_remote_sync_api_bulk_sync (client, &send_msg);
    CMSG_REPEATED_FREE (send_msg.data);
}

/**
 * Helper function called for each client in the GList. Finds the client that
 * matches the input transport.
 *
 * @param a - A client from the list.
 * @param b - Pointer to the transport to compare against.
 *
 * @returns 0 if the client matches, -1 otherwise.
 */
static gint
remote_sync_find_client_by_transport (gconstpointer a, gconstpointer b)
{
    cmsg_client *client = (cmsg_client *) a;
    cmsg_transport *transport = (cmsg_transport *) b;

    if (cmsg_transport_compare (client->_transport, transport))
    {
        return 0;
    }

    return -1;
}

/**
 * Logic to run when a server for the "cmsg_pssd, remote_sync" service starts or
 * stops running on either a local or remote node. In this case we only care about
 * remote host events.
 *
 * @param transport - The transport information about the server that has either started
 *                    or stopped.
 * @param added - true if the server has started running, false otherwise.
 */
static void
remote_sync_sl_event_handler (cmsg_transport *transport, bool added)
{
    cmsg_client *client = NULL;
    GList *link = NULL;

    /* Do nothing for the server running locally. */
    if (cmsg_transport_compare (server->_transport, transport))
    {
        cmsg_transport_destroy (transport);
        return;
    }

    if (added)
    {
        client = cmsg_client_new (transport, CMSG_DESCRIPTOR (cmsg_pssd, remote_sync));
        client_list = g_list_append (client_list, client);
        remote_sync_bulk_sync_subscriptions (client);
    }
    else
    {
        /* todo: Remove subscriptions from that host */
        link = g_list_find_custom (client_list, transport,
                                   remote_sync_find_client_by_transport);
        cmsg_destroy_client_and_transport ((cmsg_client *) link->data);
        client_list = g_list_delete_link (client_list, link);
        cmsg_transport_destroy (transport);
    }
}

/**
 * Callback function that fires when an event is generated from the CMSG
 * service listener functionality.
 */
static int
remote_sync_sl_event_process (GIOChannel *source, GIOCondition condition, gpointer data)
{
    const cmsg_sl_info *info = (const cmsg_sl_info *) data;

    cmsg_service_listener_event_queue_process (info);

    return TRUE;
}

/**
 * Initialise the usage of the service listener functionality to track the related CMSG
 * servers running on remote nodes..
 */
static void
remote_sync_sl_init (void)
{
    int event_fd;
    GIOChannel *event_channel = NULL;
    const cmsg_sl_info *info = NULL;
    const char *service_name = NULL;;

    service_name = cmsg_service_name_get (CMSG_DESCRIPTOR (cmsg_pssd, remote_sync));

    info = cmsg_service_listener_listen (service_name, remote_sync_sl_event_handler);

    event_fd = cmsg_service_listener_get_event_fd (info);
    event_channel = g_io_channel_unix_new (event_fd);
    g_io_add_watch (event_channel, G_IO_IN, remote_sync_sl_event_process, (void *) info);
}

/**
 * Create the CMSG server for remote daemons to connect to and
 * sync their local subscriptions to.
 *
 * @param addr - The address to use for the CMSG server.
 */
void
remote_sync_address_set (struct in_addr addr)
{
    if (!server)
    {
        server = cmsg_create_server_tcp_ipv4_oneway ("cmsg_pssd_sync", &addr,
                                                     CMSG_SERVICE (cmsg_pssd, remote_sync));
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
            return;
        }

        remote_sync_sl_init ();
        data_check_remote_entries ();
    }
}

/**
 * Get the IPv4 address used by the remote sync server on this node.
 *
 * @returns the IPv4 address (or zero if the address has not been set yet).
 */
uint32_t
remote_sync_get_local_ip (void)
{
    uint32_t addr = 0;

    if (server)
    {
        addr = server->_transport->config.socket.sockaddr.in.sin_addr.s_addr;;
    }

    return addr;
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
    g_list_foreach (client_list, data_debug_server_dump, fp);

    fprintf (fp, "\n");
}
