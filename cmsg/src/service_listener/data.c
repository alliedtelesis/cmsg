/**
 * data.c
 *
 * Implements the storage of the information about services running
 * locally as well as on remote members.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include <arpa/inet.h>
#include <cmsg/cmsg_glib_helpers.h>
#include "configuration_impl_auto.h"
#include "events_api_auto.h"
#include "remote_sync.h"
#include "transport/cmsg_transport_private.h"

typedef struct _service_data_entry_s
{
    GList *servers;
    GList *subscribers;
} service_data_entry;

static GHashTable *hash_table = NULL;

/**
 * Gets the 'service_data_entry' structure for the given service name
 * or creates one if it doesn't already exist.
 *
 * @param service - The name of the service.
 *
 * @returns A pointer to the related 'service_data_entry' structure.
 */
static service_data_entry *
get_service_entry_or_create (const char *service)
{
    service_data_entry *entry = NULL;

    entry = (service_data_entry *) g_hash_table_lookup (hash_table, service);
    if (!entry)
    {
        entry = CMSG_CALLOC (1, sizeof (service_data_entry));
        g_hash_table_insert (hash_table, (char *) service, entry);
    }

    return entry;
}

/**
 * Notify all subscribers of a given service about a server that has been added
 * or removed for that service.
 *
 * @param server_info - The information about the server that has been added/removed.
 * @param entry - The 'service_data_entry' containing the list of subscribers.
 * @param adeded - Whether the server has been added or removed.
 */
static void
notify_subscribers (const cmsg_service_info *server_info, service_data_entry *entry,
                    bool added)
{
    GList *list = NULL;
    GList *list_next = NULL;
    cmsg_client *client = NULL;
    GList *removal_list = NULL;
    int ret;

    for (list = g_list_first (entry->subscribers); list; list = list_next)
    {
        client = (cmsg_client *) list->data;
        list_next = g_list_next (list);

        if (added)
        {
            ret = events_api_server_added (client, server_info);
        }
        else
        {
            ret = events_api_server_added (client, server_info);
        }

        if (ret != CMSG_RET_OK)
        {
            removal_list = g_list_append (removal_list, client);
        }
    }

    /* Remove any clients that we failed to send events to */
    for (list = g_list_first (removal_list); list; list = list_next)
    {
        client = (cmsg_client *) list->data;
        list_next = g_list_next (list);

        entry->subscribers = g_list_remove (entry->subscribers, client);
        cmsg_destroy_client_and_transport (client);
    }
    g_list_free (removal_list);
}

/**
 * Add a newly created server to the database of servers running
 * for services.
 *
 * @param server_info - The information about the server being added.
 */
void
data_add_server (const cmsg_service_info *server_info)
{
    service_data_entry *entry = NULL;

    entry = get_service_entry_or_create (server_info->service);
    entry->servers = g_list_append (entry->servers, (gpointer) server_info);

    notify_subscribers (server_info, entry, true);
    remote_sync_server_added (server_info);
}

/**
 * Compares two 'cmsg_transport_info' structures for equality.
 *
 * @param transport_info_a - The first structure to compare.
 * @param transport_info_b - The second structure to compare.
 *
 * @returns true if they are equal, false otherwise.
 */
static bool
cmsg_transport_info_compare (cmsg_transport_info *transport_info_a,
                             cmsg_transport_info *transport_info_b)
{
    if (transport_info_a->type != transport_info_b->type ||
        transport_info_a->one_way != transport_info_b->one_way)
    {
        return false;
    }

    if (transport_info_a->type == CMSG_TRANSPORT_INFO_TYPE_TCP)
    {
        cmsg_tcp_transport_info *tcp_info_a = transport_info_a->tcp_info;
        cmsg_tcp_transport_info *tcp_info_b = transport_info_b->tcp_info;

        if (tcp_info_a->ipv4 == tcp_info_b->ipv4 &&
            tcp_info_a->port == tcp_info_b->port &&
            !memcmp (tcp_info_a->addr.data, tcp_info_b->addr.data, tcp_info_a->addr.len))
        {
            return true;
        }
        return false;
    }

    if (transport_info_a->type == CMSG_TRANSPORT_INFO_TYPE_UNIX)
    {
        cmsg_unix_transport_info *unix_info_a = transport_info_a->unix_info;
        cmsg_unix_transport_info *unix_info_b = transport_info_b->unix_info;

        return (strcmp (unix_info_a->path, unix_info_b->path) == 0);
    }

    return false;
}

/**
 * Helper function used with 'g_list_foreach'. Compares to server entries
 * for a service for equality.
 */
