/**
 * data.c
 *
 * Implements the storage of the information about services running
 * locally as well as on remote members.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include <cmsg/cmsg_glib_helpers.h>
#include "configuration_impl_auto.h"
#include "remote_sync.h"

typedef struct _service_data_entry_s
{
    GList *servers;
    GList *subscribers;
} service_data_entry;

static GHashTable *hash_table = NULL;

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

    entry = (service_data_entry *) g_hash_table_lookup (hash_table, server_info->service);
    if (entry)
    {
        entry->servers = g_list_append (entry->servers, (gpointer) server_info);
    }
    else
    {
        entry = CMSG_CALLOC (1, sizeof (service_data_entry));
        entry->servers = g_list_append (entry->servers, (gpointer) server_info);
        g_hash_table_insert (hash_table, server_info->service, entry);
    }

    /* todo: notify subscribers */
    /* todo: remote sync */
}

/**
 * Compares two 'cmsg_transport_info' structures for equality.
 *
 * @param transport_info_a - The first structure to compare.
 * @param transport_info_b - The second structure to compare.
 *
 * @returns true if they are equal, false otherwise.
 */
static gint
find_server (gconstpointer a, gconstpointer b)
{
    cmsg_service_info *service_a = (cmsg_service_info *) a;
    cmsg_service_info *service_b = (cmsg_service_info *) b;
    cmsg_transport_info *transport_info_a = service_a->server_info;
    cmsg_transport_info *transport_info_b = service_b->server_info;

    if (transport_info_a->type != transport_info_b->type ||
        transport_info_a->one_way != transport_info_b->one_way)
    {
        return -1;
    }

    if (transport_info_a->type == CMSG_TRANSPORT_INFO_TYPE_TCP)
    {
        cmsg_tcp_transport_info *tcp_info_a = transport_info_a->tcp_info;
        cmsg_tcp_transport_info *tcp_info_b = transport_info_b->tcp_info;

        if (tcp_info_a->ipv4 == tcp_info_b->ipv4 &&
            tcp_info_a->port == tcp_info_b->port &&
            !memcmp (tcp_info_a->addr.data, tcp_info_b->addr.data, tcp_info_a->addr.len))
        {
            return 0;
        }
        return -1;
    }

    if (transport_info_a->type == CMSG_TRANSPORT_INFO_TYPE_UNIX)
    {
        cmsg_unix_transport_info *unix_info_a = transport_info_a->unix_info;
        cmsg_unix_transport_info *unix_info_b = transport_info_b->unix_info;

        return strcmp (unix_info_a->path, unix_info_b->path);
    }

    return -1;
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

    /* todo: notify subscribers */
    /* todo: remote sync */
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
