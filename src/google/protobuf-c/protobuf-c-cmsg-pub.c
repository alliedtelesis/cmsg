#include "protobuf-c-cmsg-pub.h"

//macro for register handler implentation
Cmsg__SubService_Service cmsg_pub_subscriber_service = CMSG__SUB_SERVICE__INIT (cmsg_pub_);


int32_t
cmsg_sub_entry_compare (cmsg_sub_entry *one, cmsg_sub_entry *two)
{
    CMSG_ASSERT (one);
    CMSG_ASSERT (two);

    if ((one->transport.config.socket.family == two->transport.config.socket.family) &&
        (one->transport.type == two->transport.type) &&
        (one->transport.config.socket.sockaddr.in.sin_addr.s_addr ==
         two->transport.config.socket.sockaddr.in.sin_addr.s_addr) &&
        (one->transport.config.socket.sockaddr.in.sin_port ==
         two->transport.config.socket.sockaddr.in.sin_port) &&
        (one->transport.config.socket.family == two->transport.config.socket.family) &&
        (one->transport.type == two->transport.type) &&
        (one->transport.config.socket.sockaddr.tipc.family ==
         two->transport.config.socket.sockaddr.tipc.family) &&
        (one->transport.config.socket.sockaddr.tipc.addrtype ==
         two->transport.config.socket.sockaddr.tipc.addrtype) &&
        (one->transport.config.socket.sockaddr.tipc.addr.name.domain ==
         two->transport.config.socket.sockaddr.tipc.addr.name.domain) &&
        (one->transport.config.socket.sockaddr.tipc.addr.name.name.instance ==
         two->transport.config.socket.sockaddr.tipc.addr.name.name.instance) &&
        (one->transport.config.socket.sockaddr.tipc.addr.name.name.type ==
         two->transport.config.socket.sockaddr.tipc.addr.name.name.type) &&
        (one->transport.config.socket.sockaddr.tipc.scope ==
         two->transport.config.socket.sockaddr.tipc.scope) &&
        !strcmp (one->method_name, two->method_name))
    {
        return 1;
    }

    return 0;
}

int32_t
cmsg_sub_entry_compare_transport (cmsg_sub_entry *one, cmsg_transport *transport)
{
    CMSG_ASSERT (one);
    CMSG_ASSERT (transport);

    if ((one->transport.config.socket.family == transport->config.socket.family) &&
        (one->transport.type == transport->type) &&
        (one->transport.config.socket.sockaddr.in.sin_addr.s_addr ==
         transport->config.socket.sockaddr.in.sin_addr.s_addr) &&
        (one->transport.config.socket.sockaddr.in.sin_port ==
         transport->config.socket.sockaddr.in.sin_port) &&
        (one->transport.config.socket.family == transport->config.socket.family) &&
        (one->transport.type == transport->type) &&
        (one->transport.config.socket.sockaddr.tipc.family ==
         transport->config.socket.sockaddr.tipc.family) &&
        (one->transport.config.socket.sockaddr.tipc.addrtype ==
         transport->config.socket.sockaddr.tipc.addrtype) &&
        (one->transport.config.socket.sockaddr.tipc.addr.name.domain ==
         transport->config.socket.sockaddr.tipc.addr.name.domain) &&
        (one->transport.config.socket.sockaddr.tipc.addr.name.name.instance ==
         transport->config.socket.sockaddr.tipc.addr.name.name.instance) &&
        (one->transport.config.socket.sockaddr.tipc.addr.name.name.type ==
         transport->config.socket.sockaddr.tipc.addr.name.name.type) &&
        (one->transport.config.socket.sockaddr.tipc.scope ==
         transport->config.socket.sockaddr.tipc.scope))
    {
        return 1;
    }

    return 0;
}

