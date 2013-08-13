#include "protobuf-c-cmsg-pub.h"

//macro for register handler implentation
Cmsg__SubService_Service cmsg_pub_subscriber_service = CMSG__SUB_SERVICE__INIT (cmsg_pub_);


int32_t
cmsg_sub_entry_compare (cmsg_sub_entry *one,
                        cmsg_sub_entry *two)
{
    CMSG_ASSERT (one);
    CMSG_ASSERT (two);

    if ((one->transport.config.socket.family == two->transport.config.socket.family) &&
        (one->transport.type == two->transport.type) &&
        (one->transport.config.socket.sockaddr.in.sin_addr.s_addr == two->transport.config.socket.sockaddr.in.sin_addr.s_addr) &&
        (one->transport.config.socket.sockaddr.in.sin_port == two->transport.config.socket.sockaddr.in.sin_port) &&
        (one->transport.config.socket.family == two->transport.config.socket.family) &&
        (one->transport.type == two->transport.type) &&
        (one->transport.config.socket.sockaddr.tipc.family == two->transport.config.socket.sockaddr.tipc.family) &&
        (one->transport.config.socket.sockaddr.tipc.addrtype == two->transport.config.socket.sockaddr.tipc.addrtype) &&
        (one->transport.config.socket.sockaddr.tipc.addr.name.domain == two->transport.config.socket.sockaddr.tipc.addr.name.domain) &&
        (one->transport.config.socket.sockaddr.tipc.addr.name.name.instance == two->transport.config.socket.sockaddr.tipc.addr.name.name.instance) &&
        (one->transport.config.socket.sockaddr.tipc.addr.name.name.type == two->transport.config.socket.sockaddr.tipc.addr.name.name.type) &&
        (one->transport.config.socket.sockaddr.tipc.scope == two->transport.config.socket.sockaddr.tipc.scope) &&
        !strcmp (one->method_name, two->method_name))
    {
        return 1;
    }

    return 0;
}


cmsg_pub *
cmsg_pub_new (cmsg_transport                   *sub_server_transport,
              const ProtobufCServiceDescriptor *pub_service)
{
    CMSG_ASSERT (sub_server_transport);

    cmsg_pub *publisher = malloc (sizeof (cmsg_pub));
    if (!publisher)
    {
	syslog(LOG_CRIT | LOG_LOCAL6, "[PUB] [LIST] error: unable to create publisher. line(%d)\n",__LINE__);
        return NULL;
    }

    publisher->sub_server = cmsg_server_new (sub_server_transport,
                                             (ProtobufCService *)&cmsg_pub_subscriber_service);
    if (!publisher->sub_server)
    {
        DEBUG (CMSG_ERROR, "[PUB] [LIST] error: unable to create publisher->sub_server\n");
        free (publisher);
        return NULL;
    }

    publisher->sub_server->message_processor = cmsg_pub_message_processor;
    publisher->sub_server->parent = publisher;

    publisher->descriptor = pub_service;
    publisher->invoke = &cmsg_pub_invoke;
    publisher->subscriber_list = NULL;
    publisher->subscriber_count = 0;

    publisher->queue_enabled = 0;

    if (pthread_mutex_init (&publisher->queue_mutex, NULL) != 0)
    {
        DEBUG (CMSG_ERROR, "[CLIENT] queue mutex init failed\n");
        return 0;
    }

    publisher->queue_timeouts = 0;
    publisher->queue = g_queue_new ();
    g_queue_init (publisher->queue);
    publisher->queue_total_size = 0;

    srand (time (NULL));

    return publisher;
}


void
cmsg_pub_destroy (cmsg_pub **publisher)
{
    CMSG_ASSERT (publisher);

    if ((*publisher)->sub_server)
    {
        cmsg_server_destroy (&(*publisher)->sub_server);
        (*publisher)->sub_server = NULL;
    }

    g_list_free ((*publisher)->subscriber_list);
    (*publisher)->subscriber_list = NULL;

    free (*publisher);
    *publisher = NULL;

    return;
}


int
cmsg_pub_get_server_socket (cmsg_pub *publisher)
{
    CMSG_ASSERT (publisher);

    return (cmsg_server_get_socket (publisher->sub_server));
}


