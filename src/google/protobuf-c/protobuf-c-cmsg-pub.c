#include "protobuf-c-cmsg-private.h"
#include "protobuf-c-cmsg-pub.h"
#include "protobuf-c-cmsg-error.h"

//macro for register handler implentation
cmsg_sub_service_Service cmsg_pub_subscriber_service = CMSG_SUB_SERVICE_INIT (cmsg_pub_);

static void _cmsg_pub_subscriber_mark_for_removal (cmsg_pub *publisher, cmsg_sub_entry *entry);

static int32_t _cmsg_pub_queue_process_all_direct (cmsg_pub *publisher);

static void _cmsg_pub_print_subscriber_list (cmsg_pub *publisher);

static cmsg_pub * _cmsg_create_publisher_tipc (const char *server_name, int member_id, int scope,
                                               ProtobufCServiceDescriptor *descriptor,
                                               cmsg_transport_type transport_type);


int32_t
cmsg_sub_entry_compare (cmsg_sub_entry *one, cmsg_sub_entry *two)
{
    if ((one->transport->config.socket.family == two->transport->config.socket.family) &&
        (one->transport->type == two->transport->type) &&
        (one->transport->config.socket.sockaddr.in.sin_addr.s_addr ==
         two->transport->config.socket.sockaddr.in.sin_addr.s_addr) &&
        (one->transport->config.socket.sockaddr.in.sin_port ==
         two->transport->config.socket.sockaddr.in.sin_port) &&
        (one->transport->config.socket.family == two->transport->config.socket.family) &&
        (one->transport->type == two->transport->type) &&
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
        (one->attempted_remove_time == 0) &&  // If either entry has been marked
        (one->attempted_remove_time == 0) &&  // for deletion, don't match it
        (two->attempted_remove_time == 0) &&
        (two->attempted_remove_time == 0))
    {
        return 1;
    }

    return 0;
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
        (one->transport->config.socket.family == transport->config.socket.family) &&
        (one->transport->type == transport->type) &&
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
        return 1;
    }

    return 0;
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
        (one->config.socket.family == two->config.socket.family) &&
        (one->type == two->type) &&
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

    publisher->sub_server =
        cmsg_server_new (sub_server_transport,
                         (ProtobufCService *) &cmsg_pub_subscriber_service);
    if (!publisher->sub_server)
    {
        CMSG_LOG_GEN_ERROR ("[%s%s] Unable to create publisher sub_server.",
                            pub_service->name, sub_server_transport->tport_id);
        CMSG_FREE (publisher);
        return NULL;
    }

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
        return 0;
    }

    publisher->queue = g_queue_new ();
    publisher->queue_filter_hash_table = g_hash_table_new (cmsg_queue_filter_hash_function,
                                                           cmsg_queue_filter_hash_equal_function);

    if (pthread_cond_init (&publisher->queue_process_cond, NULL) != 0)
    {
        CMSG_LOG_PUBLISHER_ERROR (publisher, "Init failed for queue_process_cond.");
        return 0;
    }

    if (pthread_mutex_init (&publisher->queue_process_mutex, NULL) != 0)
    {
        CMSG_LOG_PUBLISHER_ERROR (publisher, "Init failed queue_process_mutex.");
        return 0;
    }

    if (pthread_mutex_init (&publisher->subscriber_list_mutex, NULL) != 0)
    {
        CMSG_LOG_PUBLISHER_ERROR (publisher, "Init failed for subscriber_list_mutex.");
        return 0;
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

    cmsg_send_queue_free_all (publisher->queue);

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
            CMSG_LOG_PUBLISHER_ERROR (publisher,
                                      "[PUB] [LIST] Couldn't get subscriber client!\n");

            pthread_mutex_unlock (&publisher->subscriber_list_mutex);
            return CMSG_RET_ERR;
        }
        else if (list_entry->client->state != CMSG_CLIENT_STATE_CONNECTED)
        {
            if (cmsg_client_connect (list_entry->client) != CMSG_RET_OK)
            {
                CMSG_LOG_PUBLISHER_ERROR (publisher,
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

void
cmsg_pub_initiate_subscriber_connections (cmsg_pub *publisher, cmsg_transport *transport)
{
    CMSG_ASSERT_RETURN_VOID (publisher != NULL);
    CMSG_ASSERT_RETURN_VOID (transport != NULL);

    /*
     * walk the list and get a client connection for every subscription that
     * matches the transport
     */
    pthread_mutex_lock (&publisher->subscriber_list_mutex);

    GList *subscriber_list = g_list_first (publisher->subscriber_list);
    while (subscriber_list)
    {
        cmsg_sub_entry *list_entry = (cmsg_sub_entry *) subscriber_list->data;
        if (cmsg_sub_entry_compare_transport (list_entry, transport))
        {
            if (list_entry->client)
            {
                if (cmsg_client_connect (list_entry->client) != 0)
                {
                    CMSG_DEBUG (CMSG_INFO,
                                "[PUB] [LIST] Couldn't connect to subscriber!\n");
                }
            }
        }
        subscriber_list = g_list_next (subscriber_list);
    }

    pthread_mutex_unlock (&publisher->subscriber_list_mutex);
}

int32_t
cmsg_pub_subscriber_add (cmsg_pub *publisher, cmsg_sub_entry *entry)
{
    CMSG_ASSERT_RETURN_VAL (publisher != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (entry != NULL, CMSG_RET_ERR);

    CMSG_DEBUG (CMSG_INFO, "[PUB] [LIST] adding subscriber to list\n");
    CMSG_DEBUG (CMSG_INFO, "[PUB] [LIST] entry->method_name: %s\n", entry->method_name);

    int add = 1;

    pthread_mutex_lock (&publisher->subscriber_list_mutex);

    // check if the entry already exists first
    GList *subscriber_list = g_list_first (publisher->subscriber_list);
    while (subscriber_list)
    {
        cmsg_sub_entry *list_entry = (cmsg_sub_entry *) subscriber_list->data;

        if (cmsg_sub_entry_compare (entry, list_entry))
        {
            add = 0;
        }

        subscriber_list = g_list_next (subscriber_list);
    }

    // if the entry isn't already in the list
    if (add)
    {
        publisher->subscriber_list = g_list_append (publisher->subscriber_list, entry);
        publisher->subscriber_count++;
    }
    else
    {
        CMSG_DEBUG (CMSG_INFO, "[PUB] [LIST] not a new entry doing nothing\n");
    }

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
 * This function is not thread-safe. If you want to safely remove expired entries from the
 * subscriber list, use cmsg_pub_subscriber_remove_expired_entries ().
 * Only call this function if you already have the lock on subscriber_list_mutex.

 * This function will remove any eligible entry that has been marked for deletion by
 * having its attempted_remove_time set to a value. An entry is eligible if enough
 * time has passed since it was marked.
 */
static void
_cmsg_pub_subscriber_remove_expired_entries (cmsg_pub *publisher)
{
    CMSG_DEBUG (CMSG_INFO, "[PUB] [LIST] removing expired subscribers from list\n");

    GList *list = NULL;
    GList *list_next = NULL;
    time_t current_time = time (NULL);

    for (list = g_list_first (publisher->subscriber_list); list; list = list_next)
    {
        cmsg_sub_entry *list_entry = (cmsg_sub_entry *) list->data;
        list_next = g_list_next (list);

        if (list_entry->attempted_remove_time > 0 &&
            ((current_time - list_entry->attempted_remove_time) >
             CMSG_PUB_SUBSCRIBER_TIMEOUT))
        {
            CMSG_DEBUG (CMSG_INFO, "[PUB] [LIST] deleting entry\n");
            publisher->subscriber_list = g_list_remove (publisher->subscriber_list,
                                                        list_entry);

            cmsg_client_destroy (list_entry->client);
            cmsg_transport_destroy (list_entry->transport);
            CMSG_FREE (list_entry);
            publisher->subscriber_count--;
        }
    }
}


int32_t
cmsg_pub_subscriber_remove_expired_entries (cmsg_pub *publisher)
{
    CMSG_ASSERT_RETURN_VAL (publisher != NULL, CMSG_RET_ERR);

    pthread_mutex_lock (&publisher->subscriber_list_mutex);

    _cmsg_pub_subscriber_remove_expired_entries (publisher);

    pthread_mutex_unlock (&publisher->subscriber_list_mutex);

    return CMSG_RET_OK;
}


/**
 * This function is not thread-safe. If you want to safely remove a subscriber,
 * use cmsg_pub_subscriber_mark_for_removal ().
 * Only call this function if you already have the lock on subscriber_list_mutex.
 */
static void
_cmsg_pub_subscriber_mark_for_removal (cmsg_pub *publisher, cmsg_sub_entry *entry)
{
    CMSG_DEBUG (CMSG_INFO, "[PUB] [LIST] marking subscriber for removal from list\n");
    CMSG_DEBUG (CMSG_INFO, "[PUB] [LIST] entry->method_name: %s\n", entry->method_name);

    GList *list = NULL;
    GList *list_next = NULL;
    time_t current_time = time (NULL);

    for (list = g_list_first (publisher->subscriber_list); list; list = list_next)
    {
        cmsg_sub_entry *list_entry = (cmsg_sub_entry *) list->data;
        list_next = g_list_next (list);

        if (cmsg_sub_entry_compare (list_entry, entry) &&
            list_entry->attempted_remove_time == 0)
        {
            CMSG_DEBUG (CMSG_INFO, "[PUB] [LIST] marking entry for deletion\n");
            list_entry->attempted_remove_time = current_time;
            break;
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
}


int32_t
cmsg_pub_subscriber_mark_for_removal (cmsg_pub *publisher, cmsg_sub_entry *entry)
{
    CMSG_ASSERT_RETURN_VAL (publisher != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (entry != NULL, CMSG_RET_ERR);

    pthread_mutex_lock (&publisher->subscriber_list_mutex);

    _cmsg_pub_subscriber_mark_for_removal (publisher, entry);

    pthread_mutex_unlock (&publisher->subscriber_list_mutex);

    return CMSG_RET_OK;
}


/*
 * Marks subscriber list entries for deletion if they match the transport being passed in.
 * This function will never directly remove subscribers from the list, only mark them to
 * be removed at a later date. They will not be used once they have been marked.
 */
int32_t
cmsg_pub_subscriber_remove_all_with_transport (cmsg_pub *publisher,
                                               cmsg_transport *transport)
{
    CMSG_ASSERT_RETURN_VAL (publisher != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (transport != NULL, CMSG_RET_ERR);

    CMSG_DEBUG (CMSG_INFO, "[PUB] [LIST] removing subscriber from list\n");
    CMSG_DEBUG (CMSG_INFO, "[PUB] [LIST] transport: type %d\n", transport->type);

    GList *list;
    GList *list_next;
    time_t current_time = 0;

    pthread_mutex_lock (&publisher->subscriber_list_mutex);

    current_time = time (NULL);

    for (list = g_list_first (publisher->subscriber_list); list; list = list_next)
    {
        cmsg_sub_entry *list_entry = (cmsg_sub_entry *) list->data;
        list_next = g_list_next (list);

        if (cmsg_sub_entry_compare_transport (list_entry, transport))
        {
            CMSG_DEBUG (CMSG_INFO, "[PUB] [LIST] marking entry for %s for deletion\n",
                        list_entry->method_name);

            cmsg_send_queue_free_all_by_transport (publisher->queue, transport);
            if (list_entry->attempted_remove_time == 0)
            {
                list_entry->attempted_remove_time = current_time;
            }
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
    int32_t ret = 0;

    CMSG_ASSERT_RETURN_VAL (publisher != NULL, CMSG_RET_ERR);

    CMSG_DEBUG (CMSG_INFO, "[PUB]\n");

    ret = publisher->sub_server->_transport->server_recv (server_socket,
                                                          publisher->sub_server);

    if (ret < 0)
    {
        CMSG_DEBUG (CMSG_ERROR, "[SERVER] server receive failed\n");
        return -1;
    }

    return ret;
}

int32_t
cmsg_pub_server_accept (cmsg_pub *publisher, int32_t listen_socket)
{
    CMSG_ASSERT_RETURN_VAL (publisher != NULL, CMSG_RET_ERR);

    return cmsg_server_accept (publisher->sub_server, listen_socket);
}

int32_t
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
    ProtobufCAllocator *allocator = (ProtobufCAllocator *) server->allocator;
    cmsg_server_closure_data closure_data;
    const ProtobufCMessageDescriptor *desc;

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
                 unsigned method_index,
                 const ProtobufCMessage *input,
                 ProtobufCClosure closure, void *closure_data)
{
    int ret = CMSG_RET_OK;
    cmsg_pub *publisher = (cmsg_pub *) service;
    const char *method_name;
    cmsg_bool_t expired_list_entry = FALSE;

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
    GList *subscriber_list = g_list_first (publisher->subscriber_list);
    while (subscriber_list)
    {
        cmsg_sub_entry *list_entry = (cmsg_sub_entry *) subscriber_list->data;

        if (!list_entry)
        {
            //skip this entry is not what we want
            subscriber_list = g_list_next (subscriber_list);
            continue;
        }

        if (!list_entry->client)
        {
            //skip this entry is not what we want
            subscriber_list = g_list_next (subscriber_list);
            continue;
        }

        if (!list_entry->transport)
        {
            //skip this entry is not what we want
            subscriber_list = g_list_next (subscriber_list);
            continue;
        }

        /* If we encounter an entry marked for removal in the list, we should remember
         * this. Don't try and send using an expired entry. We don't want to remove the
         * list entry now, because_cmsg_pub_subscriber_remove_expired_entries () could
         * remove the entry that we get from g_list_next. Instead, we will clean the list
         * up once we have finished sending. */
        if (list_entry->attempted_remove_time > 0)
        {
            //skip this entry is not what we want
            subscriber_list = g_list_next (subscriber_list);
            expired_list_entry = TRUE;
            continue;
        }

        //just send to this client if it has subscribed for this notification before
        if (strcmp (method_name, list_entry->method_name) != 0)
        {
            //skip this entry is not what we want
            subscriber_list = g_list_next (subscriber_list);
            continue;
        }
        else
        {
            CMSG_DEBUG (CMSG_INFO, "[PUB] subscriber has subscribed to: %s\n", method_name);
        }

        // Unlock list to send notification
        pthread_mutex_unlock (&publisher->subscriber_list_mutex);

        if (action == CMSG_QUEUE_FILTER_PROCESS)
        {
            //dont queue, process directly
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
            return CMSG_RET_ERR;
        }

        //pass parent to client for queueing using correct queue
        list_entry->client->parent = publisher->self;

        int i = 0;
        for (i = 0; i <= CMSG_TRANSPORT_CLIENT_SEND_TRIES; i++)
        {
            //create a create packet function inside cmsg_client_invoke_oneway
            //if we want to queue from the publisher just create the buffer
            //and add it directly to the publisher queue
            //this would remove the publisher dependency in the client and
            //would make the code easier to understand and cleaner
            ret = cmsg_client_invoke_oneway ((ProtobufCService *) list_entry->client,
                                             method_index, input, closure, closure_data);

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
        subscriber_list = g_list_next (subscriber_list);

        if (ret == CMSG_RET_ERR)
        {
            CMSG_LOG_PUBLISHER_ERROR (publisher,
                                      "Failed to send notification (method: %s) (queue: %d). Removing subscription",
                                      method_name, action == CMSG_QUEUE_FILTER_QUEUE);
            /* We already have the lock on subscriber_list_mutex. Therefore we should
             * use the thread unsafe subscriber remove function. */
            _cmsg_pub_subscriber_mark_for_removal (publisher, list_entry);
            expired_list_entry = TRUE;
        }
    }

    if (expired_list_entry == TRUE)
    {
        _cmsg_pub_subscriber_remove_expired_entries (publisher);
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

    if (server->parent.object_type == CMSG_OBJ_TYPE_PUB)
        publisher = (cmsg_pub *) server->parent.object;

    cmsg_sub_entry_response response = CMSG_SUB_ENTRY_RESPONSE_INIT;

    if ((input->transport_type != CMSG_TRANSPORT_ONEWAY_TCP) &&
        (input->transport_type != CMSG_TRANSPORT_ONEWAY_TIPC))
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

    //we can just create the client here
    //connecting here will cause deadlocks if the subscriber is single threaded
    //like for example hsl <> exfx
    subscriber_entry->client = cmsg_client_new (subscriber_entry->transport, publisher->descriptor);

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
        response.return_value = cmsg_pub_subscriber_mark_for_removal (publisher, subscriber_entry);

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
        return 0;

    pthread_mutex_lock (queue_mutex);
    if (g_queue_get_length (queue))
        queue_entry = (cmsg_send_queue_entry *) g_queue_pop_tail (queue);
    pthread_mutex_unlock (queue_mutex);


    while (queue_entry)
    {
        send_client = queue_entry->client;

        int ret = cmsg_client_buffer_send_retry (send_client,
                                                 queue_entry->queue_buffer,
                                                 queue_entry->queue_buffer_size,
                                                 CMSG_TRANSPORT_CLIENT_SEND_TRIES);

        if (ret == CMSG_RET_OK)
            processed++;
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
                print_list_entry->attempted_remove_time != 0 ? "TRUE" : "FALSE");

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
