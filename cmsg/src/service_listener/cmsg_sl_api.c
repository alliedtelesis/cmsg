/**
 * cmsg_sl_api.c
 *
 * Implements the functions that can be used to interact with the service
 * listener daemon.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include <sys/eventfd.h>
#include <cmsg/cmsg_pthread_helpers.h>
#include "configuration_api_auto.h"
#include "cmsg_sl_config.h"
#include "cmsg_server_private.h"
#include "cmsg_sl.h"
#include "events_impl_auto.h"
#include "transport/cmsg_transport_private.h"

struct _cmsg_sl_info_s
{
    char *service_name;
    cmsg_sl_event_handler_t handler;
    uint32_t id;
    void *user_data;
    GAsyncQueue *queue;
    int eventfd;
};

typedef struct _cmsg_sl_event
{
    bool added;
    cmsg_transport *transport;
} cmsg_sl_event;

static cmsg_server *event_server = NULL;
static pthread_t event_server_thread;
static GList *listener_list = NULL;
static pthread_mutex_t listener_list_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t add_remove_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * Get the eventfd descriptor from the 'cmsg_sl_info' structure.
 *
 * @param info - The 'cmsg_sl_info' structure to get the eventfd descriptor for.
 *
 * @returns The eventfd descriptor.
 */
int
cmsg_service_listener_get_event_fd (const cmsg_sl_info *info)
{
    return info->eventfd;
}

/**
 * Process any events on the event queue of the 'cmsg_sl_info' structure.
 *
 * @param info - The 'cmsg_sl_info' structure to process events for.
 *
 * @returns true if the service listening should keep running, false otherwise.
 */
bool
cmsg_service_listener_event_queue_process (const cmsg_sl_info *info)
{
    cmsg_sl_event *event = NULL;
    eventfd_t value;
    bool ret = true;

    /* clear notification */
    TEMP_FAILURE_RETRY (eventfd_read (info->eventfd, &value));

    while ((event = g_async_queue_try_pop (info->queue)))
    {
        ret = info->handler (event->transport, event->added, info->user_data);
        cmsg_transport_destroy (event->transport);
        CMSG_FREE (event);

        if (!ret)
        {
            break;
        }
    }

    return ret;
}

/**
 * Notify the listener of a given service of the event that has occurred.
 *
 * @param recv_msg - The received event message from the service listener daemon.
 * @param added - Whether the service has been added or removed.
 */
static void
notify_listener (const cmsg_sld_server_event *recv_msg, bool added)
{
    GList *list;
    GList *list_next;
    const cmsg_sl_info *entry;
    cmsg_sl_event *event = NULL;
    const cmsg_transport_info *transport_info = NULL;

    pthread_mutex_lock (&listener_list_mutex);

    for (list = g_list_first (listener_list); list; list = list_next)
    {
        entry = (const cmsg_sl_info *) list->data;
        list_next = g_list_next (list);

        if (entry->id == recv_msg->id)
        {
            event = CMSG_MALLOC (sizeof (cmsg_sl_event));
            if (event)
            {
                transport_info = recv_msg->service_info->server_info;

                event->added = added;
                event->transport = cmsg_transport_info_to_transport (transport_info);
                g_async_queue_push (entry->queue, event);
                TEMP_FAILURE_RETRY (eventfd_write (entry->eventfd, 1));
            }
        }
    }

    pthread_mutex_unlock (&listener_list_mutex);
}

/**
 * Notification from the CMSG service listener daemon that a server for
 * a specific service has been added.
 */
void
cmsg_sld_events_impl_server_added (const void *service,
                                   const cmsg_sld_server_event *recv_msg)
{
    notify_listener (recv_msg, true);
    cmsg_sld_events_server_server_addedSend (service);
}

/**
 * Notification from the CMSG service listener daemon that a server for
 * a specific service has been removed.
 */
void
cmsg_sld_events_impl_server_removed (const void *service,
                                     const cmsg_sld_server_event *recv_msg)
{
    notify_listener (recv_msg, false);
    cmsg_sld_events_server_server_removedSend (service);
}

/**
 * Initialise the server for receiving events from the service listener.
 */