int32_t
cmsg_transport_compare (cmsg_transport *one, cmsg_transport *two)
{
    CMSG_ASSERT (one);
    CMSG_ASSERT (two);

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
    CMSG_ASSERT (sub_server_transport);

    cmsg_pub *publisher = CMSG_CALLOC (1, sizeof (cmsg_pub));
    if (!publisher)
    {
        syslog (LOG_CRIT | LOG_LOCAL6,
                "[PUB] [LIST] error: unable to create publisher. line(%d)\n", __LINE__);
        return NULL;
    }

    publisher->sub_server = cmsg_server_new (sub_server_transport,
                                             (ProtobufCService *) &cmsg_pub_subscriber_service);
    if (!publisher->sub_server)
    {
        DEBUG (CMSG_ERROR, "[PUB] [LIST] error: unable to create publisher->sub_server\n");
        CMSG_FREE (publisher);
        return NULL;
    }

    publisher->sub_server->message_processor = cmsg_pub_message_processor;

    publisher->self.object_type = CMSG_OBJ_TYPE_PUB;
    publisher->self.object = publisher;

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
        DEBUG (CMSG_ERROR, "[PUBLISHER] error: queue mutex init failed\n");
        return 0;
    }

    publisher->queue = g_queue_new ();
    publisher->queue_filter_hash_table = g_hash_table_new (cmsg_queue_filter_hash_function,
                                                           cmsg_queue_filter_hash_equal_function);

    if (pthread_cond_init (&publisher->queue_process_cond, NULL) != 0)
    {
        DEBUG (CMSG_ERROR, "[PUBLISHER] error: queue_process_cond init failed\n");
        return 0;
    }

    if (pthread_mutex_init (&publisher->queue_process_mutex, NULL) != 0)
    {
        DEBUG (CMSG_ERROR, "[PUBLISHER] error: queue_process_mutex init failed\n");
        return 0;
    }

    if (pthread_mutex_init (&publisher->subscriber_list_mutex, NULL) != 0)
    {
        DEBUG (CMSG_ERROR, "[PUBLISHER] error: subscriber_list_mutex init failed\n");
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
    CMSG_ASSERT (publisher);

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
    CMSG_ASSERT (publisher);

    return (cmsg_server_get_socket (publisher->sub_server));
}

cmsg_client *
cmsg_pub_get_subscriber_client (cmsg_sub_entry *sub_entry, cmsg_pub *publisher)
{
    CMSG_ASSERT (sub_entry);
    CMSG_ASSERT (publisher);

    /*
     * if the client doesn't already exist, create it and
     * update the subscription entry
     */
    if (!sub_entry->client)
    {
        sub_entry->client = cmsg_client_new (&sub_entry->transport,
                                             publisher->descriptor);
    }

    // now initiate the connection
    if (!publisher->queue_enabled)
    {
        cmsg_client_connect (sub_entry->client);
    }

    return sub_entry->client;
}

void
cmsg_pub_remove_subscriber_client (cmsg_sub_entry *sub_entry)
{
    CMSG_ASSERT (sub_entry);

    if (sub_entry->client)
    {
        //destroy the client (this will shutdown the connection)
        cmsg_client_destroy (sub_entry->client);
        sub_entry->client = NULL;
    }
}

int32_t
cmsg_pub_initiate_all_subscriber_connections (cmsg_pub *publisher)
{
    CMSG_ASSERT (publisher);
    int32_t ret = CMSG_RET_OK;
    /*
     * walk the list and get a client connection for every subscription
     */
    pthread_mutex_lock (&publisher->subscriber_list_mutex);

    GList *subscriber_list = g_list_first (publisher->subscriber_list);
    while (subscriber_list)
    {
        cmsg_sub_entry *list_entry = (cmsg_sub_entry *) subscriber_list->data;
        cmsg_client *client = cmsg_pub_get_subscriber_client (list_entry, publisher);
        if (client == NULL)
        {
            ret = CMSG_RET_ERR;
            DEBUG (CMSG_INFO, "[PUB] [LIST] Couldn't get subscriber client!\n");
        }
        else if (client->state != CMSG_CLIENT_STATE_CONNECTED)
        {
            ret = CMSG_RET_ERR;
            DEBUG (CMSG_INFO, "[PUB] [LIST] Couldn't connect to subscriber!\n");
        }
        subscriber_list = g_list_next (subscriber_list);
    }

    pthread_mutex_unlock (&publisher->subscriber_list_mutex);

    return ret;
}

