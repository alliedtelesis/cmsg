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
#include "transport/cmsg_transport_private.h"

typedef struct
{
    cmsg_transport_info *transport;
    GList *methods;
} subscriber_data_entry;

typedef struct
{
    uint32_t addr;
    bool addr_is_tcp;
    GList *subscribers;
} host_data_entry;

typedef struct
{
    host_data_entry local_host;
    GList *remote_hosts;
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
        g_hash_table_insert (hash_table, g_strdup (service), entry);
    }

    return entry;
}

/**
 * Helper function for use with 'g_list_find_custom'. Specifically compares a
 * 'host_data_entry' structure with a 'cmsg_pssd_subscription_info' message
 * to see if they refer to the same remote host.
 *
 * @param a - The 'host_data_entry' structure to use in the comparison.
 * @param b - The 'cmsg_pssd_subscription_info' message to use in the comparison.
 *
 * @returns 0 if they refer to the same remote host, -1 otherwise.
 */
static gint
remote_host_entry_compare (gconstpointer a, gconstpointer b)
{
    host_data_entry *entry = (host_data_entry *) a;
    const cmsg_pssd_subscription_info *info = (const cmsg_pssd_subscription_info *) b;

    if ((entry->addr_is_tcp && info->remote_addr_is_tcp) ||
        (!entry->addr_is_tcp && !info->remote_addr_is_tcp))
    {
        return (entry->addr == info->remote_addr ? 0 : -1);
    }

    return -1;
}

/**
 * Gets the 'host_data_entry' structure for the given 'cmsg_pssd_subscription_info'
 * message or creates one if it doesn't already exist.
 *
 * @param service_entry - The 'service_data_entry' to get the host data entry from.
 * @param info - The 'cmsg_pssd_subscription_info' to get the host data entry for.
 *
 * @returns A pointer to the related 'host_data_entry' structure.
 */
static host_data_entry *
get_host_entry_or_create (service_data_entry *service_entry,
                          const cmsg_pssd_subscription_info *info)
{
    GList *list_entry = NULL;
    host_data_entry *entry = NULL;

    if (!CMSG_IS_FIELD_PRESENT (info, remote_addr))
    {
        return &service_entry->local_host;
    }

    list_entry = g_list_find_custom (service_entry->remote_hosts, info,
                                     remote_host_entry_compare);
    if (list_entry)
    {
        entry = (host_data_entry *) list_entry->data;
    }
    else
    {
        entry = CMSG_CALLOC (1, sizeof (host_data_entry));
        entry->addr = info->remote_addr;
        entry->addr_is_tcp = info->remote_addr_is_tcp;
        service_entry->remote_hosts = g_list_prepend (service_entry->remote_hosts, entry);
    }

    return entry;
}

/**
 * Helper function for use with 'g_list_find_custom'. Specifically compares a
 * 'subscriber_data_entry' structure with a 'cmsg_transport_info' message to
 * see if they refer to the same subscriber.
 *
 * @param a - The 'subscriber_data_entry' structure to use in the comparison.
 * @param b - The 'cmsg_transport_info' message to use in the comparison.
 *
 * @returns 0 if they refer to the same subscriber, -1 otherwise.
 */
static gint
subscriber_entry_compare (gconstpointer a, gconstpointer b)
{
    bool ret;

    subscriber_data_entry *entry = (subscriber_data_entry *) a;
    cmsg_transport_info *transport_info = (cmsg_transport_info *) b;

    ret = cmsg_transport_info_compare (entry->transport, transport_info);

    return (ret ? 0 : -1);
}

/**
 * Gets the 'subscriber_data_entry' structure for the given 'cmsg_transport_info'
 * message or creates one if it doesn't already exist.
 *
 * @param host_entry - The 'host_data_entry' to get the subscriber data entry from.
 * @param transport_info - The 'cmsg_transport_info' to get the subscriber data entry for.
 *
 * @returns A pointer to the related 'subscriber_data_entry' structure.
 */
static subscriber_data_entry *
get_subscriber_entry_or_create (host_data_entry *host_entry,
                                cmsg_transport_info *transport_info)
{
    GList *list_entry = NULL;
    subscriber_data_entry *entry = NULL;

    list_entry = g_list_find_custom (host_entry->subscribers, transport_info,
                                     subscriber_entry_compare);
    if (list_entry)
    {
        entry = (subscriber_data_entry *) list_entry->data;
    }
    else
    {
        entry = CMSG_CALLOC (1, sizeof (subscriber_data_entry));
        entry->transport = transport_info;
        host_entry->subscribers = g_list_prepend (host_entry->subscribers, entry);
    }

    return entry;
}

/**
 * Add a new subscription to the database.
 *
 * @param info - The information about the subscription being added.
 *
 * Note that this function steals the memory of the 'transport_info' field
 * inside the 'cmsg_pssd_subscription_info' message passed in. The field will
 * be NULL once this function returns.
 */
