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
#include <cmsg/cmsg_composite_client.h>
#include "cmsg_client_private.h"

typedef struct
{
    char *method_name;
    cmsg_client *comp_client;
} method_data_entry;

typedef struct
{
    GList *methods;
} service_data_entry;

static GHashTable *local_subscriptions_table = NULL;
static GList *remote_subscriptions_list = NULL;

static const ProtobufCServiceDescriptor cmsg_psd_pub_descriptor = {
    PROTOBUF_C__SERVICE_DESCRIPTOR_MAGIC,
    "cmsg_psd.pub",
    "pub",
    "cmsg_psd_pub",
    "cmsg_psd",
    0,
    NULL,
    NULL,
};

/**
 * Gets the 'service_data_entry' structure for the given service name
 * or potentially create one if it doesn't already exist.
 *
 * @param service - The name of the service.
 * @param create - Whether to create an entry if one didn't already exist or not.
 *
 * @returns A pointer to the related 'service_data_entry' structure.
 */
static service_data_entry *
get_service_entry_or_create (const char *service, bool create)
{
    service_data_entry *entry = NULL;

    entry = (service_data_entry *) g_hash_table_lookup (local_subscriptions_table, service);
    if (!entry && create)
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
 * or potentially create one if it doesn't already exist.
 *
 * @param service_entry - The service entry to get the method entry from.
 * @param method - The name of the method to get the entry for.
 * @param create - Whether to create an entry if one didn't already exist or not.
 *
 * @returns A pointer to the related 'method_data_entry' structure.
 */
static method_data_entry *
get_method_entry_or_create (service_data_entry *service_entry, const char *method,
                            bool create)
{
    method_data_entry *entry = NULL;
    GList *list_entry = NULL;

    list_entry = g_list_find_custom (service_entry->methods, method, method_entry_compare);
    if (list_entry)
    {
        entry = (method_data_entry *) list_entry->data;
    }
    else if (create)
    {
        entry = CMSG_CALLOC (1, sizeof (method_data_entry));
        entry->method_name = CMSG_STRDUP (method);
        entry->comp_client = cmsg_composite_client_new (&cmsg_psd_pub_descriptor);
        service_entry->methods = g_list_prepend (service_entry->methods, entry);
    }

    return entry;
}


/**
 * Add a local subscription to the database.
 *
 * @param info - The information about the subscription being added.
 */
void
data_add_local_subscription (const cmsg_subscription_info *info)
{
    service_data_entry *service_entry = NULL;
    method_data_entry *method_entry = NULL;
    cmsg_transport *transport = NULL;
    cmsg_client *client = NULL;

    service_entry = get_service_entry_or_create (info->service, true);
    method_entry = get_method_entry_or_create (service_entry, info->method_name, true);
    transport = cmsg_transport_info_to_transport (info->transport_info);

    client = cmsg_client_create (transport, &cmsg_psd_pub_descriptor);
    cmsg_composite_client_add_child (method_entry->comp_client, client);
}

/**
 * Add a new subscription to the database. Note that this function may steal
 * the memory of the passed in 'cmsg_subscription_info' message (see the
 * return value for more information).
 *
 * @param info - The information about the subscription being added.
 * @param sync - If the subscription is remote set true to sync this subscription
 *               to the associated remote node, false to not sync.
 *
 * @returns true if the memory of the passed in 'cmsg_subscription_info'
 *          message is now stolen (the caller should not free the memory).
 *          false if the memory was not stolen (the caller is still responsible
 *          for the memory).
 */
bool
data_add_subscription (const cmsg_subscription_info *info)
{
    if (CMSG_IS_FIELD_PRESENT (info, remote_addr))
    {
        if (remote_sync_get_local_ip () == info->remote_addr)
        {
            syslog (LOG_ERR, "Incorrect subscription API used for %s service (method: %s)",
                    info->service, info->method_name);
        }

        remote_subscriptions_list = g_list_prepend (remote_subscriptions_list,
                                                    (void *) info);
        remote_sync_subscription_added (info);
        return true;
    }

    data_add_local_subscription (info);
    return false;
}

/**
 * Return 0 if two 'cmsg_subscription_info' messages are the same,
 * otherwise return -1.
 */
static gint
remote_subscription_compare (gconstpointer a, gconstpointer b)
{
    const cmsg_subscription_info *info_a = (const cmsg_subscription_info *) a;
    const cmsg_subscription_info *info_b = (const cmsg_subscription_info *) b;

    if (strcmp (info_a->service, info_b->service))
    {
        return -1;
    }

    if (strcmp (info_a->method_name, info_b->method_name))
    {
        return -1;
    }

    if (info_a->remote_addr != info_b->remote_addr)
    {
        return -1;
    }

    if (!cmsg_transport_info_compare (info_a->transport_info, info_b->transport_info))
    {
        return -1;
    }

    return 0;
}

/**
 * Remove a remote subscription from the database if it exists.
 *
 * @param info - The information about the subscription being removed.
 */
static void
data_remove_remote_subscription (const cmsg_subscription_info *info)
{
    GList *list_entry = NULL;

    list_entry = g_list_find_custom (remote_subscriptions_list, info,
                                     remote_subscription_compare);
    if (list_entry)
    {
        remote_subscriptions_list = g_list_remove (remote_subscriptions_list,
                                                   list_entry->data);
        remote_sync_subscription_removed (info);
        CMSG_FREE_RECV_MSG (list_entry->data);
    }
}

/**
 * Helper function called for a list of cmsg clients. Compares each client
 * with the given transport.
 *
 * @param a - The client.
 * @param b - The transport.
 *
 * @returns 0 if the given transport matches the transport of the client.
 *          -1 otherwise.
 */
static gint
cmsg_subscription_client_compare (gconstpointer a, gconstpointer b)
{
    const cmsg_client *client = (const cmsg_client *) a;
    const cmsg_transport *transport = (const cmsg_transport *) b;

    if (cmsg_transport_compare (client->_transport, transport))
    {
        return 0;
    }

    return -1;
}

/**
 * Remove a client from the composite client on a given method entry.
 *
 * @param method_entry - The method entry to remove the client from.
 * @param transport_info - A 'cmsg_transport_info' structure specifying
 *                         the client to remove.
 */
static void
data_remove_client_from_method (method_data_entry *method_entry,
                                const cmsg_transport_info *transport_info)
{
    cmsg_transport *transport = NULL;
    GList *list_entry = NULL;
    GList *client_list = cmsg_composite_client_get_children (method_entry->comp_client);
    cmsg_client *child_client = NULL;

    transport = cmsg_transport_info_to_transport (transport_info);
    list_entry = g_list_find_custom (client_list, transport,
                                     cmsg_subscription_client_compare);
    if (list_entry)
    {
        child_client = (cmsg_client *) list_entry->data;
        cmsg_composite_client_delete_child (method_entry->comp_client, child_client);
        cmsg_destroy_client_and_transport (child_client);
    }
    cmsg_transport_destroy (transport);
}

/**
 * Remove a local subscription from the database if it exists. If a subscription
 * is removed then the database is pruned accordingly to remove any empty service/
 * method entries.
 *
 * @param info - The information about the subscription being removed.
 */
void
data_remove_local_subscription (const cmsg_subscription_info *info)
{
    service_data_entry *service_entry = NULL;
    method_data_entry *method_entry = NULL;
    bool destroy_method_entry = false;
    bool destroy_service_entry = false;
    uint32_t num_clients = 0;

    service_entry = get_service_entry_or_create (info->service, false);
    if (service_entry)
    {
        method_entry = get_method_entry_or_create (service_entry, info->method_name, false);
        if (method_entry)
        {
            data_remove_client_from_method (method_entry, info->transport_info);
            num_clients = cmsg_composite_client_num_children (method_entry->comp_client);
            destroy_method_entry = (num_clients == 0);
        }
        if (destroy_method_entry)
        {
            service_entry->methods = g_list_remove (service_entry->methods, method_entry);
            CMSG_FREE (method_entry->method_name);
            cmsg_client_destroy (method_entry->comp_client);
            CMSG_FREE (method_entry);
            destroy_service_entry = (g_list_length (service_entry->methods) == 0);
        }
    }
    if (destroy_service_entry)
    {
        g_hash_table_remove (local_subscriptions_table, info->service);
        CMSG_FREE (service_entry);
    }
}

/**
 * Remove a subscription from the database.
 *
 * @param info - The information about the subscription being removed.
 */
void
data_remove_subscription (const cmsg_subscription_info *info)
{
    if (CMSG_IS_FIELD_PRESENT (info, remote_addr))
    {
        data_remove_remote_subscription (info);
        return;
    }

    data_remove_local_subscription (info);
}

/**
 * Remove all remote subscription entries for the given subscriber.
 *
 * @param sub_transport - The transport information for the subscriber.
 */
static void
data_remove_remote_entries_for_subscriber (const cmsg_transport_info *sub_transport)
{
    GList *list = NULL;
    GList *list_next = NULL;
    GList *removal_list = NULL;
    const cmsg_subscription_info *info = NULL;

    for (list = g_list_first (remote_subscriptions_list); list; list = list_next)
    {
        info = (const cmsg_subscription_info *) list->data;
        if (cmsg_transport_info_compare (info->transport_info, sub_transport))
        {
            removal_list = g_list_append (removal_list, (void *) info);
        }
        list_next = g_list_next (list);
    }

    for (list = g_list_first (removal_list); list; list = list_next)
    {
        remote_subscriptions_list = g_list_remove (remote_subscriptions_list, list->data);

        remote_sync_subscription_removed (list->data);
        CMSG_FREE_RECV_MSG (list->data);
        list_next = g_list_next (list);
    }
    g_list_free (removal_list);
}

/**
 * Helper function called for each method to potentially remove a specific
 * subscriber that is subscribed to it.
 *
 * @param data - The 'method_data_entry' structure for a method.
 * @param user_data - The transport information for the subscriber.
 */
static void
data_remove_subscriber_from_method (gpointer data, gpointer user_data)
{
    const cmsg_transport_info *sub_transport = (const cmsg_transport_info *) user_data;
    method_data_entry *entry = (method_data_entry *) data;

    data_remove_client_from_method (entry, sub_transport);
}

/**
 * Helper function called for each method. Updates the GList to include this
 * method if the composite client is empty.
 *
 * @param data - The 'method_data_entry' structure for a method.
 * @param user_data - Pointer to the GList to update.
 */
static void
data_find_methods_without_clients (gpointer data, gpointer user_data)
{
    GList **removal_list = (GList **) user_data;
    method_data_entry *entry = (method_data_entry *) data;
    uint32_t num_clients = 0;

    num_clients = cmsg_composite_client_num_children (entry->comp_client);
    if (num_clients == 0)
    {
        *removal_list = g_list_prepend (*removal_list, entry);
    }
}

/**
 * Prune any empty method entries from the given service data entry.
 *
 * @param entry - The service data entry to prune empty method entries from.
 */
static void
data_prune_empty_methods (service_data_entry *entry)
{
    GList *list = NULL;
    GList *list_next = NULL;
    GList *removal_list = NULL;
    method_data_entry *method_entry = NULL;

    g_list_foreach (entry->methods, data_find_methods_without_clients, &removal_list);
    for (list = g_list_first (removal_list); list; list = list_next)
    {
        method_entry = (method_data_entry *) list->data;
        entry->methods = g_list_remove (entry->methods, method_entry);
        CMSG_FREE (method_entry->method_name);
        CMSG_FREE (method_entry);
        list_next = g_list_next (list);
    }
    g_list_free (removal_list);
}

/**
 * Helper function called for each entry in the hash table. Removes
 * entries for the given subscriber.
 *
 * @param key - The key from the hash table (service name)
 * @param value - The value associated with the key (service_data_entry structure)
 * @param user_data - The transport information for the subscriber.
 */
static void
data_remove_local_entries_for_subscriber (gpointer key, gpointer value, gpointer user_data)
{
    service_data_entry *entry = (service_data_entry *) value;

    g_list_foreach (entry->methods, data_remove_subscriber_from_method, user_data);
    data_prune_empty_methods (entry);
}

/**
 * Helper function called for each entry in the hash table. Returns TRUE
 * if the service entry has an empty method list, meaning the entry should
 * be removed from the hash table.
 *
 * @param key - The key from the hash table (service name)
 * @param value - The value associated with the key (service_data_entry structure)
 * @param user_data - NULL.
 *
 * @returns TRUE if the entry should be removed from the hash table, FALSE otherwise.
 */
static gboolean
data_remove_empty_services (gpointer key, gpointer value, gpointer user_data)
{
    service_data_entry *entry = (service_data_entry *) value;

    if (g_list_length (entry->methods) == 0)
    {
        CMSG_FREE (entry);
        return TRUE;
    }

    return FALSE;
}

/**
 * Remove all subscriptions from the database for the given subscriber.
 *
 * @param sub_transport - The transport information for the subscriber.
 */
void
data_remove_subscriber (const cmsg_transport_info *sub_transport)
{
    data_remove_remote_entries_for_subscriber (sub_transport);
    g_hash_table_foreach (local_subscriptions_table,
                          data_remove_local_entries_for_subscriber, (void *) sub_transport);
    g_hash_table_foreach_remove (local_subscriptions_table, data_remove_empty_services,
                                 NULL);
}

/**
 * Remove any client from the composite client on a given method entry
 * if it has the address of the given remote host.
 *
 * @param method_entry - The method entry to remove the client from.
 * @param addr - The address specifying any client to remove.
 */
static void
data_remove_clients_with_addr_from_method (method_data_entry *method_entry, uint32_t addr)
{
    GList *list = NULL;
    GList *list_next = NULL;
    GList *removal_list = NULL;
    GList *client_list = cmsg_composite_client_get_children (method_entry->comp_client);
    cmsg_client *child_client = NULL;

    for (list = g_list_first (client_list); list; list = list_next)
    {
        child_client = (cmsg_client *) list->data;
        if (child_client->_transport->config.socket.sockaddr.in.sin_addr.s_addr == addr)
        {
            removal_list = g_list_append (removal_list, (void *) child_client);
        }
        list_next = g_list_next (list);
    }

    for (list = g_list_first (removal_list); list; list = list_next)
    {
        child_client = (cmsg_client *) list->data;
        cmsg_composite_client_delete_child (method_entry->comp_client, child_client);
        cmsg_destroy_client_and_transport (child_client);
        list_next = g_list_next (list);
    }
    g_list_free (removal_list);
}

/**
 * Helper function called for each method to potentially remove any
 * subscriber on a remote host that is subscribed to it.
 *
 * @param data - The 'method_data_entry' structure for a method.
 * @param user_data - The address of the remote host.
 */
static void
data_remove_subscribers_with_addr_from_method (gpointer data, gpointer user_data)
{
    uint32_t *addr = (uint32_t *) user_data;
    method_data_entry *entry = (method_data_entry *) data;

    data_remove_clients_with_addr_from_method (entry, *addr);
}

/**
 * Helper function called for each entry in the hash table. Removes entries
 * for any subscriber which is on a remote host with the given address.
 *
 * @param key - The key from the hash table (service name)
 * @param value - The value associated with the key (service_data_entry structure)
 * @param user_data - The address of the remote host.
 */
static void
data_remove_local_entries_for_addr (gpointer key, gpointer value, gpointer user_data)
{
    service_data_entry *entry = (service_data_entry *) value;

    g_list_foreach (entry->methods, data_remove_subscribers_with_addr_from_method,
                    user_data);
    data_prune_empty_methods (entry);
}

void
data_remove_local_subscriptions_for_addr (uint32_t addr)
{
    g_hash_table_foreach (local_subscriptions_table,
                          data_remove_local_entries_for_addr, (void *) &addr);
    g_hash_table_foreach_remove (local_subscriptions_table, data_remove_empty_services,
                                 NULL);
}

/**
 * Helper function called for each remote subscription that has been subscribed
 * to from a subscriber running locally. If the remote subscription is in fact for
 * the local address then an error will be logged to warn the library user.
 *
 * @param data - The 'cmsg_subscription_info' structure for a remote subscription.
 * @param user_data - NULL.
 */
static void
_data_check_remote_entries (gpointer data, gpointer user_data)
{
    cmsg_subscription_info *info = (cmsg_subscription_info *) data;

    if (CMSG_IS_FIELD_PRESENT (info, remote_addr) &&
        remote_sync_get_local_ip () == info->remote_addr)
    {
        syslog (LOG_ERR, "Incorrect subscription API used for %s service (method: %s)",
                info->service, info->method_name);
    }
}

/**
 * Check all existing remote subscriptions and check whether they are in fact local
 * subscriptions and log an error appropriately.
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
 * Get the list of remote subscriptions.
 *
 * @returns Pointer to the GList storing the remote subscriptions.
 */
GList *
data_get_remote_subscriptions (void)
{
    return remote_subscriptions_list;
}

/**
 * Publish a message for the given service and method name.
 *
 * @param service - The service to publish for.
 * @param method_name - The specific method that is publishing.
 * @param packet - The CMSG packet to send to all subscribers.
 * @param packet_len - The length of the CMSG packet to send.
 */
void
data_publish_message (const char *service, const char *method_name, uint8_t *packet,
                      uint32_t packet_len)
{
    service_data_entry *service_entry = NULL;
    method_data_entry *method_entry = NULL;

    service_entry = get_service_entry_or_create (service, false);
    if (service_entry)
    {
        method_entry = get_method_entry_or_create (service_entry, method_name, false);
        if (method_entry)
        {
            cmsg_client_send_bytes (method_entry->comp_client, packet, packet_len);
        }
    }
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
 * Helper function called for each client subscribed to a method.
 * Prints the transport details of the client.
 *
 * @param data - The 'subscriber_data_entry' structure for the subscriber.
 * @param user_data - The file to print to.
 */
static void
clients_dump (gpointer data, gpointer user_data)
{
    FILE *fp = (FILE *) user_data;
    const cmsg_client *client = (const cmsg_client *) data;
    const cmsg_transport *transport = client->_transport;

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
    GList *clients_list = NULL;

    clients_list = cmsg_composite_client_get_children (entry->comp_client);

    fprintf (fp, "   %s:\n", entry->method_name);
    fprintf (fp, "    subscribers:\n");
    g_list_foreach (clients_list, clients_dump, fp);
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
 * @param data - The 'cmsg_subscription_info' structure for a remote subscription.
 * @param user_data - The file to print to.
 */
static void
remote_subscription_dump (gpointer data, gpointer user_data)
{
    FILE *fp = (FILE *) user_data;
    const cmsg_subscription_info *entry = (const cmsg_subscription_info *) data;
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