void
cmsg_pub_initiate_subscriber_connections (cmsg_pub *publisher, cmsg_transport *transport)
{
    CMSG_ASSERT (publisher);
    CMSG_ASSERT (transport);

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
            if (cmsg_pub_get_subscriber_client (list_entry, publisher) == NULL)
            {
                DEBUG (CMSG_INFO, "[PUB] [LIST] Couldn't connect to subscriber!\n");
            }
        }
        subscriber_list = g_list_next (subscriber_list);
    }

    pthread_mutex_unlock (&publisher->subscriber_list_mutex);
}

int32_t
cmsg_pub_subscriber_add (cmsg_pub *publisher, cmsg_sub_entry *entry)
{
    CMSG_ASSERT (publisher);
    CMSG_ASSERT (entry);

    DEBUG (CMSG_INFO, "[PUB] [LIST] adding subscriber to list\n");
    DEBUG (CMSG_INFO, "[PUB] [LIST] entry->method_name: %s\n", entry->method_name);

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
        DEBUG (CMSG_INFO, "[PUB] [LIST] adding new entry\n");

        cmsg_sub_entry *list_entry = g_malloc0 (sizeof (cmsg_sub_entry));
        if (!list_entry)
        {
            CMSG_LOG_USER_ERROR (
                    "[PUB] [LIST] error: unable to create list entry. line(%d)\n",
                    __LINE__);
            pthread_mutex_unlock (&publisher->subscriber_list_mutex);
            return CMSG_RET_ERR;
        }
        list_entry->client = NULL;
        strcpy (list_entry->method_name, entry->method_name);
        list_entry->transport = entry->transport;

        publisher->subscriber_list = g_list_append (publisher->subscriber_list, list_entry);

        publisher->subscriber_count++;
    }
    else
    {
        DEBUG (CMSG_INFO, "[PUB] [LIST] not a new entry doing nothing\n");
    }

#ifndef CMSG_DISABLED
    DEBUG (CMSG_INFO, "[PUB] [LIST] listing all list entries\n");
    GList *print_subscriber_list = g_list_first (publisher->subscriber_list);
    while (print_subscriber_list != NULL)
    {
        cmsg_sub_entry *print_list_entry = (cmsg_sub_entry *) print_subscriber_list->data;
        DEBUG (CMSG_INFO, "[PUB] [LIST] print_list_entry->method_name: %s\n",
               print_list_entry->method_name);
        print_subscriber_list = g_list_next (print_subscriber_list);
    }
#endif

    pthread_mutex_unlock (&publisher->subscriber_list_mutex);

    return CMSG_RET_OK;
}


/**
 * This function is not thread-safe. If you want to safely remove a subscriber,
 * use cmsg_pub_subscriber_remove ().
 * Only call this function if you already have the lock on subscriber_list_mutex.
 */
static void
_cmsg_pub_subscriber_remove (cmsg_pub *publisher, cmsg_sub_entry *entry)
{
    CMSG_ASSERT (publisher);
    CMSG_ASSERT (entry);

    DEBUG (CMSG_INFO, "[PUB] [LIST] removing subscriber from list\n");
    DEBUG (CMSG_INFO, "[PUB] [LIST] entry->method_name: %s\n", entry->method_name);

    GList *subscriber_list = g_list_first (publisher->subscriber_list);
    while (subscriber_list)
    {
        cmsg_sub_entry *list_entry = (cmsg_sub_entry *) subscriber_list->data;
        if (cmsg_sub_entry_compare (list_entry, entry))
        {
            DEBUG (CMSG_INFO, "[PUB] [LIST] deleting entry\n");
            publisher->subscriber_list = g_list_remove (publisher->subscriber_list,
                                                        list_entry);
            cmsg_pub_remove_subscriber_client (list_entry);
            g_free (list_entry);
            publisher->subscriber_count--;
            break;
        }
        else
        {
            DEBUG (CMSG_INFO, "[PUB] [LIST] entry not found, nothing to delete\n");
        }
        subscriber_list = g_list_next (subscriber_list);
    }

#ifndef CMSG_DISABLED
    DEBUG (CMSG_INFO, "[PUB] [LIST] listing all list entries\n");
    GList *print_subscriber_list = g_list_first (publisher->subscriber_list);
    while (print_subscriber_list != NULL)
    {
        cmsg_sub_entry *print_list_entry = (cmsg_sub_entry *) print_subscriber_list->data;
        DEBUG (CMSG_INFO,
               "[PUB] [LIST] print_list_entry->method_name: %s\n",
               print_list_entry->method_name);

        print_subscriber_list = g_list_next (print_subscriber_list);
    }
#endif
}