int32_t
cmsg_pub_subscriber_add (cmsg_pub       *publisher,
                         cmsg_sub_entry *entry)
{
    CMSG_ASSERT (publisher);
    CMSG_ASSERT (entry);

    DEBUG (CMSG_INFO, "[PUB] [LIST] adding subscriber to list\n");
    DEBUG (CMSG_INFO, "[PUB] [LIST] entry->method_name: %s\n", entry->method_name);

    int add = 1;

    GList *subscriber_list = g_list_first (publisher->subscriber_list);
    while (subscriber_list)
    {
        cmsg_sub_entry *list_entry = (cmsg_sub_entry *)subscriber_list->data;

        if (cmsg_sub_entry_compare (entry, list_entry))
        {
            add = 0;
        }

        subscriber_list = g_list_next (subscriber_list);
    }

    if (add)
    {
        DEBUG (CMSG_INFO, "[PUB] [LIST] adding new entry\n");

        cmsg_sub_entry *list_entry = g_malloc (sizeof (cmsg_sub_entry));
        if (!list_entry)
        {
	    syslog(LOG_CRIT | LOG_LOCAL6, "[PUB] [LIST] error: unable to create list entry. line(%d)\n",__LINE__);
            return CMSG_RET_ERR;
        }
        strcpy (list_entry->method_name, entry->method_name);

        list_entry->transport.config.socket.family = entry->transport.config.socket.family;
        list_entry->transport.type = entry->transport.type;
        list_entry->transport.config.socket.sockaddr.in.sin_addr.s_addr = entry->transport.config.socket.sockaddr.in.sin_addr.s_addr;
        list_entry->transport.config.socket.sockaddr.in.sin_port = entry->transport.config.socket.sockaddr.in.sin_port;

        list_entry->transport.config.socket.family = entry->transport.config.socket.family;
        list_entry->transport.type = entry->transport.type;
        list_entry->transport.config.socket.sockaddr.tipc.family = entry->transport.config.socket.sockaddr.tipc.family;
        list_entry->transport.config.socket.sockaddr.tipc.addrtype = entry->transport.config.socket.sockaddr.tipc.addrtype;
        list_entry->transport.config.socket.sockaddr.tipc.addr.name.domain = entry->transport.config.socket.sockaddr.tipc.addr.name.domain;
        list_entry->transport.config.socket.sockaddr.tipc.addr.name.name.instance = entry->transport.config.socket.sockaddr.tipc.addr.name.name.instance;
        list_entry->transport.config.socket.sockaddr.tipc.addr.name.name.type = entry->transport.config.socket.sockaddr.tipc.addr.name.name.type;
        list_entry->transport.config.socket.sockaddr.tipc.scope = entry->transport.config.socket.sockaddr.tipc.scope;

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
        cmsg_sub_entry *print_list_entry = (cmsg_sub_entry *)print_subscriber_list->data;
        DEBUG (CMSG_INFO, "[PUB] [LIST] print_list_entry->method_name: %s\n", print_list_entry->method_name);
        print_subscriber_list = g_list_next (print_subscriber_list);
    }
#endif

    return CMSG_RET_OK;
}