static void
event_server_init (void)
{
    cmsg_transport *transport = NULL;

    transport = cmsg_transport_new (CMSG_TRANSPORT_ONEWAY_UNIX);
    transport->config.socket.family = AF_UNIX;
    transport->config.socket.sockaddr.un.sun_family = AF_UNIX;
    snprintf (transport->config.socket.sockaddr.un.sun_path,
              sizeof (transport->config.socket.sockaddr.un.sun_path) - 1,
              "/tmp/%s.%u", cmsg_service_name_get (CMSG_DESCRIPTOR (cmsg_sld, events)),
              getpid ());

    event_server = cmsg_server_new (transport, CMSG_SERVICE (cmsg_sld, events));
    cmsg_pthread_server_init (&event_server_thread, event_server);
}

/**
 * Destroy the server used for receiving events from the service listener.
 */
static void
event_server_deinit (void)
{
    pthread_cancel (event_server_thread);
    pthread_join (event_server_thread, NULL);
    cmsg_destroy_server_and_transport (event_server);
    event_server = NULL;
}

/**
 * Helper function for calling the API to the CMSG service listener
 * to add/remove a listener for a given service.
 *
 * Note - Assumes the 'listener_list_mutex' mutex is held.
 *
 * @param service_name - The name of the service to listen/unlisten for.
 * @param listen - true to listen, false to unlisten.
 * @param id - The numerical ID of the listener.
 */
static void
_cmsg_service_listener_listen (const char *service_name, bool listen, uint32_t id)
{
    cmsg_client *client = NULL;
    cmsg_sld_listener_info send_msg = CMSG_SLD_LISTENER_INFO_INIT;
    cmsg_transport_info *transport_info = NULL;

    transport_info = cmsg_transport_info_create (event_server->_transport);

    CMSG_SET_FIELD_PTR (&send_msg, service, (char *) service_name);
    CMSG_SET_FIELD_PTR (&send_msg, transport_info, transport_info);
    CMSG_SET_FIELD_VALUE (&send_msg, id, id);

    client = cmsg_create_client_unix_oneway (CMSG_DESCRIPTOR (cmsg_sld, configuration));

    if (listen)
    {
        cmsg_sld_configuration_api_listen (client, &send_msg);
    }
    else
    {
        cmsg_sld_configuration_api_unlisten (client, &send_msg);
    }

    cmsg_destroy_client_and_transport (client);
    cmsg_transport_info_free (transport_info);
}

/**
 * When destroying the event queue there may still be events on there.
 * Simply free them as required to avoid leaking memory.
 */
static void
_clear_event_queue (gpointer data)
{
    cmsg_sl_event *info = (cmsg_sl_event *) data;
    cmsg_transport_destroy (info->transport);
    CMSG_FREE (info);
}

/**
 * Create and initialise a 'cmsg_sl_info' structure.
 *
 * @param service_name - The service name to use.
 * @param handler - The handler to use.
 * @param user_data - The user data to pass into the handler.
 *
 * @returns A pointer to the initialised structure on success, NULL otherwise.
 */
