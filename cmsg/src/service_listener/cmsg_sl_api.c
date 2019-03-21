/**
 * cmsg_sl_api.c
 *
 * Implements the functions that can be used to interact with the service
 * listener daemon.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include "configuration_api_auto.h"
#include "cmsg_sl_config.h"
#include "cmsg_server_private.h"
#include "cmsg_sl.h"
#include "events_impl_auto.h"
#include "transport/cmsg_transport_private.h"

typedef struct _function_info_s
{
    char *service_name;
    cmsg_service_listener_event_func func;
} function_info;

static cmsg_server *event_server = NULL;
static GList *function_list = NULL;

/**
 * Look up the function entry from the function list based
 * on the service name.
 */
static gint
find_function_entry (gconstpointer a, gconstpointer b)
{
    const char *service_name = (const char *) a;
    function_info *info = (function_info *) b;

    return strcmp (service_name, info->service_name);
}

/**
 * Notification from the CMSG service listener daemon that a server for
 * a specific service has been added.
 */
void
events_impl_server_added (const void *service, const cmsg_service_info *recv_msg)
{
    GList *function_list_entry = NULL;
    function_info *info = NULL;

    function_list_entry = g_list_find_custom (function_list, recv_msg->service,
                                              find_function_entry);
    info = function_list_entry->data;

    info->func (recv_msg, true);

    events_server_server_addedSend (service);
}

/**
 * Notification from the CMSG service listener daemon that a server for
 * a specific service has been removed.
 */
void
events_impl_server_removed (const void *service, const cmsg_service_info *recv_msg)
{
    GList *function_list_entry = NULL;
    function_info *info = NULL;

    function_list_entry = g_list_find_custom (function_list, recv_msg->service,
                                              find_function_entry);
    info = function_list_entry->data;

    info->func (recv_msg, false);

    events_server_server_removedSend (service);
}

/**
 * Helper function for calling the API to the CMSG service listener
 * to add/remove a subscription for a given service.
 *
 * @param service_name - The name of the service to subscribe/unsubscibe for.
 * @param subscribe - true to subscribe, false to unsubscribe.
 */
static void
_cmsg_service_listener_subscribe (const char *service_name, bool subscribe)
{
    cmsg_client *client = NULL;
    subscription_info send_msg = SUBSCRIPTION_INFO_INIT;
    cmsg_transport_info *transport_info = NULL;

    if (!event_server)
    {
        event_server = cmsg_create_server_unix_oneway (CMSG_SERVICE_NOPACKAGE (events));
    }

    transport_info = cmsg_transport_info_create (event_server->_transport);

    CMSG_SET_FIELD_PTR (&send_msg, service, (char *) service_name);
    CMSG_SET_FIELD_PTR (&send_msg, subscriber_info, transport_info);

    client = cmsg_create_client_unix_oneway (CMSG_DESCRIPTOR_NOPACKAGE (configuration));

    if (subscribe)
    {
        configuration_api_subscribe (client, &send_msg);
    }
    else
    {
        configuration_api_unsubscribe (client, &send_msg);
    }

    cmsg_destroy_client_and_transport (client);
    cmsg_transport_info_free (transport_info);
}

/**
 * Subscribe for events for the given service name.
 *
 * @param service_name - The service to subscribe for.
 * @param func - The function to call when a server is added or removed
 *               for the given service.
 */
void
cmsg_service_listener_subscribe (const char *service_name,
                                 cmsg_service_listener_event_func func)
{
    GList *function_list_entry = NULL;
    function_info *info = NULL;

    function_list_entry = g_list_find_custom (function_list, service_name,
                                              find_function_entry);
    if (function_list_entry)
    {
        /* Already subscribed for this service */
        return;
    }

    info = CMSG_CALLOC (1, sizeof (function_info));
    if (!info)
    {
        return;
    }

    info->service_name = CMSG_STRDUP (service_name);
    info->func = func;

    function_list = g_list_append (function_list, info);

    _cmsg_service_listener_subscribe (service_name, true);

}

/**
 * Unsubscribe from events for the given service name.
 *
 * @param service_name - The service to unsubscribe from.
 */
void
cmsg_service_listener_unsubscribe (const char *service_name)
{
    GList *function_list_entry = NULL;
    function_info *info = NULL;

    function_list_entry = g_list_find_custom (function_list, service_name,
                                              find_function_entry);
    if (!function_list_entry)
    {
        /* No subscription exists for this service */
        return;
    }

    info = function_list_entry->data;
    function_list = g_list_delete_link (function_list, function_list_entry);
    CMSG_FREE (info->service_name);
    CMSG_FREE (info);

    _cmsg_service_listener_subscribe (service_name, false);
}

