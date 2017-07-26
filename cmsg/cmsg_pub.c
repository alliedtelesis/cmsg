/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#include "cmsg_private.h"
#include "cmsg_pub.h"
#include "cmsg_error.h"

#ifdef HAVE_COUNTERD
#include "cntrd_app_defines.h"
#endif

//macro for register handler implentation
cmsg_sub_service_Service cmsg_pub_subscriber_service = CMSG_SUB_SERVICE_INIT (cmsg_pub_);

static void _cmsg_pub_subscriber_delete (cmsg_pub *publisher, cmsg_sub_entry *entry);
static void _cmsg_pub_subscriber_delete_link (cmsg_pub *publisher, GList *link);

static int32_t _cmsg_pub_queue_process_all_direct (cmsg_pub *publisher);

static void _cmsg_pub_print_subscriber_list (cmsg_pub *publisher);

static cmsg_pub *_cmsg_create_publisher_tipc (const char *server_name, int member_id,
                                              int scope,
                                              ProtobufCServiceDescriptor *descriptor,
                                              cmsg_transport_type transport_type);

static int32_t cmsg_pub_message_processor (cmsg_server *server, uint8_t *buffer_data);

extern cmsg_server *cmsg_server_create (cmsg_transport *transport,
                                        ProtobufCService *service);
extern int32_t cmsg_server_counter_create (cmsg_server *server, char *app_name);


/*
 * Return 0 if two entries are same and neither are marked for deletion.
 * Otherwise return -1.
 */
gint
cmsg_sub_entry_compare (gconstpointer a, gconstpointer b)
{
    const cmsg_sub_entry *one = (const cmsg_sub_entry *) a;
    const cmsg_sub_entry *two = (const cmsg_sub_entry *) b;

    if ((one->transport->config.socket.family == two->transport->config.socket.family) &&
        (one->transport->type == two->transport->type) &&
        (one->transport->config.socket.sockaddr.in.sin_addr.s_addr ==
         two->transport->config.socket.sockaddr.in.sin_addr.s_addr) &&
        (one->transport->config.socket.sockaddr.in.sin_port ==
         two->transport->config.socket.sockaddr.in.sin_port) &&
        (one->transport->config.socket.sockaddr.tipc.family ==
         two->transport->config.socket.sockaddr.tipc.family) &&
        (one->transport->config.socket.sockaddr.tipc.addrtype ==
         two->transport->config.socket.sockaddr.tipc.addrtype) &&
        (one->transport->config.socket.sockaddr.tipc.addr.name.domain ==
         two->transport->config.socket.sockaddr.tipc.addr.name.domain) &&
        (one->transport->config.socket.sockaddr.tipc.addr.name.name.instance ==
         two->transport->config.socket.sockaddr.tipc.addr.name.name.instance) &&
        (one->transport->config.socket.sockaddr.tipc.addr.name.name.type ==
         two->transport->config.socket.sockaddr.tipc.addr.name.name.type) &&
        (one->transport->config.socket.sockaddr.tipc.scope ==
         two->transport->config.socket.sockaddr.tipc.scope) &&
        (strcmp (one->method_name, two->method_name) == 0) &&
        // If either entry has been marked for deletion, don't match it
        (!one->to_be_removed && !two->to_be_removed))
    {
        return 0;
    }

    return -1;
}

int32_t
cmsg_sub_entry_compare_transport (cmsg_sub_entry *one, cmsg_transport *transport)
{
    if ((one->transport->config.socket.family == transport->config.socket.family) &&
        (one->transport->type == transport->type) &&
        (one->transport->config.socket.sockaddr.in.sin_addr.s_addr ==
         transport->config.socket.sockaddr.in.sin_addr.s_addr) &&
        (one->transport->config.socket.sockaddr.in.sin_port ==
         transport->config.socket.sockaddr.in.sin_port) &&
        (one->transport->config.socket.sockaddr.tipc.family ==
         transport->config.socket.sockaddr.tipc.family) &&
        (one->transport->config.socket.sockaddr.tipc.addrtype ==
         transport->config.socket.sockaddr.tipc.addrtype) &&
        (one->transport->config.socket.sockaddr.tipc.addr.name.domain ==
         transport->config.socket.sockaddr.tipc.addr.name.domain) &&
        (one->transport->config.socket.sockaddr.tipc.addr.name.name.instance ==
         transport->config.socket.sockaddr.tipc.addr.name.name.instance) &&
        (one->transport->config.socket.sockaddr.tipc.addr.name.name.type ==
         transport->config.socket.sockaddr.tipc.addr.name.name.type) &&
        (one->transport->config.socket.sockaddr.tipc.scope ==
         transport->config.socket.sockaddr.tipc.scope))
    {
        return TRUE;
    }

    return FALSE;
}