static cmsg_sl_info *
cmsg_service_listener_info_create (const char *service_name,
                                   cmsg_sl_event_handler_t handler, void *user_data)
{
    cmsg_sl_info *info = NULL;
    static uint32_t id = 0;

    info = CMSG_CALLOC (1, sizeof (cmsg_sl_info));
    if (!info)
    {
        return NULL;
    }

    info->service_name = CMSG_STRDUP (service_name);
    if (!info->service_name)
    {
        CMSG_FREE (info);
        return NULL;
    }

    info->eventfd = eventfd (0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (info->eventfd < 0)
    {
        CMSG_FREE (info->service_name);
        CMSG_FREE (info);
        return NULL;
    }

    info->queue = g_async_queue_new_full (_clear_event_queue);
    if (!info->queue)
    {
        close (info->eventfd);
        CMSG_FREE (info->service_name);
        CMSG_FREE (info);
        return NULL;
    }

    info->handler = handler;
    info->user_data = user_data;
    info->id = id++;

    return info;
}

/**
 * Destroy a 'cmsg_sl_info' structure and all associated memory.
 *
 * @param info - The structure to destroy.
 */
static void
cmsg_service_listener_info_destroy (cmsg_sl_info *info)
{
    g_async_queue_unref (info->queue);
    close (info->eventfd);
    CMSG_FREE (info->service_name);
    CMSG_FREE (info);
}

/**
 * Listen for events for the given service name.
 *
 * @param service_name - The service to listen for.
 * @param func - The function to call when a server is added or removed
 *               for the given service.
 * @param user_data - Pointer to user supplied data that will be passed into the
 *                    supplied handler function.
 *
 * @returns A pointer to the 'cmsg_sl_info' structure for the given service.
 */
const cmsg_sl_info *
cmsg_service_listener_listen (const char *service_name, cmsg_sl_event_handler_t handler,
                              void *user_data)
{
    cmsg_sl_info *info = NULL;

    pthread_mutex_lock (&add_remove_mutex);

    info = cmsg_service_listener_info_create (service_name, handler, user_data);
    if (!info)
    {
        pthread_mutex_unlock (&add_remove_mutex);
        return NULL;
    }

    pthread_mutex_lock (&listener_list_mutex);

    listener_list = g_list_append (listener_list, info);

    /* If this is the first listener then create the server for receiving
     * notifications from the service listener daemon. */
    if (g_list_length (listener_list) == 1)
    {
        event_server_init ();
    }

    _cmsg_service_listener_listen (service_name, true, info->id);

    pthread_mutex_unlock (&listener_list_mutex);
    pthread_mutex_unlock (&add_remove_mutex);

    return info;
}

/**
 * Unlisten from events for the given service name.
 *
 * @param service_name - The service to unlisten from.
 */
void
cmsg_service_listener_unlisten (const cmsg_sl_info *info)
{
    pthread_mutex_lock (&add_remove_mutex);
    pthread_mutex_lock (&listener_list_mutex);

    listener_list = g_list_remove (listener_list, info);

    _cmsg_service_listener_listen (info->service_name, false, info->id);

    /* If this was the only listener then destroy the server for receiving
     * notifications from the service listener daemon. */
    if (g_list_length (listener_list) == 0)
    {
        /* Release the list mutex as the server thread may be blocked waiting
         * to take it. If the lock is not release then the server thread cannot
         * be cancelled and subsequently joined. */
        pthread_mutex_unlock (&listener_list_mutex);
        event_server_deinit ();
        pthread_mutex_unlock (&add_remove_mutex);
    }
    else
    {
        pthread_mutex_unlock (&listener_list_mutex);
        pthread_mutex_unlock (&add_remove_mutex);
    }


    cmsg_service_listener_info_destroy ((cmsg_sl_info *) info);
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

    client = cmsg_create_client_unix_oneway (CMSG_DESCRIPTOR (cmsg_sld, configuration));
    if (!client)
    {
        return false;
    }

    CMSG_SET_FIELD_VALUE (&send_msg, value, addr.s_addr);

    ret = cmsg_sld_configuration_api_address_set (client, &send_msg);
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

    client = cmsg_create_client_unix_oneway (CMSG_DESCRIPTOR (cmsg_sld, configuration));
    if (!client)
    {
        return false;
    }

    CMSG_SET_FIELD_VALUE (&send_msg, value, addr.s_addr);

    ret = cmsg_sld_configuration_api_add_host (client, &send_msg);
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

    client = cmsg_create_client_unix_oneway (CMSG_DESCRIPTOR (cmsg_sld, configuration));
    if (!client)
    {
        return false;
    }

    CMSG_SET_FIELD_VALUE (&send_msg, value, addr.s_addr);

    ret = cmsg_sld_configuration_api_delete_host (client, &send_msg);
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
        client = cmsg_create_client_unix_oneway (CMSG_DESCRIPTOR (cmsg_sld, configuration));
        cmsg_sld_configuration_api_add_server (client, send_msg);
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
        client = cmsg_create_client_unix_oneway (CMSG_DESCRIPTOR (cmsg_sld, configuration));
        cmsg_sld_configuration_api_remove_server (client, send_msg);
        cmsg_destroy_client_and_transport (client);
        cmsg_server_service_info_free (send_msg);
    }
}
