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
#include "data.h"

enum pid_status
{
    UNCHECKED = (intptr_t) NULL,
    DEAD = 1,
    ALIVE = 2,
};

typedef struct _service_lookup_data
{
    GList *list;
    uint32_t addr;
} service_lookup_data;

GHashTable *hash_table = NULL;
static guint cleanup_dead_entries_source = 0;

/**
 * Called for each entry in the servers list of a service entry.
 * Simply frees all memory used by the server entry.
 *
 * @param data - The entry to free.
 */
static void
service_entry_free_servers (gpointer data)
{
    cmsg_service_info *info = (cmsg_service_info *) data;
    CMSG_FREE_RECV_MSG (info);
}

/**
 * Called for each entry in the listeners list of a service entry.
 * Simply frees all memory used by the listener_data entry.
 *
 * @param data - The listener_data entry to free.
 */
static void
service_entry_free_listeners (gpointer data)
{
    listener_data *listener_info = (listener_data *) data;

    cmsg_destroy_client_and_transport (listener_info->client);
    CMSG_FREE (listener_info);
}

/**
 * Frees all memory used by the given service entry.
 *
 * @param data - The service entry to free.
 */
static void
service_entry_free (gpointer data)
{
    service_data_entry *entry = (service_data_entry *) data;

    g_list_free_full (entry->servers, service_entry_free_servers);
    entry->servers = NULL;
    g_list_free_full (entry->listeners, service_entry_free_listeners);
    entry->listeners = NULL;
    CMSG_FREE (entry);
}

/**
 * Gets the 'service_data_entry' structure for the given service name
 * or potentially creates one if it doesn't already exist.
 *
 * @param service - The name of the service.
 * @param create - Whether to create an entry if one didn't already exist or not.
 *
 * @returns A pointer to the related 'service_data_entry' structure.
 */
service_data_entry *
get_service_entry_or_create (const char *service, bool create)
{
    service_data_entry *entry = NULL;

    entry = (service_data_entry *) g_hash_table_lookup (hash_table, service);
    if (!entry && create)
    {
        entry = CMSG_CALLOC (1, sizeof (service_data_entry));
        g_hash_table_insert (hash_table, CMSG_STRDUP (service), entry);
    }

    return entry;
}

/**
 * Notify all listeners of a given service about a server that has been added
 * or removed for that service.
 *
 * @param server_info - The information about the server that has been added/removed.
 * @param entry - The 'service_data_entry' containing the list of listeners.
 * @param added - Whether the server has been added or removed.
 */
static void
notify_listeners (const cmsg_service_info *server_info, service_data_entry *entry,
                  bool added)
{
    GList *list = NULL;
    cmsg_client *client = NULL;
    GList *removal_list = NULL;
    listener_data *listener_info = NULL;
    int ret;
    cmsg_sld_server_event send_msg = CMSG_SLD_SERVER_EVENT_INIT;

    for (list = g_list_first (entry->listeners); list; list = g_list_next (list))
    {
        listener_info = (listener_data *) list->data;
        client = listener_info->client;

        CMSG_SET_FIELD_PTR (&send_msg, service_info, (void *) server_info);
        CMSG_SET_FIELD_VALUE (&send_msg, id, listener_info->id);

        if (added)
        {
            ret = cmsg_sld_events_api_server_added (client, &send_msg);
        }
        else
        {
            ret = cmsg_sld_events_api_server_removed (client, &send_msg);
        }

        if (ret != CMSG_RET_OK)
        {
            removal_list = g_list_prepend (removal_list, listener_info);
        }
    }

    /* Remove any clients that we failed to send events to */
    for (list = g_list_first (removal_list); list; list = g_list_next (list))
    {
        listener_info = (listener_data *) list->data;

        entry->listeners = g_list_remove (entry->listeners, listener_info);
        cmsg_destroy_client_and_transport (listener_info->client);
        CMSG_FREE (listener_info);
    }
    g_list_free (removal_list);
}

/**
 * Add a newly created server to the database of servers running
 * for services.
 *
 * @param server_info - The information about the server being added.
 * @param local - True if the server is running on this device,
 *                false if it is remote.
 */