int32_t
cmsg_pub_subscriber_remove (cmsg_pub *publisher, cmsg_sub_entry *entry)
{
    CMSG_ASSERT (publisher);
    CMSG_ASSERT (entry);

    pthread_mutex_lock (&publisher->subscriber_list_mutex);

    _cmsg_pub_subscriber_remove (publisher, entry);

    pthread_mutex_unlock (&publisher->subscriber_list_mutex);

    return CMSG_RET_OK;
}

int32_t
cmsg_pub_subscriber_remove_all_with_transport (cmsg_pub *publisher,
                                               cmsg_transport *transport)
{
    CMSG_ASSERT (publisher);
    CMSG_ASSERT (transport);

    DEBUG (CMSG_INFO, "[PUB] [LIST] removing subscriber from list\n");
    DEBUG (CMSG_INFO, "[PUB] [LIST] transport: type %d\n", transport->type);

    pthread_mutex_lock (&publisher->subscriber_list_mutex);

    GList *subscriber_list = g_list_first (publisher->subscriber_list);
    while (subscriber_list)
    {
        cmsg_sub_entry *list_entry = (cmsg_sub_entry *) subscriber_list->data;
        if (cmsg_sub_entry_compare_transport (list_entry, transport))
        {
            DEBUG (CMSG_INFO, "[PUB] [LIST] deleting entry for %s\n",
                   list_entry->method_name);
            subscriber_list = g_list_next (subscriber_list);
            publisher->subscriber_list = g_list_remove (publisher->subscriber_list,
                                                        list_entry);
            cmsg_pub_remove_subscriber_client (list_entry);
            g_free (list_entry);
            publisher->subscriber_count--;
        }
        else
        {
            subscriber_list = g_list_next (subscriber_list);
            DEBUG (CMSG_INFO, "[PUB] [LIST] entry not found, nothing to delete\n");
        }
    }

#ifndef CMSG_DISABLED
    DEBUG (CMSG_INFO, "[PUB] [LIST] listing all list entries\n");
    GList *print_subscriber_list = g_list_first (publisher->subscriber_list);
    while (print_subscriber_list != NULL)
    {
        cmsg_sub_entry *print_list_entry = (cmsg_sub_entry *) print_subscriber_list->data;
        DEBUG (CMSG_INFO,
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
    CMSG_ASSERT (publisher);

    return cmsg_server_receive_poll (publisher->sub_server, timeout_ms,
                                     master_fdset, fdmax);
}


void
cmsg_pub_subscriber_remove_all (cmsg_pub *publisher)
{
    pthread_mutex_lock (&publisher->subscriber_list_mutex);

    GList *subscriber_list = g_list_first (publisher->subscriber_list);
    while (subscriber_list)
    {
        cmsg_sub_entry *list_entry = (cmsg_sub_entry *) subscriber_list->data;
        publisher->subscriber_list = g_list_remove (publisher->subscriber_list, list_entry);
        cmsg_pub_remove_subscriber_client (list_entry);
        g_free (list_entry);

        subscriber_list = g_list_first (publisher->subscriber_list);
    }

    pthread_mutex_unlock (&publisher->subscriber_list_mutex);
}


int32_t
cmsg_pub_server_receive (cmsg_pub *publisher, int32_t server_socket)
{
    int32_t ret = 0;

    CMSG_ASSERT (publisher);
    CMSG_ASSERT (server_socket > 0);

    DEBUG (CMSG_INFO, "[PUB]\n");

    cmsg_server *server = publisher->sub_server;

    ret = publisher->sub_server->_transport->server_recv (server_socket,
                                                          publisher->sub_server);

    if (ret < 0)
    {
        DEBUG (CMSG_ERROR, "[SERVER] server receive failed\n");
        return -1;
    }

    return ret;
}

int32_t
cmsg_pub_server_accept (cmsg_pub *publisher, int32_t listen_socket)
{
    return cmsg_server_accept (publisher->sub_server, listen_socket);
}

int32_t
cmsg_pub_message_processor (cmsg_server *server, uint8_t *buffer_data)
{
    CMSG_ASSERT (server);
    CMSG_ASSERT (server->_transport);
    CMSG_ASSERT (server->service);
    CMSG_ASSERT (server->service->descriptor);
    CMSG_ASSERT (server->server_request);
    CMSG_ASSERT (buffer_data);

    cmsg_server_request *server_request = server->server_request;
    ProtobufCMessage *message = NULL;
    ProtobufCAllocator *allocator = (ProtobufCAllocator *) server->allocator;
    cmsg_closure_data closure_data;

    if (server_request->method_index >= server->service->descriptor->n_methods)
    {
        DEBUG (CMSG_ERROR,
               "[PUB] the method index from read from the header seems to be to high\n");
        return 0;
    }

    if (!buffer_data)
    {
        CMSG_LOG_USER_ERROR ("[PUB] buffer not defined");
        return 0;
    }

    DEBUG (CMSG_INFO, "[PUB] unpacking message\n");

    message = protobuf_c_message_unpack (server->service->descriptor->methods[server_request->method_index].input,
                                         allocator,
                                         server_request->message_length,
                                         buffer_data);

    if (message == 0)
    {
        DEBUG (CMSG_ERROR, "[PUB] error unpacking message\n");
        return 0;
    }

    closure_data.server = server;
    closure_data.method_processing_reason = CMSG_METHOD_OK_TO_INVOKE;

    //this is calling: cmsg_pub_subscribe
    server->service->invoke (server->service, server_request->method_index, message,
                             server->_transport->closure, (void *) &closure_data);

    protobuf_c_message_free_unpacked (message, allocator);

    DEBUG (CMSG_INFO, "[PUB] end of message processor\n");
    return 0;
}

void
cmsg_pub_invoke (ProtobufCService *service,
                 unsigned method_index,
                 const ProtobufCMessage *input,
                 ProtobufCClosure closure, void *closure_data)
{
    int tries = CMSG_TRANSPORT_CLIENT_SEND_TRIES;
    int remove_entry = 0;
    cmsg_pub *publisher = (cmsg_pub *) service;

    CMSG_ASSERT (service);
    CMSG_ASSERT (service->descriptor);
    CMSG_ASSERT (input);

    DEBUG (CMSG_INFO,
           "[PUB] publisher sending notification for: %s\n",
           service->descriptor->methods[method_index].name);

    cmsg_queue_filter_type action = cmsg_pub_queue_filter_lookup (publisher,
                                                                  service->descriptor->methods[method_index].name);

    if (action == CMSG_QUEUE_FILTER_ERROR)
    {
        DEBUG (CMSG_ERROR,
               "[PUB] error: queue_lookup_filter returned CMSG_QUEUE_FILTER_ERROR for: %s\n",
               service->descriptor->methods[method_index].name);
        return;
    }

    if (action == CMSG_QUEUE_FILTER_DROP)
    {
        DEBUG (CMSG_ERROR,
               "[PUB] dropping message: %s\n",
               service->descriptor->methods[method_index].name);
        return;
    }

    //for each entry in pub->subscriber_entry
    pthread_mutex_lock (&publisher->subscriber_list_mutex);
    GList *subscriber_list = g_list_first (publisher->subscriber_list);
    while (subscriber_list)
    {
        cmsg_sub_entry *list_entry = (cmsg_sub_entry *) subscriber_list->data;

        //just send to this client if it has subscribed for this notification before
        if (strcmp (service->descriptor->methods[method_index].name,
                    list_entry->method_name))
        {
            //skip this entry is not what we want
            subscriber_list = g_list_next (subscriber_list);
            continue;
        }
        else
        {
            DEBUG (CMSG_INFO,
                   "[PUB] subscriber has subscribed to: %s\n",
                   service->descriptor->methods[method_index].name);
        }

        // now get the client associated with this subscription
        cmsg_client *client = cmsg_pub_get_subscriber_client (list_entry, publisher);

        if (action == CMSG_QUEUE_FILTER_PROCESS)
        {
            //dont queue, process directly
            client->queue_enabled_from_parent = 0;
        }
        else if (action == CMSG_QUEUE_FILTER_QUEUE)
        {
            //tell client to queue
            client->queue_enabled_from_parent = 1;
            tries = 1;
        }
        else    //global queue settings
        {
            DEBUG (CMSG_ERROR, "[PUB] error: queue filter action: %d wrong\n", action);
            pthread_mutex_unlock (&publisher->subscriber_list_mutex);
            return;
        }

        //pass parent to client for queueing using correct queue
        client->parent = publisher->self;

        int c = 0;
        for (c = 0; c <= tries; c++)
        {
            cmsg_client_invoke_oneway ((ProtobufCService *) client, method_index,
                                       input, closure, closure_data);

            if (client->state == CMSG_CLIENT_STATE_FAILED ||
                client->state == CMSG_CLIENT_STATE_CLOSED)
            {
                if (list_entry->transport.client_send_tries >=
                    CMSG_TRANSPORT_CLIENT_SEND_TRIES)
                {

                    // error: subscriber not reachable after 10 tries, removing subscriber from list
                    remove_entry = 1;
                }

                list_entry->transport.client_send_tries++;
                // couldn't sent retry
            }
            else if (client->state == CMSG_CLIENT_STATE_CONNECTED)
            {
                // sent successful after %d tries
                list_entry->transport.client_send_tries = 0;
                break;
            }
            else if (client->state == CMSG_CLIENT_STATE_QUEUED)
            {
                // message queued successful
                list_entry->transport.client_send_tries = 0;
                break;
            }
            else
            {
                syslog (LOG_CRIT | LOG_LOCAL6, "[PUB] error: unknown client->state=%d\n",
                        client->state);
            }
        }

        subscriber_list = g_list_next (subscriber_list);

        if (remove_entry)
        {
            /* We already have the lock on subscriber_list_mutex. Therefore we should
             * use the thread unsafe subscriber remove function.
             */
            _cmsg_pub_subscriber_remove (publisher, list_entry);

            remove_entry = 0;
        }
    }

    pthread_mutex_unlock (&publisher->subscriber_list_mutex);
    return;
}

void
cmsg_pub_subscribe (Cmsg__SubService_Service *service, const Cmsg__SubEntry *input,
                    Cmsg__SubEntryResponse_Closure closure, void *closure_data_void)
{
    CMSG_ASSERT (service);
    CMSG_ASSERT (input);
    CMSG_ASSERT (closure_data_void);

    DEBUG (CMSG_INFO, "[PUB] cmsg_notification_subscriber_server_register_handler\n");
    cmsg_closure_data *closure_data = (cmsg_closure_data *) closure_data_void;
    cmsg_server *server = closure_data->server;
    cmsg_pub *publisher = NULL;

    if (server->parent.object_type == CMSG_OBJ_TYPE_PUB)
        publisher = server->parent.object;

    Cmsg__SubEntryResponse response = CMSG__SUB_ENTRY_RESPONSE__INIT;

    //read the message
    cmsg_sub_entry subscriber_entry;
    memset (&subscriber_entry, 0, sizeof (cmsg_sub_entry));
    sprintf (subscriber_entry.method_name, "%s_pbc", input->method_name);

    if (input->transport_type == CMSG_TRANSPORT_ONEWAY_TCP)
    {
        subscriber_entry.transport.config.socket.sockaddr.generic.sa_family = PF_INET;
        subscriber_entry.transport.config.socket.family = PF_INET;

        subscriber_entry.transport.type = input->transport_type;
        subscriber_entry.transport.config.socket.sockaddr.in.sin_addr.s_addr =
            input->in_sin_addr_s_addr;
        subscriber_entry.transport.config.socket.sockaddr.in.sin_port = input->in_sin_port;

        DEBUG (CMSG_INFO, "[PUB] [LIST]  tcp address: %x, port: %d\n",
               ntohl (subscriber_entry.transport.config.socket.sockaddr.in.sin_addr.s_addr),
               ntohs (subscriber_entry.transport.config.socket.sockaddr.in.sin_port));

        cmsg_transport_oneway_tcp_init (&(subscriber_entry.transport));
    }
    else if (input->transport_type == CMSG_TRANSPORT_ONEWAY_TIPC)
    {
        subscriber_entry.transport.config.socket.sockaddr.generic.sa_family = PF_TIPC;
        subscriber_entry.transport.config.socket.family = PF_TIPC;

        subscriber_entry.transport.type = input->transport_type;
        subscriber_entry.transport.config.socket.sockaddr.tipc.family = input->tipc_family;
        subscriber_entry.transport.config.socket.sockaddr.tipc.addrtype =
            input->tipc_addrtype;
        subscriber_entry.transport.config.socket.sockaddr.tipc.addr.name.domain =
            input->tipc_addr_name_domain;
        subscriber_entry.transport.config.socket.sockaddr.tipc.addr.name.name.instance =
            input->tipc_addr_name_name_instance;
        subscriber_entry.transport.config.socket.sockaddr.tipc.addr.name.name.type =
            input->tipc_addr_name_name_type;
        subscriber_entry.transport.config.socket.sockaddr.tipc.scope = input->tipc_scope;

        DEBUG (CMSG_INFO, "[PUB] [LIST]  tipc type: %d, instance: %d\n",
               subscriber_entry.transport.config.socket.sockaddr.tipc.addr.name.name.type,
               subscriber_entry.transport.config.socket.sockaddr.tipc.addr.name.name.instance);

        cmsg_transport_oneway_tipc_init (&(subscriber_entry.transport));
    }
    else
    {
        CMSG_LOG_USER_ERROR ("[PUB] error: subscriber transport not supported");

        return;
    }

    if (input->add)
    {
        response.return_value = cmsg_pub_subscriber_add (publisher, &subscriber_entry);
    }
    else
    {
        response.return_value = cmsg_pub_subscriber_remove (publisher, &subscriber_entry);
    }

    closure (&response, closure_data);

    return;
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

unsigned int
cmsg_pub_queue_get_length (cmsg_pub *publisher)
{
    pthread_mutex_lock (&publisher->queue_mutex);
    unsigned int queue_length = g_queue_get_length (publisher->queue);
    pthread_mutex_unlock (&publisher->queue_mutex);

    return queue_length;
}

int32_t
cmsg_pub_queue_process_all (cmsg_pub *publisher)
{
    struct timespec time_to_wait;
    uint32_t processed = 0;
    cmsg_object obj;

    clock_gettime (CLOCK_REALTIME, &time_to_wait);

    obj.object_type = CMSG_OBJ_TYPE_PUB;
    obj.object = publisher;

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

        processed = cmsg_send_queue_process_all (obj);
    }
    else
    {
        processed = cmsg_send_queue_process_all (obj);
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
cmsg_pub_queue_filter_set (cmsg_pub *publisher,
                           const char *method, cmsg_queue_filter_type filter_type)
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
void
_cmsg_pub_print_subscriber_list (cmsg_pub *publisher)
{
    syslog (LOG_CRIT | LOG_LOCAL6, "[PUB] [LIST] listing all list entries\n");
    GList *print_subscriber_list = g_list_first (publisher->subscriber_list);
    while (print_subscriber_list != NULL)
    {
        cmsg_sub_entry *print_list_entry = (cmsg_sub_entry *) print_subscriber_list->data;
        syslog (LOG_CRIT | LOG_LOCAL6,
                "[PUB] [LIST] print_list_entry->method_name: %s\n",
                print_list_entry->method_name);

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