void
data_add_subscription (const cmsg_pssd_subscription_info *info)
{
    service_data_entry *service_entry = NULL;
    host_data_entry *host_entry = NULL;
    subscriber_data_entry *subscriber_entry = NULL;
    cmsg_transport_info *transport_info = NULL;

    /* Steal the memory of the transport info from the passed in message. */
    transport_info = (cmsg_transport_info *) info->transport_info;
    ((cmsg_pssd_subscription_info *) info)->transport_info = NULL;

    service_entry = get_service_entry_or_create (info->service);
    host_entry = get_host_entry_or_create (service_entry, info);
    subscriber_entry = get_subscriber_entry_or_create (host_entry, transport_info);

    subscriber_entry->methods = g_list_prepend (subscriber_entry->methods,
                                                strdup (info->method_name));
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
 * Initialise the data layer.
 */
void
data_init (void)
{
    hash_table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    if (!hash_table)
    {
        syslog (LOG_ERR, "Failed to initialize hash table");
        return;
    }
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

    fprintf (fp, "     (unix, %s) path = %s\n", oneway_str,
             transport_info->unix_info->path);
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

    fprintf (fp, "     (tcp, %s) %s:%u\n", oneway_str, ip, port);
}

/**
 * Print the information about a TIPC transport.
 *
 * @param fp - The file to print to.
 * @param transport_info - The 'cmsg_transport_info' structure for the transport.
 * @param oneway_str - String to print for the oneway/rpc nature of the transport.
 */
static void
data_debug_tipc_server_dump (FILE *fp, const cmsg_transport_info *transport_info,
                             const char *oneway_str)
{
    cmsg_tipc_transport_info *tipc_info = transport_info->tipc_info;

    fprintf (fp, "     (tipc, %s) instance:%u\n", oneway_str,
             tipc_info->addr_name_name_instance);
}

/**
 * Helper function called for each method that has been subscribed for. Simply prints
 * the method name to the given file.
 *
 * @param data - The method name to print.
 * @param user_data - The file to print to.
 */
static void
data_debug_method_dump (gpointer data, gpointer user_data)
{
    FILE *fp = (FILE *) user_data;
    const char *method_name = (const char *) data;

    fprintf (fp, "       %s\n", method_name);
}

/**
 * Helper function called for each subscriber. Prints the transport details of the
 * subscriber and lists all the methods it is subscribed for.
 *
 * @param data - The 'subscriber_data_entry' structure for the subscriber.
 * @param user_data - The file to print to.
 */
static void
data_debug_subscriber_dump (gpointer data, gpointer user_data)
{
    FILE *fp = (FILE *) user_data;
    const subscriber_data_entry *entry = (const subscriber_data_entry *) data;
    const cmsg_transport_info *transport_info = entry->transport;
    const char *oneway_str = (transport_info->one_way ? "one-way" : "rpc");

    if (transport_info->type == CMSG_TRANSPORT_INFO_TYPE_UNIX)
    {
        data_debug_unix_server_dump (fp, transport_info, oneway_str);
    }
    else if (transport_info->type == CMSG_TRANSPORT_INFO_TYPE_TCP)
    {
        data_debug_tcp_server_dump (fp, transport_info, oneway_str);
    }
    else if (transport_info->type == CMSG_TRANSPORT_INFO_TYPE_TIPC)
    {
        data_debug_tipc_server_dump (fp, transport_info, oneway_str);
    }

    fprintf (fp, "      methods:\n");
    g_list_foreach (entry->methods, data_debug_method_dump, fp);
}

/**
 * Helper function called for each remote host that has been subscribed to from
 * any subscriber running locally. Prints the address of the remote host and then
 * lists all subscribers that are subscribed to a service running on that host.
 *
 * @param data - The 'cmsg_service_info' structure for the server.
 * @param user_data - The file to print to.
 */
static void
data_debug_remote_host_dump (gpointer data, gpointer user_data)
{
    FILE *fp = (FILE *) user_data;
    const host_data_entry *entry = (const host_data_entry *) data;
    char ip[INET6_ADDRSTRLEN] = { };

    if (entry->addr_is_tcp)
    {
        inet_ntop (AF_INET, &entry->addr, ip, INET6_ADDRSTRLEN);
        fprintf (fp, "   remote_host: %s (%s):\n", ip, "tcp");
    }
    else
    {
        fprintf (fp, "   remote_host: %u (%s):\n", entry->addr, "tipc");

    }

    fprintf (fp, "    subscribers:\n");
    g_list_foreach (entry->subscribers, data_debug_subscriber_dump, fp);
}

/**
 * Prints the subscribers for the services running locally.
 *
 * @param entry - The 'host_data_entry' structure for the local service.
 * @param fp - The file to print to.
 */
static void
data_debug_local_host_dump (const host_data_entry *entry, FILE *fp)
{
    fprintf (fp, "   local host:\n");
    fprintf (fp, "    subscribers:\n");
    g_list_foreach (entry->subscribers, data_debug_subscriber_dump, fp);
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
    fprintf (fp, "  hosts:\n");
    data_debug_local_host_dump (&entry->local_host, fp);

    g_list_foreach (entry->remote_hosts, data_debug_remote_host_dump, fp);
}

/**
 * Dump the current information about all known subscriptions to the debug file.
 *
 * @param fp - The file to print to.
 */
void
data_debug_dump (FILE *fp)
{
    fprintf (fp, "Services:\n");
    g_hash_table_foreach (hash_table, _data_debug_dump, fp);
}