int32_t
cmsg_transport_compare (cmsg_transport *one, cmsg_transport *two)
{
    if ((one->config.socket.family == two->config.socket.family) &&
        (one->type == two->type) &&
        (one->config.socket.sockaddr.in.sin_addr.s_addr ==
         two->config.socket.sockaddr.in.sin_addr.s_addr) &&
        (one->config.socket.sockaddr.in.sin_port ==
         two->config.socket.sockaddr.in.sin_port) &&
        (one->config.socket.sockaddr.tipc.family ==
         two->config.socket.sockaddr.tipc.family) &&
        (one->config.socket.sockaddr.tipc.addrtype ==
         two->config.socket.sockaddr.tipc.addrtype) &&
        (one->config.socket.sockaddr.tipc.addr.name.domain ==
         two->config.socket.sockaddr.tipc.addr.name.domain) &&
        (one->config.socket.sockaddr.tipc.addr.name.name.instance ==
         two->config.socket.sockaddr.tipc.addr.name.name.instance) &&
        (one->config.socket.sockaddr.tipc.addr.name.name.type ==
         two->config.socket.sockaddr.tipc.addr.name.name.type) &&
        (one->config.socket.sockaddr.tipc.scope == two->config.socket.sockaddr.tipc.scope))
    {
        return 1;
    }

    return 0;
}

