/**
 * data.c
 *
 * Implements the storage of the information about subscriptions.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include <glib.h>
#include <arpa/inet.h>
#include <cmsg/cmsg_private.h>
#include "data.h"
#include "remote_sync.h"
#include "transport/cmsg_transport_private.h"

typedef struct
{
    char *method_name;
    GList *transports;
} method_data_entry;

typedef struct
{
    GList *methods;
} service_data_entry;

static GHashTable *local_subscriptions_table = NULL;
static GList *remote_subscriptions_list = NULL;

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

    entry = (service_data_entry *) g_hash_table_lookup (local_subscriptions_table, service);
    if (!entry)
    {
        entry = CMSG_CALLOC (1, sizeof (service_data_entry));
        g_hash_table_insert (local_subscriptions_table, g_strdup (service), entry);
    }

    return entry;
}

/**
 * Helper function for use with 'g_list_find_custom'. Specifically compares a
 * 'method_data_entry' structure with a method name string to see if they match.
 *
 * @param a - The 'method_data_entry' structure to use in the comparison.
 * @param b - The method name string to use in the comparison.
 *
 * @returns 0 if they match, -1 otherwise.
 */
static gint
method_entry_compare (gconstpointer a, gconstpointer b)
{
    method_data_entry *entry = (method_data_entry *) a;
    const char *method_name = (const char *) b;

    return strcmp (entry->method_name, method_name);
}

/**
 * Gets the 'method_data_entry' structure for the given method name
 * or creates one if it doesn't already exist.
 *
 * @param service_entry - The service entry to get the method entry from.
 * @param method - The name of the method to get the entry for.
 *
 * @returns A pointer to the related 'method_data_entry' structure.
 */
static method_data_entry *
get_method_entry_or_create (service_data_entry *service_entry, const char *method)
{
    method_data_entry *entry = NULL;
    GList *list_entry = NULL;

    list_entry = g_list_find_custom (service_entry->methods, method, method_entry_compare);
    if (list_entry)
    {
        entry = (method_data_entry *) list_entry->data;
    }
    else
    {
        entry = CMSG_CALLOC (1, sizeof (method_data_entry));
        entry->method_name = CMSG_STRDUP (method);
        service_entry->methods = g_list_prepend (service_entry->methods, entry);
    }

    return entry;
}

/**
 * Add a new subscription to the database. Note that this function may steal
 * the memory of the passed in 'cmsg_pssd_subscription_info' message (see the
 * return value for more information).
 *
 * @param info - The information about the subscription being added.
 * @param sync - If the subscription is remote set true to sync this subscription
 *               to the associated remote node, false to not sync.
 *
 * @returns true if the memory of the passed in 'cmsg_pssd_subscription_info'
 *          message is now stolen (the caller should not free the memory).
 *          false if the memory was not stolen (the caller is still responsible
 *          for the memory).
 */
bool
data_add_subscription (const cmsg_pssd_subscription_info *info, bool sync)
{
    service_data_entry *service_entry = NULL;
    method_data_entry *method_entry = NULL;
    cmsg_transport *transport = NULL;

    if (CMSG_IS_FIELD_PRESENT (info, remote_addr))
    {
        if (!remote_sync_address_is_set () ||
            remote_sync_get_local_ip () != info->remote_addr)
        {
            remote_subscriptions_list = g_list_prepend (remote_subscriptions_list,
                                                        (void *) info);
            if (sync)
            {
                /* todo: sync it */
            }
            return true;
        }
    }

    service_entry = get_service_entry_or_create (info->service);
    method_entry = get_method_entry_or_create (service_entry, info->method_name);
    transport = cmsg_transport_info_to_transport (info->transport_info);
    method_entry->transports = g_list_prepend (method_entry->transports, transport);

    return false;
}

/**
 * Remove a subscription from the database.
 *
 * @param info - The information about the subscription being removed.
 */
void
data_remove_subscription (const cmsg_pssd_subscription_info *info)
{
    /* todo */
}

/**
 * Remove all subscriptions from the database for the given subscriber.
 *
 * @param sub_transport - The transport information for the subscriber.
 */
void
data_remove_subscriber (const cmsg_transport_info *sub_transport)
{
    /* todo */
}

/**
 * Helper function called for each remote subscription that has been subscribed
 * to from a subscriber running locally. If the remote subscription is in fact local
 * then it will be converted to a local subscription.
 *
 * @param data - The 'cmsg_pssd_subscription_info' structure for a remote subscription.
 * @param user_data - NULL.
 */
static void
_data_check_remote_entries (gpointer data, gpointer user_data)
{
    cmsg_pssd_subscription_info *entry = (cmsg_pssd_subscription_info *) data;

    if (!data_add_subscription (entry, false))
    {
        /* The subscription is now local so we need to free the original message. */
        CMSG_FREE_RECV_MSG (entry);
    }
}

/**
 * Check all existing remote subscriptions and check whether they are in fact local
 * subscriptions.
 */
void
data_check_remote_entries (void)
{
    GList *list = remote_subscriptions_list;

    remote_subscriptions_list = NULL;
    g_list_foreach (list, _data_check_remote_entries, NULL);
    g_list_free (list);
}

/**
 * Initialise the data layer.
 */
void
data_init (void)
{
    local_subscriptions_table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                                       NULL);
    if (!local_subscriptions_table)
    {
        syslog (LOG_ERR, "Failed to initialize hash table");
        return;
    }
}

/**
 * Print the information about a TCP transport.
 *
 * @param fp - The file to print to.
 * @param transport - The 'cmsg_transport' structure for the transport.
 */