int32_t
cmsg_pub_subscriber_remove (cmsg_pub       *publisher,
                            cmsg_sub_entry *entry)
{
    CMSG_ASSERT (publisher);
    CMSG_ASSERT (entry);

    DEBUG (CMSG_INFO, "[PUB] [LIST] removing subscriber from list\n");
    DEBUG (CMSG_INFO, "[PUB] [LIST] entry->method_name: %s\n", entry->method_name);

    GList *subscriber_list = g_list_first (publisher->subscriber_list);
    while (subscriber_list)
    {
        cmsg_sub_entry *list_entry = (cmsg_sub_entry *)subscriber_list->data;
        if (cmsg_sub_entry_compare (entry, list_entry))
        {
            DEBUG (CMSG_INFO, "[PUB] [LIST] deleting entry\n");
            publisher->subscriber_list = g_list_remove (publisher->subscriber_list, list_entry);
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
        cmsg_sub_entry *print_list_entry = (cmsg_sub_entry *)print_subscriber_list->data;
        DEBUG (CMSG_INFO,
               "[PUB] [LIST] print_list_entry->method_name: %s\n",
               print_list_entry->method_name);

        print_subscriber_list = g_list_next (print_subscriber_list);
    }
#endif

    return CMSG_RET_OK;
}


int32_t
cmsg_pub_server_receive (cmsg_pub *publisher,
                         int32_t   server_socket)
{
    int32_t ret = 0;

    CMSG_ASSERT (publisher);
    CMSG_ASSERT (server_socket > 0);

    DEBUG (CMSG_INFO, "[PUB]\n");

    cmsg_server *server = publisher->sub_server;

    ret = publisher->sub_server->transport->server_recv (server_socket, publisher->sub_server);

    if (ret < 0)
    {
        DEBUG (CMSG_ERROR, "[SERVER] server receive failed\n");
        return 0;
    }

    return ret;
}

int32_t
cmsg_pub_message_processor (cmsg_server *server,
                            uint8_t     *buffer_data)
{
    CMSG_ASSERT (server);
    CMSG_ASSERT (server->transport);
    CMSG_ASSERT (server->service);
    CMSG_ASSERT (server->service->descriptor);
    CMSG_ASSERT (server->server_request);
    CMSG_ASSERT (buffer_data);

    cmsg_server_request *server_request = server->server_request;
    ProtobufCMessage *message = NULL;
    ProtobufCAllocator *allocator = (ProtobufCAllocator *)server->allocator;

    if (server_request->method_index >= server->service->descriptor->n_methods)
    {
        DEBUG (CMSG_ERROR, "[PUB] the method index from read from the header seems to be to high\n");
        return 0;
    }

    if (!buffer_data)
    {
        DEBUG (CMSG_ERROR, "[PUB] buffer not defined");
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

    //this is calling: cmsg_pub_subscribe
    server->service->invoke (server->service,
                             server_request->method_index,
                             message,
                             server->transport->closure,
                             (void *)server);

    protobuf_c_message_free_unpacked (message, allocator);

    DEBUG (CMSG_INFO, "[PUB] end of message processor\n");
    return 0;
}

void
cmsg_pub_invoke (ProtobufCService       *service,
                 unsigned                method_index,
                 const ProtobufCMessage *input,
                 ProtobufCClosure        closure,
                 void                   *closure_data)
{
    int ret = 0;
    cmsg_pub *publisher = (cmsg_pub *)service;

    CMSG_ASSERT (service);
    CMSG_ASSERT (service->descriptor);
    CMSG_ASSERT (input);

    DEBUG (CMSG_INFO,
           "[PUB] publisher sending notification for: %s\n",
           service->descriptor->methods[method_index].name);

    //for each entry in pub->subscriber_entry
    GList *subscriber_list = g_list_first (publisher->subscriber_list);
    while (subscriber_list)
    {
        cmsg_sub_entry *list_entry = (cmsg_sub_entry *)subscriber_list->data;

        //just send to this client if it has subscribed for this notification before
        if (strcmp (service->descriptor->methods[method_index].name, list_entry->method_name))
        {
            DEBUG (CMSG_INFO,
                   "[PUB] subscriber has not subscribed to: %s, skipping\n",
                   service->descriptor->methods[method_index].name);

            subscriber_list = g_list_next (subscriber_list);
            continue;
        }
        else
        {
            DEBUG (CMSG_INFO,
                   "[PUB] subscriber has subscribed to: %s\n",
                   service->descriptor->methods[method_index].name);
        }


        DEBUG (CMSG_INFO, "[PUB] sending notification\n");
        if (list_entry->transport.type == CMSG_TRANSPORT_ONEWAY_TCP)
        {
            DEBUG (CMSG_INFO, "[PUB] [LIST]  tcp address: %x, port: %d\n",
                   ntohl (list_entry->transport.config.socket.sockaddr.in.sin_addr.s_addr),
                   ntohs (list_entry->transport.config.socket.sockaddr.in.sin_port));

            cmsg_transport_oneway_tcp_init (&list_entry->transport);
        }
        else if (list_entry->transport.type == CMSG_TRANSPORT_ONEWAY_TIPC)
        {
            DEBUG (CMSG_INFO, "[PUB] [LIST]  tipc type: %d, instance: %d\n",
                   list_entry->transport.config.socket.sockaddr.tipc.addr.name.name.type,
                   list_entry->transport.config.socket.sockaddr.tipc.addr.name.name.instance);

            cmsg_transport_oneway_tipc_init (&list_entry->transport);
        }

        cmsg_client *client = cmsg_client_new (&list_entry->transport, publisher->descriptor);

        //tell the client if we have to queue messages
        if (publisher->queue_enabled == 1)
        {
            DEBUG (CMSG_ERROR, "[PUB] queueing is enabled\n");
        }
        else
        {
            DEBUG (CMSG_ERROR, "[PUB] queueing is disabled");
        }

        client->queue_enabled = publisher->queue_enabled;
        //pass parent to client for queueing
        client->parent_type = CMSG_PARENT_TYPE_PUB;
        client->parent = (void *)publisher;

        //todo: remove client from subscriber list when connection failed
        cmsg_client_invoke_oneway ((ProtobufCService *)client,
                                   method_index,
                                   input,
                                   closure,
                                   closure_data);

        if (client->state == CMSG_CLIENT_STATE_FAILED)
        {
            DEBUG (CMSG_WARN, "[PUB] warning subscriber not reachable, removing from list\n");
            cmsg_pub_subscriber_remove (publisher, list_entry);
        }

        cmsg_client_destroy (&client);

        subscriber_list = g_list_next (subscriber_list);
    }
    return;
}

int32_t
cmsg_pub_subscribe (Cmsg__SubService_Service       *service,
                    const Cmsg__SubEntry           *input,
                    Cmsg__SubEntryResponse_Closure  closure,
                    void                           *closure_data)
{
    CMSG_ASSERT (service);
    CMSG_ASSERT (input);
    CMSG_ASSERT (closure_data);

    DEBUG (CMSG_INFO, "[PUB] cmsg_notification_subscriber_server_register_handler\n");
    cmsg_server *server = (cmsg_server *)closure_data;
    cmsg_pub *publisher = (cmsg_pub *)server->parent;

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
        subscriber_entry.transport.config.socket.sockaddr.in.sin_addr.s_addr = input->in_sin_addr_s_addr;
        subscriber_entry.transport.config.socket.sockaddr.in.sin_port = input->in_sin_port;
    }
    else if (input->transport_type == CMSG_TRANSPORT_ONEWAY_TIPC)
    {
        subscriber_entry.transport.config.socket.sockaddr.generic.sa_family = PF_TIPC;
        subscriber_entry.transport.config.socket.family = PF_TIPC;

        subscriber_entry.transport.type = input->transport_type;
        subscriber_entry.transport.config.socket.sockaddr.tipc.family = input->tipc_family;
        subscriber_entry.transport.config.socket.sockaddr.tipc.addrtype = input->tipc_addrtype;
        subscriber_entry.transport.config.socket.sockaddr.tipc.addr.name.domain = input->tipc_addr_name_domain;
        subscriber_entry.transport.config.socket.sockaddr.tipc.addr.name.name.instance = input->tipc_addr_name_name_instance;
        subscriber_entry.transport.config.socket.sockaddr.tipc.addr.name.name.type = input->tipc_addr_name_name_type;
        subscriber_entry.transport.config.socket.sockaddr.tipc.scope = input->tipc_scope;
    }
    else
    {
        DEBUG (CMSG_ERROR, "[PUB] error: subscriber transport not supported\n");

	return CMSG_RET_ERR;
    }

    if (input->add)
    {
        response.return_value = cmsg_pub_subscriber_add (publisher,
                                                         &subscriber_entry);
    }
    else
    {
        response.return_value = cmsg_pub_subscriber_remove (publisher,
                                                            &subscriber_entry);
    }

    closure (&response, closure_data);

    return CMSG_RET_OK;
}

int32_t
cmsg_pub_queue_process_one (cmsg_pub *publisher)
{
    uint32_t processed = 0;
    pthread_mutex_lock (&publisher->queue_mutex);

    cmsg_queue_entry *queue_entry = g_queue_pop_tail (publisher->queue);

    if (queue_entry)
    {
        //init transport
        if (queue_entry->transport.type == CMSG_TRANSPORT_ONEWAY_TIPC)
        {
            DEBUG (CMSG_ERROR, "[PUB QUEUE] queue_entry: transport tipc_init");
            cmsg_transport_oneway_tipc_init (&queue_entry->transport);
        }
        else if (queue_entry->transport.type == CMSG_TRANSPORT_ONEWAY_TCP)
        {
            DEBUG (CMSG_ERROR, "[PUB QUEUE] queue_entry: transport tcp_init");
            cmsg_transport_oneway_tcp_init (&queue_entry->transport);
        }
        else
        {
            DEBUG (CMSG_ERROR, "[PUB QUEUE] queue_entry: transport unknown");
        }

        cmsg_client *client = cmsg_client_new (&queue_entry->transport, publisher->descriptor);

        cmsg_client_connect (client);

        if (client->state == CMSG_CLIENT_STATE_CONNECTED)
        {
            DEBUG (CMSG_INFO, "[PUB QUEUE] sending message to server\n");
            int ret = client->transport->client_send (client,
                                                      queue_entry->queue_buffer,
                                                      queue_entry->queue_buffer_size,
                                                      0);

            if (ret < queue_entry->queue_buffer_size)
                DEBUG (CMSG_ERROR,
                       "[PUB QUEUE] sending response failed send:%d of %ld\n",
                       ret, queue_entry->queue_buffer_size);

            client->state = CMSG_CLIENT_STATE_DESTROYED;
            client->transport->client_close (client);

            publisher->queue_total_size -= queue_entry->queue_buffer_size;

            free (queue_entry->queue_buffer);
            g_free (queue_entry);

            processed++;
        }
        else
        {
            DEBUG (CMSG_ERROR, "[PUB QUEUE] error: client is not connected, requeueing message\n");
            g_queue_push_head (publisher->queue, queue_entry);
            publisher->queue_timeouts++;
        }
        cmsg_client_destroy (&client);
    }

    pthread_mutex_unlock (&publisher->queue_mutex);
    return processed;
}


int32_t
cmsg_pub_queue_process_all (cmsg_pub *publisher)
{
    uint32_t processed = 0;
    pthread_mutex_lock (&publisher->queue_mutex);

    cmsg_queue_entry *queue_entry = g_queue_pop_tail (publisher->queue);

    while (queue_entry)
    {
        //init transport
        if (queue_entry->transport.type == CMSG_TRANSPORT_ONEWAY_TIPC)
        {
            DEBUG (CMSG_ERROR, "[PUB QUEUE] queue_entry: transport tipc_init");
            cmsg_transport_oneway_tipc_init (&queue_entry->transport);
        }
        else if (queue_entry->transport.type == CMSG_TRANSPORT_ONEWAY_TCP)
        {
            DEBUG (CMSG_ERROR, "[PUB QUEUE] queue_entry: transport tipc_init");
            cmsg_transport_oneway_tcp_init (&queue_entry->transport);
        }
        else
        {
            DEBUG (CMSG_ERROR,
                   "[PUB QUEUE] queue_entry: transport unknown, transport: %d",
                   queue_entry->transport.type);
        }

        cmsg_client *client = cmsg_client_new (&queue_entry->transport, publisher->descriptor);

        cmsg_client_connect (client);

        if (client->state == CMSG_CLIENT_STATE_CONNECTED)
        {
            DEBUG (CMSG_INFO, "[PUB QUEUE] sending message to server\n");
            int ret = client->transport->client_send (client,
                                                      queue_entry->queue_buffer,
                                                      queue_entry->queue_buffer_size,
                                                      0);

            if (ret < queue_entry->queue_buffer_size)
            {
                DEBUG (CMSG_ERROR,
                       "[PUB QUEUE] sending response failed send:%d of %ld, queue message dropped\n",
                       ret, queue_entry->queue_buffer_size);
            }

            client->state = CMSG_CLIENT_STATE_DESTROYED;
            client->transport->client_close (client);

            publisher->queue_total_size -= queue_entry->queue_buffer_size;

            free (queue_entry->queue_buffer);
            g_free (queue_entry);

            processed++;
        }
        else
        {
            DEBUG (CMSG_ERROR, "[PUB QUEUE] error: client is not connected, requeueing message\n");
            g_queue_push_head (publisher->queue, queue_entry);

            unsigned int queue_length = g_queue_get_length (publisher->queue);
            DEBUG (CMSG_ERROR, "[PUB QUEUE] queue length: %d\n", queue_length);

            cmsg_client_destroy (&client);

            publisher->queue_timeouts++;

            //UNLOOCK in all cases when leaving
            pthread_mutex_unlock (&publisher->queue_mutex);

            int sleep_time = rand () % 5 + 1;

            DEBUG (CMSG_ERROR, "[PUB QUEUE] retrying in: %d seconds\n", sleep_time);
            sleep (sleep_time);
            DEBUG (CMSG_ERROR, "[PUB QUEUE] sleeping done\n");
            return -1;
        }
        cmsg_client_destroy (&client);

        //get the next entry
        queue_entry = g_queue_pop_tail (publisher->queue);
    }

    pthread_mutex_unlock (&publisher->queue_mutex);

    //todo: add thread signal
    sleep (1);

    return processed;
}


int32_t
cmsg_pub_queue_enable (cmsg_pub *publisher)
{
    publisher->queue_enabled = 1;
    return 0;
}


int32_t
cmsg_pub_queue_disable (cmsg_pub *publisher)
{
    publisher->queue_enabled = 0;

    cmsg_pub_queue_process_all (publisher);

    return 0;
}