static gint
find_server (gconstpointer a, gconstpointer b)
{
    cmsg_service_info *service_a = (cmsg_service_info *) a;
    cmsg_service_info *service_b = (cmsg_service_info *) b;
    cmsg_transport_info *transport_info_a = service_a->server_info;
    cmsg_transport_info *transport_info_b = service_b->server_info;
    bool ret;

    ret = cmsg_transport_info_compare (transport_info_a, transport_info_b);

    return (ret ? 0 : -1);
}

/**
 * Remove a server from the database of servers running for services.
 *
 * @param server_info - The information about the server being removed.
 */
void
data_remove_server (const cmsg_service_info *server_info)
{
    service_data_entry *entry = NULL;
    GList *list_entry = NULL;

    entry = (service_data_entry *) g_hash_table_lookup (hash_table, server_info->service);

    list_entry = g_list_find_custom (entry->servers, server_info, find_server);
    CMSG_FREE_RECV_MSG (list_entry->data);
    entry->servers = g_list_delete_link (entry->servers, list_entry);

    notify_subscribers (server_info, entry, false);
    remote_sync_server_removed (server_info);
}

/**
 * Helper function called for each entry in the hash table. Finds any
 * servers for a service that match an IP address and deletes them.
 *
 * @param key - The key from the hash table (service name)
 * @param value - The value associated with the key (service_data_entry structure)
 * @param user_data - The address to match against.
 */
static void
_delete_server_by_addr (gpointer key, gpointer value, gpointer user_data)
{
    uint32_t *addr = (uint32_t *) user_data;
    service_data_entry *entry = (service_data_entry *) value;
    GList *list = NULL;
    GList *list_next = NULL;
    cmsg_service_info *service_info = NULL;
    GList *removal_list = NULL;

    for (list = g_list_first (entry->servers); list; list = list_next)
    {
        service_info = (cmsg_service_info *) list->data;

        if (service_info->server_info->type == CMSG_TRANSPORT_INFO_TYPE_TCP)
        {
            cmsg_tcp_transport_info *info = service_info->server_info->tcp_info;

            if (info->ipv4 && !memcmp (info->addr.data, addr, info->addr.len))
            {
                removal_list = g_list_append (removal_list, service_info);
            }
        }

        list_next = g_list_next (list);
    }

    for (list = g_list_first (removal_list); list; list = list_next)
    {
        service_info = (cmsg_service_info *) list->data;
        list_next = g_list_next (list);

        entry->servers = g_list_remove (entry->servers, service_info);
        notify_subscribers (service_info, entry, false);
        remote_sync_server_removed (service_info);
        CMSG_FREE_RECV_MSG (service_info);
    }
    g_list_free (removal_list);
}

/**
 * Remove any servers that match the given address from the hash table.
 *
 * @param addr - The IP address to match against.
 */
void
data_remove_servers_by_addr (struct in_addr addr)
{
    g_hash_table_foreach (hash_table, _delete_server_by_addr, &addr.s_addr);
}

/**
 * Add a new subscriber for a service.
 *
 * @param info - Information about the subscriber and the service
 *               they are subscribing for.
 */
void
data_add_subscriber (const subscription_info *info)
{
    service_data_entry *entry = NULL;
    cmsg_transport *transport = NULL;
    GList *list = NULL;
    GList *list_next = NULL;
    cmsg_service_info *server_info = NULL;
    cmsg_client *client = NULL;

    entry = get_service_entry_or_create (info->service);
    transport = cmsg_transport_info_to_transport (info->subscriber_info);
    client = cmsg_client_new (transport, CMSG_DESCRIPTOR_NOPACKAGE (events));
    entry->subscribers = g_list_append (entry->subscribers, client);

    for (list = g_list_first (entry->servers); list; list = list_next)
    {
        server_info = (cmsg_service_info *) list->data;
        list_next = g_list_next (list);

        if (!events_api_server_added (client, server_info) != CMSG_RET_OK)
        {
            entry->subscribers = g_list_remove (entry->subscribers, client);
            cmsg_destroy_client_and_transport (client);
            break;
        }
    }
}

/**
 * Remove a subscriber for a service.
 *
 * @param info - Information about the subscriber and the service
 *               they are unsubscribing from.
 */