static void
tcp_transport_dump (FILE *fp, const cmsg_transport *transport)
{
    char ip[INET6_ADDRSTRLEN] = { };
    uint16_t port;

    if (transport->config.socket.family != PF_INET6)
    {
        port = transport->config.socket.sockaddr.in.sin_port;
        inet_ntop (AF_INET, &transport->config.socket.sockaddr.in.sin_addr.s_addr,
                   ip, INET6_ADDRSTRLEN);
    }
    else
    {
        port = transport->config.socket.sockaddr.in6.sin6_port;
        inet_ntop (AF_INET6, &transport->config.socket.sockaddr.in6.sin6_addr.s6_addr,
                   ip, INET6_ADDRSTRLEN);
    }

    fprintf (fp, "     (tcp) %s:%u\n", ip, port);
}

/**
 * Helper function called for each transport subscribed to a method.
 * Prints the transport details of the transport.
 *
 * @param data - The 'subscriber_data_entry' structure for the subscriber.
 * @param user_data - The file to print to.
 */
static void
transports_dump (gpointer data, gpointer user_data)
{
    FILE *fp = (FILE *) user_data;
    const cmsg_transport *transport = (const cmsg_transport *) data;

    if (transport->type == CMSG_TRANSPORT_ONEWAY_UNIX ||
        transport->type == CMSG_TRANSPORT_RPC_UNIX)
    {
        fprintf (fp, "     (unix) path = %s\n",
                 transport->config.socket.sockaddr.un.sun_path);
    }
    else if (transport->type == CMSG_TRANSPORT_ONEWAY_TCP ||
             transport->type == CMSG_TRANSPORT_RPC_TCP)
    {
        tcp_transport_dump (fp, transport);
    }
    else if (transport->type == CMSG_TRANSPORT_ONEWAY_TIPC ||
             transport->type == CMSG_TRANSPORT_RPC_TIPC)
    {
        fprintf (fp, "     (tipc) instance:%u\n",
                 transport->config.socket.sockaddr.tipc.addr.name.name.instance);
    }
}

/**
 * Helper function called for each method that has been subscribed to from
 * a service running locally.
 *
 * @param data - The 'method_data_entry' structure for a method.
 * @param user_data - The file to print to.
 */
static void
methods_data_dump (gpointer data, gpointer user_data)
{
    FILE *fp = (FILE *) user_data;
    const method_data_entry *entry = (const method_data_entry *) data;

    fprintf (fp, "   %s:\n", entry->method_name);
    fprintf (fp, "    subscribers:\n");
    g_list_foreach (entry->transports, transports_dump, fp);
}

/**
 * Helper function called for each entry in the hash table. Prints the
 * information about each local service that is subscribed for.
 *
 * @param key - The key from the hash table (service name)
 * @param value - The value associated with the key (service_data_entry structure)
 * @param user_data - The file to print to.
 */
static void
local_subscriptions_dump (gpointer key, gpointer value, gpointer user_data)
{
    FILE *fp = (FILE *) user_data;
    service_data_entry *entry = (service_data_entry *) value;

    fprintf (fp, " service: %s\n", (char *) key);
    fprintf (fp, "  methods:\n");
    g_list_foreach (entry->methods, methods_data_dump, fp);
}

/**
 * Dump the information about the transport used for a remote subscription.
 *
 * @param transport_info - The 'cmsg_transport_info' message for the transport.
 * @param fp - The file to print to.
 */
static void
transport_info_dump (cmsg_transport_info *transport_info, FILE *fp)
{
    cmsg_tipc_transport_info *tipc_info = NULL;
    cmsg_tcp_transport_info *tcp_info = NULL;
    char ip[INET6_ADDRSTRLEN] = { };

    if (transport_info->type == CMSG_TRANSPORT_INFO_TYPE_TCP)
    {
        tcp_info = transport_info->tcp_info;
        inet_ntop (AF_INET, tcp_info->addr.data, ip, INET6_ADDRSTRLEN);

        fprintf (fp, " transport: (tcp) %s:%u\n", ip, tcp_info->port);
    }
    else if (transport_info->type == CMSG_TRANSPORT_INFO_TYPE_TIPC)
    {
        tipc_info = transport_info->tipc_info;
        fprintf (fp, " transport: (tipc) instance:%u\n",
                 tipc_info->addr_name_name_instance);
    }
}

/**
 * Helper function called for each remote subscription that has been subscribed
 * to from a subscriber running locally.
 *
 * @param data - The 'cmsg_pssd_subscription_info' structure for a remote subscription.
 * @param user_data - The file to print to.
 */
static void
remote_subscription_dump (gpointer data, gpointer user_data)
{
    FILE *fp = (FILE *) user_data;
    const cmsg_pssd_subscription_info *entry = (const cmsg_pssd_subscription_info *) data;
    char ip[INET6_ADDRSTRLEN] = { };

    inet_ntop (AF_INET, &entry->remote_addr, ip, INET6_ADDRSTRLEN);

    fprintf (fp, " service: %s\n", entry->service);
    fprintf (fp, " method name: %s\n", entry->method_name);
    fprintf (fp, " remote address: %s\n", ip);
    transport_info_dump (entry->transport_info, fp);
    fprintf (fp, "\n");
}

/**
 * Dump the current information about all known subscriptions to the debug file.
 *
 * @param fp - The file to print to.
 */
void
data_debug_dump (FILE *fp)
{
    fprintf (fp, "Local subscriptions:\n");
    g_hash_table_foreach (local_subscriptions_table, local_subscriptions_dump, fp);
    fprintf (fp, "\n");
    fprintf (fp, "Remote subscriptions:\n");
    g_list_foreach (remote_subscriptions_list, remote_subscription_dump, fp);
}