void
data_add_server (cmsg_service_info *server_info, bool local)
{
    service_data_entry *entry = NULL;

    CMSG_SET_FIELD_VALUE (server_info, local, local);

    /* Remove the server in case it already exists. This should only
     * occur if the server was previously removed without notifying the
     * service listener daemon (i.e. process crash). This ensures listeners
     * will get the server removed notification before it is added again. */
    data_remove_server (server_info);

    entry = get_service_entry_or_create (server_info->service, true);
    entry->servers = g_list_prepend (entry->servers, (gpointer) server_info);

    notify_listeners (server_info, entry, true);
    remote_sync_server_added (server_info);
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

static void
data_remove_service_data_entry_if_empty (service_data_entry *entry, const char *key)
{
    if (!entry->servers && !entry->listeners)
    {
        g_hash_table_remove (hash_table, key);
    }
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

    entry = get_service_entry_or_create (server_info->service, false);
    if (entry)
    {
        list_entry = g_list_find_custom (entry->servers, server_info, find_server);
        if (list_entry)
        {
            CMSG_FREE_RECV_MSG (list_entry->data);
            entry->servers = g_list_delete_link (entry->servers, list_entry);

            notify_listeners (server_info, entry, false);
            remote_sync_server_removed (server_info);

            data_remove_service_data_entry_if_empty (entry, server_info->service);
        }
    }
}

/**
 * Helper function called for each entry in the hash table. Finds any
 * servers for a service that match an IP address and deletes them.
 *
 * @param key - The key from the hash table (service name)
 * @param value - The value associated with the key (service_data_entry structure)
 * @param user_data - The address to match against.
 *
 * @returns TRUE if the service data entry is now empty and should be removed from the
 *          hash table, FALSE otherwise.
 */
static gboolean
_delete_server_by_addr (gpointer key, gpointer value, gpointer user_data)
{
    uint32_t *addr = (uint32_t *) user_data;
    service_data_entry *entry = (service_data_entry *) value;
    GList *list = NULL;
    cmsg_service_info *service_info = NULL;
    GList *removal_list = NULL;

    for (list = g_list_first (entry->servers); list; list = g_list_next (list))
    {
        service_info = (cmsg_service_info *) list->data;

        if (service_info->server_info->type == CMSG_TRANSPORT_INFO_TYPE_TCP)
        {
            cmsg_tcp_transport_info *info = service_info->server_info->tcp_info;

            if (info->ipv4 && !memcmp (info->addr.data, addr, info->addr.len))
            {
                removal_list = g_list_prepend (removal_list, service_info);
            }
        }
    }

    for (list = g_list_first (removal_list); list; list = g_list_next (list))
    {
        service_info = (cmsg_service_info *) list->data;

        entry->servers = g_list_remove (entry->servers, service_info);
        notify_listeners (service_info, entry, false);
        remote_sync_server_removed (service_info);
        CMSG_FREE_RECV_MSG (service_info);
    }
    g_list_free (removal_list);

    if (entry->servers || entry->listeners)
    {
        return FALSE;
    }

    return TRUE;
}

/**
 * Remove any servers that match the given address from the hash table.
 *
 * @param addr - The IP address to match against.
 */
void
data_remove_servers_by_addr (struct in_addr addr)
{
    g_hash_table_foreach_remove (hash_table, _delete_server_by_addr, &addr.s_addr);
}

/**
 * Add a new listener for a service.
 *
 * @param info - Information about the listener and the service
 *               they are listening for.
 */
void
data_add_listener (const cmsg_sld_listener_info *info)
{
    service_data_entry *entry = NULL;
    cmsg_transport *transport = NULL;
    GList *list = NULL;
    cmsg_service_info *server_info = NULL;
    cmsg_client *client = NULL;
    listener_data *listener_info = NULL;
    cmsg_sld_server_event send_msg = CMSG_SLD_SERVER_EVENT_INIT;

    entry = get_service_entry_or_create (info->service, true);
    transport = cmsg_transport_info_to_transport (info->transport_info);
    client = cmsg_client_new (transport, CMSG_DESCRIPTOR (cmsg_sld, events));

    listener_info = CMSG_CALLOC (1, sizeof (listener_data));
    listener_info->client = client;
    listener_info->id = info->id;
    listener_info->pid = info->pid;

    entry->listeners = g_list_prepend (entry->listeners, listener_info);

    for (list = g_list_first (entry->servers); list; list = g_list_next (list))
    {
        server_info = (cmsg_service_info *) list->data;

        CMSG_SET_FIELD_PTR (&send_msg, service_info, server_info);
        CMSG_SET_FIELD_VALUE (&send_msg, id, listener_info->id);

        if (cmsg_sld_events_api_server_added (client, &send_msg) != CMSG_RET_OK)
        {
            entry->listeners = g_list_remove (entry->listeners, listener_info);
            cmsg_destroy_client_and_transport (listener_info->client);
            CMSG_FREE (listener_info);
            break;
        }
    }
}

/**
 * Remove a listener for a service.
 *
 * @param info - Information about the listener and the service
 *               they are unlistening from.
 */
void
data_remove_listener (const cmsg_sld_listener_info *info)
{
    service_data_entry *entry = NULL;
    GList *list = NULL;
    cmsg_transport_info *transport_info = NULL;
    listener_data *listener_info = NULL;

    entry = get_service_entry_or_create (info->service, false);
    if (!entry)
    {
        return;
    }

    for (list = g_list_first (entry->listeners); list; list = g_list_next (list))
    {
        listener_info = (listener_data *) list->data;

        transport_info = cmsg_transport_info_create (listener_info->client->_transport);

        if (cmsg_transport_info_compare (info->transport_info, transport_info))
        {
            cmsg_transport_info_free (transport_info);
            break;
        }
        cmsg_transport_info_free (transport_info);
        listener_info = NULL;
    }

    if (listener_info)
    {
        entry->listeners = g_list_remove (entry->listeners, listener_info);
        cmsg_destroy_client_and_transport (listener_info->client);
        CMSG_FREE (listener_info);
        data_remove_service_data_entry_if_empty (entry, info->service);
    }
}

/**
 * Helper function called for each entry in the hash table. Finds any
 * servers for a service that match an IP address and stores them in a
 * GList.
 *
 * @param key - The key from the hash table (service name)
 * @param value - The value associated with the key (service_data_entry structure)
 * @param user_data - Pointer to a 'service_lookup_data' structure containing the
 *                    list to store the servers in and the address to match against.
 */
static void
_get_servers_by_addr (gpointer key, gpointer value, gpointer user_data)
{
    service_lookup_data *lookup_data = (service_lookup_data *) user_data;
    service_data_entry *entry = (service_data_entry *) value;
    GList *list = NULL;
    cmsg_service_info *service_info = NULL;

    for (list = g_list_first (entry->servers); list; list = g_list_next (list))
    {
        service_info = (cmsg_service_info *) list->data;

        if (service_info->server_info->type == CMSG_TRANSPORT_INFO_TYPE_TCP)
        {
            cmsg_tcp_transport_info *info = service_info->server_info->tcp_info;

            if (info->ipv4 && !memcmp (info->addr.data, &lookup_data->addr, info->addr.len))
            {
                lookup_data->list = g_list_prepend (lookup_data->list, service_info);
            }
        }
    }
}

/**
 * Get a list of all servers for a given address.
 *
 * @param addr - The address to get the servers for.
 *
 * @returns A GList containing all of the servers. This list should be freed
 *          by the caller using 'g_list_free'.
 */
GList *
data_get_servers_by_addr (uint32_t addr)
{
    service_lookup_data lookup_data;

    lookup_data.list = NULL;
    lookup_data.addr = addr;

    g_hash_table_foreach (hash_table, _get_servers_by_addr, &lookup_data);

    return lookup_data.list;
}

/**
 * Check whether the process with the given PID is still alive.
 *
 * @param pid_lookup - The hash table used to cache results.
 * @param pid - The PID to check.
 *
 * @returns true if the process is dead, false otherwise.
 */
static bool
data_pid_is_dead (GHashTable *pid_lookup, int pid)
{
    /**
     * To improve performance we store either DEAD or ALIVE for the given
     * PID (key) in a hash table. If the key does not currently exist in the
     * hash table (we haven't checked this PID yet) then the lookup will
     * return UNCHECKED in which case we need to actually check the filesystem.
     */
    enum pid_status state = GPOINTER_TO_INT (g_hash_table_lookup (pid_lookup, &pid));
    if (state == UNCHECKED)
    {
        state = (kill (pid, 0) == 0) ? ALIVE : DEAD;
        g_hash_table_insert (pid_lookup, &pid, GINT_TO_POINTER (state));
    }

    return state == DEAD;
}

/**
 * Helper function called for each entry in the hash table. Finds any
 * servers and listeners that match a process ID that is no longer running
 * and deletes them.
 *
 * @param key - The key from the hash table (service name)
 * @param value - The value associated with the key (service_data_entry structure)
 * @param user_data - The PID lookup hash table.
 *
 * @returns TRUE if the service data entry is now empty and should be removed from the
 *          hash table, FALSE otherwise.
 */
static gboolean
data_remove_by_pid (gpointer key, gpointer value, gpointer user_data)
{
    GHashTable *pid_lookup = (GHashTable *) user_data;
    service_data_entry *entry = (service_data_entry *) value;
    GList *l = NULL;

    /* First iterate the listeners */
    l = entry->listeners;
    while (l != NULL)
    {
        GList *next = g_list_next (l);
        listener_data *listener_info = (listener_data *) l->data;
        if (data_pid_is_dead (pid_lookup, listener_info->pid))
        {
            cmsg_destroy_client_and_transport (listener_info->client);
            entry->listeners = g_list_delete_link (entry->listeners, l);
            CMSG_FREE (listener_info);
        }
        l = next;
    }

    /* Now iterate the servers */
    l = entry->servers;
    while (l != NULL)
    {
        GList *next = g_list_next (l);
        cmsg_service_info *service_info = (cmsg_service_info *) l->data;
        if (service_info->local && data_pid_is_dead (pid_lookup, service_info->pid))
        {
            notify_listeners (service_info, entry, false);
            remote_sync_server_removed (service_info);
            entry->servers = g_list_delete_link (entry->servers, l);
            CMSG_FREE_RECV_MSG (service_info);
        }
        l = next;
    }

    return (entry->servers == NULL && entry->listeners == NULL);
}

/**
 * Periodic function called to tidy up entries from processes
 * that are no longer running (and didn't unregister).
 *
 * @param unused - unused
 *
 * @returns TRUE always.
 */
static gboolean
data_cleanup_dead_entries (gpointer unused)
{
    GHashTable *pid_lookup = g_hash_table_new_full (g_int_hash, g_int_equal, NULL, NULL);

    g_hash_table_foreach_remove (hash_table, data_remove_by_pid, pid_lookup);

    g_hash_table_remove_all (pid_lookup);
    g_hash_table_unref (pid_lookup);
    return TRUE;
}

/**
 * Initialise the data layer.
 *
 * @param poll_pid - true to enable polling for dead processes, false otherwise.
 */
void
data_init (bool poll_pid)
{
    hash_table = g_hash_table_new_full (g_str_hash, g_str_equal, cmsg_free,
                                        service_entry_free);
    if (!hash_table)
    {
        syslog (LOG_ERR, "Failed to initialize hash table");
        return;
    }
    if (poll_pid)
    {
        cleanup_dead_entries_source = g_timeout_add_seconds (30, data_cleanup_dead_entries,
                                                             NULL);
    }
}

/**
 * Deinitialise the data layer.
 */
void
data_deinit (void)
{
    if (hash_table)
    {
        g_hash_table_remove_all (hash_table);
        g_hash_table_unref (hash_table);
        hash_table = NULL;
    }
    if (cleanup_dead_entries_source)
    {
        g_source_remove (cleanup_dead_entries_source);
        cleanup_dead_entries_source = 0;
    }
}

/**
 * Helper function called for each listener associated with a service. Prints the
 * information about each individual listener for a service.
 *
 * @param data - The client connected to the listener.
 * @param user_data - The file to print to.
 */
static void
data_debug_listener_dump (gpointer data, gpointer user_data)
{
    FILE *fp = (FILE *) user_data;
    const listener_data *listener_info = (const listener_data *) data;
    const cmsg_client *client = listener_info->client;

    fprintf (fp, "   %s (ID: %u) (pid: %u)\n",
             client->_transport->config.socket.sockaddr.un.sun_path, listener_info->id,
             listener_info->pid);
}

/**
 * Print the information about a UNIX transport.
 *
 * @param fp - The file to print to.
 * @param transport_info - The 'cmsg_transport_info' structure for the transport.
 * @param oneway_str - String to print for the oneway/rpc nature of the transport.
 * @param pid  - the PID of the process that runs this server.
 *
 */
static void
data_debug_unix_server_dump (FILE *fp, const cmsg_transport_info *transport_info,
                             const char *oneway_str, uint32_t pid)
{
    fprintf (fp, "   (unix, %s) path = %s (pid %u)\n", oneway_str,
             transport_info->unix_info->path, pid);
}

/**
 * Print the information about a TCP transport.
 *
 * @param fp - The file to print to.
 * @param transport_info - The 'cmsg_transport_info' structure for the transport.
 * @param oneway_str - String to print for the oneway/rpc nature of the transport.
 * @param pid  - the PID of the process that runs this server.
 *
 */
static void
data_debug_tcp_server_dump (FILE *fp, const cmsg_transport_info *transport_info,
                            const char *oneway_str, uint32_t pid)
{
    cmsg_tcp_transport_info *tcp_info = transport_info->tcp_info;
    char ip[INET6_ADDRSTRLEN] = { };
    uint16_t port;

    memcpy (&port, tcp_info->port.data, sizeof (port));

    if (tcp_info->ipv4)
    {
        inet_ntop (AF_INET, tcp_info->addr.data, ip, INET6_ADDRSTRLEN);
    }
    else
    {
        inet_ntop (AF_INET6, tcp_info->addr.data, ip, INET6_ADDRSTRLEN);
    }

    fprintf (fp, "   (tcp, %s) %s:%u (pid: %u)\n", oneway_str, ip, ntohs (port), pid);
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
        data_debug_unix_server_dump (fp, transport_info, oneway_str, service_info->pid);
    }
    else
    {
        data_debug_tcp_server_dump (fp, transport_info, oneway_str, service_info->pid);
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
    fprintf (fp, "  listeners:\n");
    g_list_foreach (entry->listeners, data_debug_listener_dump, fp);
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