cmsg_pub *
cmsg_pub_new (cmsg_transport *sub_server_transport,
              const ProtobufCServiceDescriptor *pub_service)
{
    CMSG_ASSERT_RETURN_VAL (sub_server_transport != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (pub_service != NULL, NULL);

    cmsg_pub *publisher = (cmsg_pub *) CMSG_CALLOC (1, sizeof (cmsg_pub));

    if (!publisher)
    {
        CMSG_LOG_GEN_ERROR ("[%s%s] Unable to create publisher.", pub_service->name,
                            sub_server_transport->tport_id);
        return NULL;
    }

    publisher->sub_server = cmsg_server_create (sub_server_transport,
                                                CMSG_SERVICE (cmsg_pub, subscriber));
    if (!publisher->sub_server)
    {
        CMSG_LOG_GEN_ERROR ("[%s%s] Unable to create publisher sub_server.",
                            pub_service->name, sub_server_transport->tport_id);
        CMSG_FREE (publisher);
        return NULL;
    }

#ifdef HAVE_COUNTERD
    char app_name[CNTRD_MAX_APP_NAME_LENGTH];

    /* Append "_pub" suffix to the counter app_name for publisher */
    snprintf (app_name, CNTRD_MAX_APP_NAME_LENGTH, "%s%s%s_pub",
              CMSG_COUNTER_APP_NAME_PREFIX,
              pub_service->name, sub_server_transport->tport_id);

    /* Initialise counters */
    if (cmsg_server_counter_create (publisher->sub_server, app_name) != CMSG_RET_OK)
    {
        CMSG_LOG_GEN_ERROR ("[%s] Unable to create server counters.", app_name);
    }
#endif

    publisher->sub_server->message_processor = cmsg_pub_message_processor;

    publisher->self.object_type = CMSG_OBJ_TYPE_PUB;
    publisher->self.object = publisher;
    strncpy (publisher->self.obj_id, pub_service->name, CMSG_MAX_OBJ_ID_LEN);

    publisher->sub_server->parent = publisher->self;

    publisher->parent.object_type = CMSG_OBJ_TYPE_NONE;
    publisher->parent.object = NULL;

    publisher->descriptor = pub_service;
    publisher->invoke = &cmsg_pub_invoke;
    publisher->subscriber_list = NULL;
    publisher->subscriber_count = 0;
    publisher->queue_enabled = FALSE;

    if (pthread_mutex_init (&publisher->queue_mutex, NULL) != 0)
    {
        CMSG_LOG_PUBLISHER_ERROR (publisher, "Init failed for queue_mutex.");
        return NULL;
    }

    publisher->queue = g_queue_new ();
    publisher->queue_filter_hash_table = g_hash_table_new (g_str_hash, g_str_equal);

    if (pthread_cond_init (&publisher->queue_process_cond, NULL) != 0)
    {
        CMSG_LOG_PUBLISHER_ERROR (publisher, "Init failed for queue_process_cond.");
        return NULL;
    }

    if (pthread_mutex_init (&publisher->queue_process_mutex, NULL) != 0)
    {
        CMSG_LOG_PUBLISHER_ERROR (publisher, "Init failed queue_process_mutex.");
        return NULL;
    }

    if (pthread_mutex_init (&publisher->subscriber_list_mutex, NULL) != 0)
    {
        CMSG_LOG_PUBLISHER_ERROR (publisher, "Init failed for subscriber_list_mutex.");
        return NULL;
    }

    publisher->self_thread_id = pthread_self ();

    cmsg_pub_queue_filter_init (publisher);

    //init random for publishing reconnection sleep
    srand (time (NULL));

    return publisher;
}


void
cmsg_pub_destroy (cmsg_pub *publisher)
{
    CMSG_ASSERT_RETURN_VOID (publisher != NULL);

    if (publisher->sub_server)
    {
        cmsg_server_destroy (publisher->sub_server);
        publisher->sub_server = NULL;
    }

    cmsg_pub_subscriber_remove_all (publisher);

    g_list_free (publisher->subscriber_list);

    publisher->subscriber_list = NULL;

    cmsg_queue_filter_free (publisher->queue_filter_hash_table, publisher->descriptor);

    g_hash_table_destroy (publisher->queue_filter_hash_table);

    cmsg_send_queue_destroy (publisher->queue);

    pthread_mutex_destroy (&publisher->queue_mutex);

    pthread_mutex_destroy (&publisher->queue_process_mutex);

    pthread_cond_destroy (&publisher->queue_process_cond);

    pthread_mutex_destroy (&publisher->subscriber_list_mutex);

    CMSG_FREE (publisher);

    return;
}


int
cmsg_pub_get_server_socket (cmsg_pub *publisher)
{
    CMSG_ASSERT_RETURN_VAL (publisher != NULL, -1);

    return (cmsg_server_get_socket (publisher->sub_server));
}


int32_t
cmsg_pub_initiate_all_subscriber_connections (cmsg_pub *publisher)
{
    CMSG_ASSERT_RETURN_VAL (publisher != NULL, CMSG_RET_ERR);

    /*
     * walk the list and get a client connection for every subscription
     */
    pthread_mutex_lock (&publisher->subscriber_list_mutex);

    GList *subscriber_list = g_list_first (publisher->subscriber_list);
    while (subscriber_list)
    {
        cmsg_sub_entry *list_entry = (cmsg_sub_entry *) subscriber_list->data;
        if (list_entry->client == NULL)
        {
            CMSG_LOG_PUBLISHER_DEBUG (publisher,
                                      "[PUB] [LIST] Couldn't get subscriber client!\n");

            pthread_mutex_unlock (&publisher->subscriber_list_mutex);
            return CMSG_RET_ERR;
        }
        else if (list_entry->client->state != CMSG_CLIENT_STATE_CONNECTED)
        {
            if (cmsg_client_connect (list_entry->client) != CMSG_RET_OK)
            {
                CMSG_LOG_PUBLISHER_DEBUG (publisher,
                                          "[PUB] [LIST] Couldn't connect to subscriber!\n");

                pthread_mutex_unlock (&publisher->subscriber_list_mutex);
                return CMSG_RET_ERR;
            }
        }
        subscriber_list = g_list_next (subscriber_list);
    }

    pthread_mutex_unlock (&publisher->subscriber_list_mutex);

    return CMSG_RET_OK;
}

static int32_t
cmsg_pub_subscriber_add (cmsg_pub *publisher, cmsg_sub_entry *entry)
{
    GList *list = NULL;

    CMSG_ASSERT_RETURN_VAL (publisher != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (entry != NULL, CMSG_RET_ERR);

    CMSG_DEBUG (CMSG_INFO, "[PUB] [LIST] adding subscriber to list\n");
    CMSG_DEBUG (CMSG_INFO, "[PUB] [LIST] entry->method_name: %s\n", entry->method_name);

    pthread_mutex_lock (&publisher->subscriber_list_mutex);

    /* Delete an old subscriber entry if exists */
    list = g_list_find_custom (publisher->subscriber_list, entry, cmsg_sub_entry_compare);
    if (list != NULL)
    {
        _cmsg_pub_subscriber_delete_link (publisher, list);
    }

    /* Add a new subscriber entry */
    publisher->subscriber_list = g_list_append (publisher->subscriber_list, entry);
    publisher->subscriber_count++;

#ifndef DEBUG_DISABLED
    CMSG_DEBUG (CMSG_INFO, "[PUB] [LIST] listing all list entries\n");
    GList *print_subscriber_list = g_list_first (publisher->subscriber_list);
    while (print_subscriber_list != NULL)
    {
        cmsg_sub_entry *print_list_entry = (cmsg_sub_entry *) print_subscriber_list->data;
        CMSG_DEBUG (CMSG_INFO, "[PUB] [LIST] print_list_entry->method_name: %s\n",
                    print_list_entry->method_name);
        print_subscriber_list = g_list_next (print_subscriber_list);
    }
#endif

    pthread_mutex_unlock (&publisher->subscriber_list_mutex);

    return CMSG_RET_OK;
}


/**
 * Delete and free a subscriber entry (by the specified link) from the publisher.
 * If the entry is currently "in-use", the entry is marks as "to-be-removed",
 * and will be removed later from cmsg_pub_invoke() after use.
 *
 * This function should be called after acquiring the lock on subscriber_list_mutex.
 */
static void
_cmsg_pub_subscriber_delete_link (cmsg_pub *publisher, GList *link)
{
    cmsg_sub_entry *entry = NULL;

    entry = (cmsg_sub_entry *) link->data;

    if (entry->in_use)
    {
        CMSG_DEBUG (CMSG_INFO, "[PUB] [LIST] marking entry for deletion\n");
        entry->to_be_removed = TRUE;
    }
    else
    {
        CMSG_DEBUG (CMSG_INFO, "[PUB] [LIST] deleting entry\n");

        link->data = NULL;

        publisher->subscriber_list = g_list_delete_link (publisher->subscriber_list, link);
        publisher->subscriber_count--;

        cmsg_client_destroy (entry->client);
        cmsg_transport_destroy (entry->transport);
        CMSG_FREE (entry);
    }
}


/**
 * Delete and free a subscriber entry from the publisher.
 * If the entry is currently "in-use", the entry is marks as "to-be-removed",
 * and will be removed later from cmsg_pub_invoke() after use.
 *
 * This function is not thread-safe. If you want to safely remove a subscriber,
 * use cmsg_pub_subscriber_remove (). Only call this function if you already have
 * the lock on subscriber_list_mutex.
 */
static void
_cmsg_pub_subscriber_delete (cmsg_pub *publisher, cmsg_sub_entry *entry)
{
    GList *link = NULL;

    CMSG_DEBUG (CMSG_INFO, "[PUB] [LIST] Removing subscriber entry->method_name: %s\n",
                entry->method_name);

    link = g_list_find_custom (publisher->subscriber_list, entry, cmsg_sub_entry_compare);
    if (link != NULL)
    {
        _cmsg_pub_subscriber_delete_link (publisher, link);
    }

#ifndef DEBUG_DISABLED
    CMSG_DEBUG (CMSG_INFO, "[PUB] [LIST] listing all list entries\n");
    GList *print_subscriber_list = g_list_first (publisher->subscriber_list);
    while (print_subscriber_list != NULL)
    {
        cmsg_sub_entry *print_list_entry = (cmsg_sub_entry *) print_subscriber_list->data;
        CMSG_DEBUG (CMSG_INFO,
                    "[PUB] [LIST] print_list_entry->method_name: %s\n",
                    print_list_entry->method_name);

        print_subscriber_list = g_list_next (print_subscriber_list);
    }
#endif
}


int32_t
cmsg_pub_subscriber_remove (cmsg_pub *publisher, cmsg_sub_entry *entry)
{
    CMSG_ASSERT_RETURN_VAL (publisher != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (entry != NULL, CMSG_RET_ERR);

    pthread_mutex_lock (&publisher->subscriber_list_mutex);

    _cmsg_pub_subscriber_delete (publisher, entry);

    pthread_mutex_unlock (&publisher->subscriber_list_mutex);

    return CMSG_RET_OK;
}


/**
 * Delete all subscribers belong to the transport being passed in.
 */
int32_t
cmsg_pub_subscriber_remove_all_with_transport (cmsg_pub *publisher,
                                               cmsg_transport *transport)
{
    GList *list = NULL;
    GList *list_next = NULL;

    CMSG_ASSERT_RETURN_VAL (publisher != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (transport != NULL, CMSG_RET_ERR);

    CMSG_DEBUG (CMSG_INFO, "[PUB] [LIST] removing subscriber from list\n");
    CMSG_DEBUG (CMSG_INFO, "[PUB] [LIST] transport: type %d\n", transport->type);

    pthread_mutex_lock (&publisher->subscriber_list_mutex);

    for (list = g_list_first (publisher->subscriber_list); list; list = list_next)
    {
        cmsg_sub_entry *list_entry = (cmsg_sub_entry *) list->data;
        list_next = g_list_next (list);

        if (cmsg_sub_entry_compare_transport (list_entry, transport))
        {
            CMSG_DEBUG (CMSG_INFO, "[PUB] [LIST] marking entry for %s for deletion\n",
                        list_entry->method_name);

            cmsg_send_queue_free_all_by_transport (publisher->queue, transport);
            _cmsg_pub_subscriber_delete_link (publisher, list);
        }
    }

#ifndef DEBUG_DISABLED
    CMSG_DEBUG (CMSG_INFO, "[PUB] [LIST] listing all list entries\n");
    GList *print_subscriber_list = g_list_first (publisher->subscriber_list);
    while (print_subscriber_list != NULL)
    {
        cmsg_sub_entry *print_list_entry = (cmsg_sub_entry *) print_subscriber_list->data;
        CMSG_DEBUG (CMSG_INFO,
                    "[PUB] [LIST] print_list_entry->method_name: %s\n",
                    print_list_entry->method_name);

        print_subscriber_list = g_list_next (print_subscriber_list);
    }
#endif

    pthread_mutex_unlock (&publisher->subscriber_list_mutex);

    return CMSG_RET_OK;
}

/**
 * cmsg_publisher_receive_poll
 *
 * Calls the transport receive function.
 * The expectation of the transport receive function is that it will return
 * <0 on failure & 0=< on success.
 *
 * On success returns 0, failure returns -1.
 */
int32_t
cmsg_publisher_receive_poll (cmsg_pub *publisher, int32_t timeout_ms, fd_set *master_fdset,
                             int *fdmax)
{
    CMSG_ASSERT_RETURN_VAL (publisher != NULL, CMSG_RET_ERR);

    return cmsg_server_receive_poll (publisher->sub_server, timeout_ms,
                                     master_fdset, fdmax);
}


void
cmsg_pub_subscriber_remove_all (cmsg_pub *publisher)
{
    CMSG_ASSERT_RETURN_VOID (publisher != NULL);

    pthread_mutex_lock (&publisher->subscriber_list_mutex);

    GList *subscriber_list = g_list_first (publisher->subscriber_list);
    while (subscriber_list)
    {
        cmsg_sub_entry *list_entry = (cmsg_sub_entry *) subscriber_list->data;
        publisher->subscriber_list = g_list_remove (publisher->subscriber_list, list_entry);

        cmsg_client_destroy (list_entry->client);
        cmsg_transport_destroy (list_entry->transport);
        CMSG_FREE (list_entry);

        subscriber_list = g_list_first (publisher->subscriber_list);
    }

    pthread_mutex_unlock (&publisher->subscriber_list_mutex);
}


int32_t
cmsg_pub_server_receive (cmsg_pub *publisher, int32_t server_socket)
{
    CMSG_ASSERT_RETURN_VAL (publisher != NULL, CMSG_RET_ERR);

    CMSG_DEBUG (CMSG_INFO, "[PUB]\n");

    return cmsg_server_receive (publisher->sub_server, server_socket);
}

int32_t
cmsg_pub_server_accept (cmsg_pub *publisher, int32_t listen_socket)
{
    CMSG_ASSERT_RETURN_VAL (publisher != NULL, CMSG_RET_ERR);

    return cmsg_server_accept (publisher->sub_server, listen_socket);
}

void
cmsg_pub_server_accept_callback (cmsg_pub *publisher, int32_t sd)
{
    if (publisher != NULL)
    {
        cmsg_server_accept_callback (publisher->sub_server, sd);
    }
}

static int32_t
cmsg_pub_message_processor (cmsg_server *server, uint8_t *buffer_data)
{
    CMSG_ASSERT_RETURN_VAL (server != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (server->_transport != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (server->service != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (server->service->descriptor != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (server->server_request != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (buffer_data != NULL, CMSG_RET_ERR);

    cmsg_server_request *server_request = server->server_request;
    ProtobufCMessage *message = NULL;
    ProtobufCAllocator *allocator = &cmsg_memory_allocator;
    cmsg_server_closure_data closure_data;
    const ProtobufCMessageDescriptor *desc;

    // Check for a connection open mesage, discard if received as we do not
    // reply to these.
    if (server_request->msg_type == CMSG_MSG_TYPE_CONN_OPEN)
    {
        return 0;
    }

    if (server_request->method_index >= server->service->descriptor->n_methods)
    {
        CMSG_LOG_SERVER_ERROR (server,
                               "The method index read from the header seems to be to high. index(%d) n_methods(%d)",
                               server_request->method_index,
                               server->service->descriptor->n_methods);
        return 0;
    }

    if (!buffer_data)
    {
        CMSG_LOG_SERVER_ERROR (server, "Buffer is not defined.");
        return 0;
    }

    CMSG_DEBUG (CMSG_INFO, "[PUB] unpacking message\n");

    desc = server->service->descriptor->methods[server_request->method_index].input;
    message = protobuf_c_message_unpack (desc, allocator,
                                         server_request->message_length, buffer_data);

    if (message == 0)
    {
        CMSG_LOG_SERVER_ERROR (server, "Failed unpacking message. No message.");
        return 0;
    }

    closure_data.server = server;
    closure_data.method_processing_reason = CMSG_METHOD_OK_TO_INVOKE;

    //this is calling: cmsg_pub_subscribe
    server->service->invoke (server->service, server_request->method_index, message,
                             server->_transport->closure, (void *) &closure_data);

    protobuf_c_message_free_unpacked (message, allocator);

    CMSG_DEBUG (CMSG_INFO, "[PUB] end of message processor\n");
    return 0;
}


int32_t
cmsg_pub_invoke (ProtobufCService *service,
                 uint32_t method_index,
                 const ProtobufCMessage *input,
                 ProtobufCClosure closure, void *closure_data)
{
    int ret = CMSG_RET_OK;
    cmsg_pub *publisher = (cmsg_pub *) service;
    const char *method_name;
    GList *list = NULL;
    GList *list_next = NULL;

    CMSG_ASSERT_RETURN_VAL (service != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (service->descriptor != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (input != NULL, CMSG_RET_ERR);

    method_name = service->descriptor->methods[method_index].name;

    CMSG_DEBUG (CMSG_INFO, "[PUB] publisher sending notification for: %s\n", method_name);

    cmsg_queue_filter_type action = cmsg_pub_queue_filter_lookup (publisher,
                                                                  method_name);

    if (action == CMSG_QUEUE_FILTER_ERROR)
    {
        CMSG_LOG_PUBLISHER_ERROR (publisher,
                                  "queue_lookup_filter returned an error for: %s\n",
                                  method_name);
        return CMSG_RET_ERR;
    }

    if (action == CMSG_QUEUE_FILTER_DROP)
    {
        CMSG_DEBUG (CMSG_ERROR, "[PUB] dropping message: %s\n", method_name);
        return CMSG_RET_OK;
    }

    //for each entry in pub->subscriber_entry
    pthread_mutex_lock (&publisher->subscriber_list_mutex);

    for (list = g_list_first (publisher->subscriber_list); list; list = list_next)
    {
        cmsg_sub_entry *list_entry = (cmsg_sub_entry *) list->data;

        // Sanity check and skip if this entry is not for the specified notification
        if (!list_entry || !list_entry->client || !list_entry->transport ||
            strcmp (method_name, list_entry->method_name) != 0)
        {
            //skip this entry - it is not what we want
            list_next = g_list_next (list);
            continue;
        }
        else
        {
            CMSG_DEBUG (CMSG_INFO, "[PUB] subscriber has subscribed to: %s\n", method_name);
        }

        if (action == CMSG_QUEUE_FILTER_PROCESS)
        {
            //don't queue, process directly
            list_entry->client->queue_enabled_from_parent = 0;
        }
        else if (action == CMSG_QUEUE_FILTER_QUEUE)
        {
            //tell client to queue
            list_entry->client->queue_enabled_from_parent = 1;
        }
        else    //global queue settings
        {
            CMSG_LOG_PUBLISHER_ERROR (publisher, "Bad action for queue filter. Action:%d.",
                                      action);
            pthread_mutex_unlock (&publisher->subscriber_list_mutex);
            return CMSG_RET_ERR;
        }

        //pass parent to client for queueing using correct queue
        list_entry->client->parent = publisher->self;

        // Mark the entry is "in-use" so it cannot be removed while sending notification
        list_entry->in_use = TRUE;

        // Unlock list to send notification
        pthread_mutex_unlock (&publisher->subscriber_list_mutex);

        int i = 0;
        for (i = 0; i <= CMSG_TRANSPORT_CLIENT_SEND_TRIES; i++)
        {
            ret = list_entry->client->invoke ((ProtobufCService *) list_entry->client,
                                              method_index, input, NULL, NULL);
            if (ret == CMSG_RET_ERR)
            {
                //try again
                CMSG_LOG_PUBLISHER_DEBUG (publisher,
                                          "Client invoke failed (method: %s) (queue: %d).",
                                          method_name, action == CMSG_QUEUE_FILTER_QUEUE);
            }
            else
            {
                break;
            }
        }

        pthread_mutex_lock (&publisher->subscriber_list_mutex);

        // Now unset "in-use" mark after sending notification
        list_entry->in_use = FALSE;

        list_next = g_list_next (list);

        if (ret == CMSG_RET_ERR)
        {
            CMSG_LOG_PUBLISHER_ERROR (publisher,
                                      "Failed to send notification (method: %s) (queue: %d). Removing subscription",
                                      method_name, action == CMSG_QUEUE_FILTER_QUEUE);
            _cmsg_pub_subscriber_delete_link (publisher, list);
        }
        else
        {
            if (list_entry->to_be_removed)
            {
                _cmsg_pub_subscriber_delete_link (publisher, list);
            }
        }
    }

    pthread_mutex_unlock (&publisher->subscriber_list_mutex);

    return CMSG_RET_OK;
}


int32_t
cmsg_pub_subscribe (cmsg_sub_service_Service *service,
                    const cmsg_sub_entry_transport_info *input,
                    cmsg_sub_entry_response_Closure closure, void *closure_data_void)
{
    CMSG_ASSERT_RETURN_VAL (service != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (input != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (closure_data_void != NULL, CMSG_RET_ERR);

    CMSG_DEBUG (CMSG_INFO, "[PUB] cmsg_notification_subscriber_server_register_handler\n");
    cmsg_server_closure_data *closure_data = (cmsg_server_closure_data *) closure_data_void;
    cmsg_server *server = closure_data->server;
    cmsg_pub *publisher = NULL;
    struct sockaddr_in *in = NULL;
    struct sockaddr_tipc *tipc = NULL;
    struct sockaddr_un *un = NULL;

    if (server->parent.object_type == CMSG_OBJ_TYPE_PUB)
    {
        publisher = (cmsg_pub *) server->parent.object;
    }

    cmsg_sub_entry_response response = CMSG_SUB_ENTRY_RESPONSE_INIT;

    if (input->transport_type != CMSG_TRANSPORT_ONEWAY_TCP &&
        input->transport_type != CMSG_TRANSPORT_ONEWAY_TIPC &&
        input->transport_type != CMSG_TRANSPORT_ONEWAY_UNIX)
    {
        CMSG_LOG_PUBLISHER_ERROR (publisher, "Subscriber transport not supported. Type:%d",
                                  input->transport_type);
        return CMSG_RET_ERR;
    }

    //read the message
    cmsg_sub_entry *subscriber_entry;
    subscriber_entry = (cmsg_sub_entry *) CMSG_CALLOC (1, sizeof (cmsg_sub_entry));
    strncpy (subscriber_entry->method_name, input->method_name,
             sizeof (subscriber_entry->method_name) - 1);
    subscriber_entry->method_name[sizeof (subscriber_entry->method_name) - 1] = '\0';

    subscriber_entry->transport = cmsg_transport_new (input->transport_type);

    if (input->transport_type == CMSG_TRANSPORT_ONEWAY_TCP)
    {
        subscriber_entry->transport->config.socket.sockaddr.generic.sa_family = PF_INET;
        subscriber_entry->transport->config.socket.family = PF_INET;

        subscriber_entry->transport->type = (cmsg_transport_type) input->transport_type;
        in = &subscriber_entry->transport->config.socket.sockaddr.in;
        in->sin_addr.s_addr = input->in_sin_addr_s_addr;
        in->sin_port = input->in_sin_port;
    }
    else if (input->transport_type == CMSG_TRANSPORT_ONEWAY_TIPC)
    {
        subscriber_entry->transport->config.socket.sockaddr.generic.sa_family = PF_TIPC;
        subscriber_entry->transport->config.socket.family = PF_TIPC;

        subscriber_entry->transport->type = (cmsg_transport_type) input->transport_type;
        tipc = &subscriber_entry->transport->config.socket.sockaddr.tipc;
        tipc->family = input->tipc_family;
        tipc->addrtype = input->tipc_addrtype;
        tipc->addr.name.domain = input->tipc_addr_name_domain;
        tipc->addr.name.name.instance = input->tipc_addr_name_name_instance;
        tipc->addr.name.name.type = input->tipc_addr_name_name_type;
        tipc->scope = input->tipc_scope;
    }
    else if (input->transport_type == CMSG_TRANSPORT_ONEWAY_UNIX)
    {
        subscriber_entry->transport->config.socket.sockaddr.generic.sa_family = PF_UNIX;
        subscriber_entry->transport->config.socket.family = PF_UNIX;

        subscriber_entry->transport->type = (cmsg_transport_type) input->transport_type;
        un = &subscriber_entry->transport->config.socket.sockaddr.un;
        strncpy (un->sun_path, input->un_sun_path, sizeof (un->sun_path) - 1);
    }

    //we can just create the client here
    //connecting here will cause deadlocks if the subscriber is single threaded
    //like for example hsl <> exfx
    subscriber_entry->client =
        cmsg_client_new (subscriber_entry->transport, publisher->descriptor);

    if (input->add)
    {
        response.return_value = cmsg_pub_subscriber_add (publisher, subscriber_entry);
    }
    else
    {
        //delete queued entries for the method being un-subscribed
        if (publisher->queue_enabled)
        {
            pthread_mutex_lock (&publisher->queue_mutex);
            cmsg_send_queue_free_by_transport_method (publisher->queue,
                                                      subscriber_entry->transport,
                                                      subscriber_entry->method_name);
            pthread_mutex_unlock (&publisher->queue_mutex);
        }
        response.return_value = cmsg_pub_subscriber_remove (publisher, subscriber_entry);

        cmsg_client_destroy (subscriber_entry->client);
        cmsg_transport_destroy (subscriber_entry->transport);
        CMSG_FREE (subscriber_entry);
    }

    closure (&response, closure_data);
    return CMSG_RET_OK;
}

void
cmsg_pub_queue_enable (cmsg_pub *publisher)
{
    publisher->queue_enabled = TRUE;
    cmsg_pub_queue_filter_set_all (publisher, CMSG_QUEUE_FILTER_QUEUE);
}

void
cmsg_pub_queue_free_all (cmsg_pub *publisher)
{
    pthread_mutex_lock (&publisher->queue_mutex);
    cmsg_send_queue_free_all (publisher->queue);
    pthread_mutex_unlock (&publisher->queue_mutex);

    //send signal to  cmsg_pub_queue_process_all
    pthread_mutex_lock (&publisher->queue_process_mutex);
    publisher->queue_process_count = -1;
    pthread_cond_signal (&publisher->queue_process_cond);
    pthread_mutex_unlock (&publisher->queue_process_mutex);
}

int32_t
cmsg_pub_queue_disable (cmsg_pub *publisher)
{
    publisher->queue_enabled = FALSE;
    cmsg_pub_queue_filter_set_all (publisher, CMSG_QUEUE_FILTER_PROCESS);

    return cmsg_pub_queue_process_all (publisher);
}

uint32_t
cmsg_pub_queue_get_length (cmsg_pub *publisher)
{
    pthread_mutex_lock (&publisher->queue_mutex);
    uint32_t queue_length = g_queue_get_length (publisher->queue);
    pthread_mutex_unlock (&publisher->queue_mutex);

    return queue_length;
}

int32_t
cmsg_pub_queue_process_all (cmsg_pub *publisher)
{
    struct timespec time_to_wait;
    uint32_t processed = 0;

    clock_gettime (CLOCK_REALTIME, &time_to_wait);

    //if the we run do api calls and processing in different threads wait
    //for a signal from the api thread to start processing
    if (publisher->self_thread_id != pthread_self ())
    {
        pthread_mutex_lock (&publisher->queue_process_mutex);
        while (publisher->queue_process_count == 0)
        {
            time_to_wait.tv_sec++;
            pthread_cond_timedwait (&publisher->queue_process_cond,
                                    &publisher->queue_process_mutex, &time_to_wait);
        }

        publisher->queue_process_count = publisher->queue_process_count - 1;
        pthread_mutex_unlock (&publisher->queue_process_mutex);

        processed = _cmsg_pub_queue_process_all_direct (publisher);
    }
    else
    {
        processed = _cmsg_pub_queue_process_all_direct (publisher);
    }

    return processed;
}

static int32_t
_cmsg_pub_queue_process_all_direct (cmsg_pub *publisher)
{
    uint32_t processed = 0;
    cmsg_send_queue_entry *queue_entry = NULL;
    GQueue *queue = publisher->queue;
    pthread_mutex_t *queue_mutex = &publisher->queue_mutex;
    const ProtobufCServiceDescriptor *descriptor = publisher->descriptor;
    cmsg_client *send_client = 0;

    if (!queue || !descriptor)
    {
        return 0;
    }

    pthread_mutex_lock (queue_mutex);
    if (g_queue_get_length (queue))
    {
        queue_entry = (cmsg_send_queue_entry *) g_queue_pop_tail (queue);
    }
    pthread_mutex_unlock (queue_mutex);


    while (queue_entry)
    {
        send_client = queue_entry->client;

        int ret = cmsg_client_buffer_send_retry (send_client,
                                                 queue_entry->queue_buffer,
                                                 queue_entry->queue_buffer_size,
                                                 CMSG_TRANSPORT_CLIENT_SEND_TRIES);

        if (ret == CMSG_RET_OK)
        {
            processed++;
        }
        else
        {
            /* if all subscribers already un-subscribed during the retry period,
             * clear the queue */
            if (publisher->subscriber_count == 0)
            {
                pthread_mutex_lock (queue_mutex);
                cmsg_send_queue_free_all (queue);
                pthread_mutex_unlock (queue_mutex);
                return processed;
            }
            //remove subscriber from subscribtion list
            cmsg_pub_subscriber_remove_all_with_transport (publisher,
                                                           queue_entry->transport);

            //delete all messages for this subscriber from queue
            pthread_mutex_lock (queue_mutex);
            cmsg_send_queue_free_all_by_transport (queue, queue_entry->transport);
            pthread_mutex_unlock (queue_mutex);

            CMSG_LOG_PUBLISHER_ERROR (publisher,
                                      "Subscriber is not reachable after %d tries and will be removed. method:(%s).",
                                      CMSG_TRANSPORT_CLIENT_SEND_TRIES,
                                      queue_entry->method_name);

        }
        CMSG_FREE (queue_entry->queue_buffer);
        CMSG_FREE (queue_entry);
        queue_entry = NULL;

        //get the next entry
        pthread_mutex_lock (queue_mutex);
        queue_entry = (cmsg_send_queue_entry *) g_queue_pop_tail (queue);
        pthread_mutex_unlock (queue_mutex);
    }

    return processed;
}


void
cmsg_pub_queue_filter_set_all (cmsg_pub *publisher, cmsg_queue_filter_type filter_type)
{
    cmsg_queue_filter_set_all (publisher->queue_filter_hash_table,
                               publisher->descriptor, filter_type);
}

void
cmsg_pub_queue_filter_clear_all (cmsg_pub *publisher)
{
    cmsg_queue_filter_clear_all (publisher->queue_filter_hash_table, publisher->descriptor);
}

int32_t
cmsg_pub_queue_filter_set (cmsg_pub *publisher, const char *method,
                           cmsg_queue_filter_type filter_type)
{
    return cmsg_queue_filter_set (publisher->queue_filter_hash_table, method, filter_type);
}

int32_t
cmsg_pub_queue_filter_clear (cmsg_pub *publisher, const char *method)
{
    return cmsg_queue_filter_clear (publisher->queue_filter_hash_table, method);
}

void
cmsg_pub_queue_filter_init (cmsg_pub *publisher)
{
    cmsg_queue_filter_init (publisher->queue_filter_hash_table, publisher->descriptor);
}

cmsg_queue_filter_type
cmsg_pub_queue_filter_lookup (cmsg_pub *publisher, const char *method)
{
    return cmsg_queue_filter_lookup (publisher->queue_filter_hash_table, method);
}

void
cmsg_pub_queue_filter_show (cmsg_pub *publisher)
{
    cmsg_queue_filter_show (publisher->queue_filter_hash_table, publisher->descriptor);
}

/**
 * Print the subscriber list of the publisher passed in.
 * This function is NOT thread-safe!!
 * If you want to print the subscriber list and you don't hold the lock on it,
 * use cmsg_pub_print_subscriber_list instead.
 */
static void
_cmsg_pub_print_subscriber_list (cmsg_pub *publisher)
{
    syslog (LOG_CRIT | LOG_LOCAL6, "[PUB] [LIST] listing all list entries\n");
    GList *print_subscriber_list = g_list_first (publisher->subscriber_list);
    while (print_subscriber_list != NULL)
    {
        cmsg_sub_entry *print_list_entry = (cmsg_sub_entry *) print_subscriber_list->data;
        syslog (LOG_CRIT | LOG_LOCAL6,
                "[PUB] [LIST] print_list_entry->method_name: %s, marked for deletion: %s\n",
                print_list_entry->method_name,
                print_list_entry->to_be_removed ? "TRUE" : "FALSE");

        print_subscriber_list = g_list_next (print_subscriber_list);
    }
}

/**
 * Print the subscriber list of the publisher passed in.
 * This function is thread-safe.
 * If you want to print the subscriber list and you hold the lock on it,
 * use _cmsg_pub_print_subscriber_list instead.
 */
void
cmsg_pub_print_subscriber_list (cmsg_pub *publisher)
{
    pthread_mutex_lock (&publisher->subscriber_list_mutex);

    _cmsg_pub_print_subscriber_list (publisher);

    pthread_mutex_unlock (&publisher->subscriber_list_mutex);
}


static cmsg_pub *
_cmsg_create_publisher_tipc (const char *server_name, int member_id, int scope,
                             ProtobufCServiceDescriptor *descriptor,
                             cmsg_transport_type transport_type)
{
    cmsg_transport *transport = NULL;
    cmsg_pub *publisher = NULL;

    transport = cmsg_create_transport_tipc (server_name, member_id, scope, transport_type);
    if (transport == NULL)
    {
        return NULL;
    }

    publisher = cmsg_pub_new (transport, descriptor);
    if (publisher == NULL)
    {
        cmsg_transport_destroy (transport);
        CMSG_LOG_GEN_ERROR ("[%s%s] No TIPC publisher to member %d", descriptor->name,
                            transport->tport_id, member_id);
        return NULL;
    }

    return publisher;
}

cmsg_pub *
cmsg_create_publisher_tipc_rpc (const char *server_name, int member_id,
                                int scope, ProtobufCServiceDescriptor *descriptor)
{
    CMSG_ASSERT_RETURN_VAL (server_name != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (descriptor != NULL, NULL);

    return _cmsg_create_publisher_tipc (server_name, member_id, scope, descriptor,
                                        CMSG_TRANSPORT_RPC_TIPC);
}

cmsg_pub *
cmsg_create_publisher_tipc_oneway (const char *server_name, int member_id,
                                   int scope, ProtobufCServiceDescriptor *descriptor)
{
    CMSG_ASSERT_RETURN_VAL (server_name != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (descriptor != NULL, NULL);

    return _cmsg_create_publisher_tipc (server_name, member_id, scope, descriptor,
                                        CMSG_TRANSPORT_ONEWAY_TIPC);
}

void
cmsg_destroy_publisher_and_transport (cmsg_pub *publisher)
{
    cmsg_transport *transport;

    if (publisher != NULL)
    {
        transport = publisher->sub_server->_transport;

        cmsg_pub_destroy (publisher);
        cmsg_transport_destroy (transport);
    }
}