void
data_remove_subscriber (const subscription_info *info)
{
    service_data_entry *entry = NULL;
    GList *list = NULL;
    GList *list_next = NULL;
    cmsg_client *client = NULL;
    entry = get_service_entry_or_create (info->service);
    cmsg_transport_info *transport_info = NULL;

    for (list = g_list_first (entry->subscribers); list; list = list_next)
    {
        client = (cmsg_client *) list->data;
        list_next = g_list_next (list);

        transport_info = cmsg_transport_info_create (client->_transport);

        if (cmsg_transport_info_compare (info->subscriber_info, transport_info))
        {
            break;
        }
    }

    entry->subscribers = g_list_remove (entry->subscribers, client);
    cmsg_destroy_client_and_transport (client);
}

/**
 * Initialise the data layer.
 */
void
data_init (void)
{
    hash_table = g_hash_table_new (g_str_hash, g_str_equal);
    if (!hash_table)
    {
        syslog (LOG_ERR, "Failed to initialize hash table");
        return;
    }
}

/**
 * Helper function called for each subscriber associated with a service. Prints the
 * information about each individual subscriber for a service.
 *
 * @param data - The client connected to the subscriber.
 * @param user_data - The file to print to.
 */
static void
data_debug_subscriber_dump (gpointer data, gpointer user_data)
{
    FILE *fp = (FILE *) user_data;
    const cmsg_client *client = (const cmsg_client *) data;

    fprintf (fp, "   %s\n", client->_transport->config.socket.sockaddr.un.sun_path);
}

/**
 * Print the information about a UNIX transport.
 *
 * @param fp - The file to print to.
 * @param transport_info - The 'cmsg_transport_info' structure for the transport.
 * @param oneway_str - String to print for the oneway/rpc nature of the transport.
 */
static void
data_debug_unix_server_dump (FILE *fp, const cmsg_transport_info *transport_info,
                             const char *oneway_str)
{

    fprintf (fp, "   (unix, %s) path = %s\n", oneway_str, transport_info->unix_info->path);
}

/**
 * Print the information about a TCP transport.
 *
 * @param fp - The file to print to.
 * @param transport_info - The 'cmsg_transport_info' structure for the transport.
 * @param oneway_str - String to print for the oneway/rpc nature of the transport.
 */
static void
data_debug_tcp_server_dump (FILE *fp, const cmsg_transport_info *transport_info,
                            const char *oneway_str)
{
    cmsg_tcp_transport_info *tcp_info = transport_info->tcp_info;
    char ip[INET6_ADDRSTRLEN] = { };
    uint16_t port;

    port = tcp_info->port;

    if (tcp_info->ipv4)
    {
        inet_ntop (AF_INET, tcp_info->addr.data, ip, INET6_ADDRSTRLEN);
    }
    else
    {
        inet_ntop (AF_INET6, tcp_info->addr.data, ip, INET6_ADDRSTRLEN);
    }

    fprintf (fp, "   (tcp, %s) %s:%u\n", oneway_str, ip, port);
}

/**
 * Helper function called for each server associated with a service. Prints the
 * information about each individual server for a service.
 *
 * @param data - The 'cmsg_service_info' structure for the server.
 * @param user_data - The file to print to.
 */
static void
data_debug_server_dump (gpointer data, gpointer user_data)
{
    FILE *fp = (FILE *) user_data;
    const cmsg_service_info *service_info = (const cmsg_service_info *) data;
    const cmsg_transport_info *transport_info = service_info->server_info;
    const char *oneway_str = (transport_info->one_way ? "one-way" : "rpc");

    if (transport_info->type == CMSG_TRANSPORT_INFO_TYPE_UNIX)
    {
        data_debug_unix_server_dump (fp, transport_info, oneway_str);
    }
    else
    {
        data_debug_tcp_server_dump (fp, transport_info, oneway_str);
    }
}

/**
 * Helper function called for each entry in the hash table. Prints the
 * information about each individual service.
 *
 * @param key - The key from the hash table (service name)
 * @param value - The value associated with the key (service_data_entry structure)
 * @param user_data - The file to print to.
 */
static void
_data_debug_dump (gpointer key, gpointer value, gpointer user_data)
{
    FILE *fp = (FILE *) user_data;
    service_data_entry *entry = (service_data_entry *) value;

    fprintf (fp, " service: %s\n", (char *) key);
    fprintf (fp, "  servers:\n");
    g_list_foreach (entry->servers, data_debug_server_dump, fp);
    fprintf (fp, "  subscribers:\n");
    g_list_foreach (entry->subscribers, data_debug_subscriber_dump, fp);
}

/**
 * Dump the current information about all known services to the debug file.
 *
 * @param fp - The file to print to.
 */
void
data_debug_dump (FILE *fp)
{
    fprintf (fp, "Services:\n");
    g_hash_table_foreach (hash_table, _data_debug_dump, fp);
}