/**
 * Returns the server that can be used to receive service notifications
 * from the cmsg service listener. It is up to the library user to ensure
 * this server is run using the required threading library.
 *
 * @returns The CMSG server that receives service notifications
 */
cmsg_server *
cmsg_service_listener_subscription_server_get (void)
{
    if (!event_server)
    {
        event_server = cmsg_create_server_unix_oneway (CMSG_SERVICE_NOPACKAGE (events));
    }

    return event_server;
}

/**
 * Configure the IP address of the server running in the service listener
 * daemon. This is the address that remote hosts can connect to.
 *
 * @param addr - The address to configure.
 *
 * @returns true on success, false on error.
 */
bool
cmsg_service_listener_address_set (struct in_addr addr)
{
    cmsg_client *client = NULL;
    cmsg_uint32 send_msg = CMSG_UINT32_INIT;
    int ret;

    client = cmsg_create_client_unix_oneway (CMSG_DESCRIPTOR_NOPACKAGE (configuration));
    if (!client)
    {
        return false;
    }

    CMSG_SET_FIELD_VALUE (&send_msg, value, addr.s_addr);

    ret = configuration_api_address_set (client, &send_msg);
    cmsg_destroy_client_and_transport (client);

    return (ret == CMSG_RET_OK);
}

/**
 * Add a remote host to the service listener daemon. The daemon will then
 * connect to the service listener daemon running on the remote host and sync
 * the local service information to it.
 *
 * @param addr - The address of the remote node.
 *
 * @returns true on success, false on error.
 */
bool
cmsg_service_listener_add_host (struct in_addr addr)
{
    cmsg_client *client = NULL;
    cmsg_uint32 send_msg = CMSG_UINT32_INIT;
    int ret;

    client = cmsg_create_client_unix_oneway (CMSG_DESCRIPTOR_NOPACKAGE (configuration));
    if (!client)
    {
        return false;
    }

    CMSG_SET_FIELD_VALUE (&send_msg, value, addr.s_addr);

    ret = configuration_api_add_host (client, &send_msg);
    cmsg_destroy_client_and_transport (client);

    return (ret == CMSG_RET_OK);
}

/**
 * Removes a remote host from the service listener daemon. The daemon will then
 * remove the connection to the service listener daemon running on the remote host
 * and remove all service information for it.
 *
 * @param addr - The address of the remote node.
 *
 * @returns true on success, false on error.
 */
bool
cmsg_service_listener_delete_host (struct in_addr addr)
{
    cmsg_client *client = NULL;
    cmsg_uint32 send_msg = CMSG_UINT32_INIT;
    int ret;

    client = cmsg_create_client_unix_oneway (CMSG_DESCRIPTOR_NOPACKAGE (configuration));
    if (!client)
    {
        return false;
    }

    CMSG_SET_FIELD_VALUE (&send_msg, value, addr.s_addr);

    ret = configuration_api_delete_host (client, &send_msg);
    cmsg_destroy_client_and_transport (client);

    return (ret == CMSG_RET_OK);
}

/**
 * Tell the service listener daemon that a server implementing a specific service
 * is now running.
 *
 * @param server - The newly created server.
 */
void
cmsg_service_listener_add_server (cmsg_server *server)
{
    cmsg_client *client = NULL;
    cmsg_service_info *send_msg = NULL;

    send_msg = cmsg_server_service_info_create (server);
    if (send_msg)
    {
        client = cmsg_create_client_unix_oneway (CMSG_DESCRIPTOR_NOPACKAGE (configuration));
        configuration_api_add_server (client, send_msg);
        cmsg_destroy_client_and_transport (client);
        cmsg_server_service_info_free (send_msg);
    }
}

/**
 * Tell the service listener daemon that a server implementing a specific service
 * is no longer running.
 *
 * @param server - The server that is being deleted.
 */
void
cmsg_service_listener_remove_server (cmsg_server *server)
{
    cmsg_client *client = NULL;
    cmsg_service_info *send_msg = NULL;

    send_msg = cmsg_server_service_info_create (server);
    if (send_msg)
    {
        client = cmsg_create_client_unix_oneway (CMSG_DESCRIPTOR_NOPACKAGE (configuration));
        configuration_api_remove_server (client, send_msg);
        cmsg_destroy_client_and_transport (client);
        cmsg_server_service_info_free (send_msg);
    }
}
