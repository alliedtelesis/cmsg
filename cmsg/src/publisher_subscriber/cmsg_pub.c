/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#include "cmsg_private.h"
#include "cmsg_pub.h"
#include "cmsg_error.h"
#include "cmsg_sub_service.pb-c.h"

#ifdef HAVE_COUNTERD
#include "cntrd_app_defines.h"
#endif

//service implementation for handling register messages from the subscriber
int32_t cmsg_pub_subscribe (cmsg_sub_service_Service *service,
                            const cmsg_sub_entry_transport_info *input,
                            cmsg_sub_entry_response_Closure closure, void *closure_data);

//macro for register handler implementation
cmsg_sub_service_Service cmsg_pub_subscriber_service = CMSG_SUB_SERVICE_INIT (cmsg_pub_);

static void _cmsg_pub_subscriber_delete (cmsg_pub *publisher, cmsg_sub_entry *entry);
static void _cmsg_pub_subscriber_delete_link (cmsg_pub *publisher, GList *link);

static int32_t _cmsg_pub_queue_process_all_direct (cmsg_pub *publisher);

static int32_t cmsg_pub_message_processor (int socket, cmsg_server_request *server_request,
                                           cmsg_server *server, uint8_t *buffer_data);

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

    if (cmsg_transport_compare (one->transport, two->transport) &&
        (strcmp (one->method_name, two->method_name) == 0) &&
        (!one->to_be_removed && !two->to_be_removed))
    {
        return 0;
    }

    return -1;
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
              pub_service->name,
              cmsg_transport_counter_app_tport_id (sub_server_transport));

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

    if (pthread_mutex_init (&publisher->queue_mutex, NULL) != 0)
    {
        CMSG_LOG_PUBLISHER_ERROR (publisher, "Init failed for queue_mutex.");
        return NULL;
    }

    publisher->queue = g_queue_new ();
    publisher->queue_filter_hash_table = g_hash_table_new (g_str_hash, g_str_equal);
    publisher->queue_thread_running = false;

    if (pthread_cond_init (&publisher->queue_process_cond, NULL) != 0)
    {
        CMSG_LOG_PUBLISHER_ERROR (publisher, "Init failed for queue_process_cond.");
        return NULL;
    }

    if (pthread_mutex_init (&publisher->subscriber_list_mutex, NULL) != 0)
    {
        CMSG_LOG_PUBLISHER_ERROR (publisher, "Init failed for subscriber_list_mutex.");
        return NULL;
    }

    publisher->self_thread_id = pthread_self ();

    cmsg_pub_queue_filter_init (publisher);

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

    cmsg_pub_queue_thread_stop (publisher);

    cmsg_pub_subscriber_remove_all (publisher);

    g_list_free (publisher->subscriber_list);

    publisher->subscriber_list = NULL;

    cmsg_queue_filter_free (publisher->queue_filter_hash_table, publisher->descriptor);

    g_hash_table_destroy (publisher->queue_filter_hash_table);

    cmsg_send_queue_destroy (publisher->queue);

    pthread_mutex_destroy (&publisher->queue_mutex);

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
    int timeout;

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
            /* During stack failover we need connect attempts for a publisher to
             * remote nodes that have now left to fail quickly. Otherwise the system
             * will hang using the default TIPC timeout (30 seconds) and the stack will
             * fall apart. */
            timeout = CMSG_TRANSPORT_TIPC_PUB_CONNECT_TIMEOUT;
            if (cmsg_client_connect_with_timeout (list_entry->client,
                                                  timeout) != CMSG_RET_OK)
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

    if (entry->in_use > 0)
    {
        CMSG_DEBUG (CMSG_INFO, "[PUB] [LIST] marking entry for deletion\n");
        entry->to_be_removed = true;
    }
    else
    {
        CMSG_DEBUG (CMSG_INFO, "[PUB] [LIST] deleting entry\n");

        link->data = NULL;

        publisher->subscriber_list = g_list_delete_link (publisher->subscriber_list, link);
        publisher->subscriber_count--;

        /* Before destroying client/transport, clean up messages for that subscriber */
        pthread_mutex_lock (&publisher->queue_mutex);
        cmsg_send_queue_free_all_by_single_transport (publisher->queue, entry->transport);
        pthread_mutex_unlock (&publisher->queue_mutex);

        cmsg_client_destroy (entry->client);
        cmsg_transport_destroy (entry->transport);
        CMSG_FREE (entry);
    }
}


/**
 * Delete and free a subscriber entry from the publisher.
 * If the entry is currently "in-use", the entry is marks as "to_be_removed",
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
 * Clean up a subscriber if it's marked as 'to_be_removed'.
 */
static int32_t
_cmsg_pub_subscriber_cleanup_link (cmsg_pub *publisher, GList *link)
{
    cmsg_sub_entry *entry = (cmsg_sub_entry *) link->data;

    /* Delete a subscriber if it's marked as 'to_be_removed' unless it's still used
     * by other thread */
    if (entry->in_use == 0 && entry->to_be_removed)
    {
        _cmsg_pub_subscriber_delete_link (publisher, link);
    }

    return CMSG_RET_OK;
}

/**
 * Clean up all subscriber entries marked as 'to_be_removed'.
 * If a subscriber is in use when unscribe happens, then this clean up
 * function should be called later on to clean up subscribers.
 */
int32_t
cmsg_pub_subscriber_cleanup (cmsg_pub *publisher)
{
    GList *list;
    GList *list_next;

    pthread_mutex_lock (&publisher->subscriber_list_mutex);

    /* walk the list and get a client connection for every subscription */
    for (list = g_list_first (publisher->subscriber_list); list; list = list_next)
    {
        list_next = g_list_next (list);

        _cmsg_pub_subscriber_cleanup_link (publisher, list);
    }

    pthread_mutex_unlock (&publisher->subscriber_list_mutex);

    return CMSG_RET_OK;
}

/**
 * Set or unset 'in_use' flag on subscribers using the specified transport.
 */
static int32_t
_cmsg_pub_subscriber_set_transport_in_use (cmsg_pub *publisher, cmsg_transport *transport,
                                           bool set)
{
    GList *list;
    GList *list_next;
    cmsg_sub_entry *entry;

    CMSG_ASSERT_RETURN_VAL (publisher != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (transport != NULL, CMSG_RET_ERR);

    for (list = g_list_first (publisher->subscriber_list); list; list = list_next)
    {
        entry = (cmsg_sub_entry *) list->data;
        list_next = g_list_next (list);

        if (entry->transport == transport)
        {
            entry->in_use += set ? 1 : -1;
            break;
        }
    }

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

        if (cmsg_transport_compare (list_entry->transport, transport))
        {
            CMSG_DEBUG (CMSG_INFO, "[PUB] [LIST] marking entry for %s for deletion\n",
                        list_entry->method_name);

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
        _cmsg_pub_subscriber_delete_link (publisher, subscriber_list);
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
cmsg_pub_message_processor (int socket, cmsg_server_request *server_request,
                            cmsg_server *server, uint8_t *buffer_data)
{
    CMSG_ASSERT_RETURN_VAL (server != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (server->_transport != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (server->service != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (server->service->descriptor != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (server_request != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (buffer_data != NULL, CMSG_RET_ERR);

    ProtobufCMessage *message = NULL;
    ProtobufCAllocator *allocator = &cmsg_memory_allocator;
    cmsg_server_closure_data closure_data;
    const ProtobufCMessageDescriptor *desc;

    // Check for a connection open message, discard if received as we do not
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
    closure_data.server_request = server_request;
    closure_data.reply_socket = socket;
    closure_data.method_processing_reason = CMSG_METHOD_OK_TO_INVOKE;

    //this is calling: cmsg_pub_subscribe
    server->service->invoke ((ProtobufCService *) server->service,
                             server_request->method_index, message,
                             server->closure, (void *) &closure_data);

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
    bool queue_packet = false;
    uint8_t *packet = NULL;
    uint32_t total_message_size = 0;

    CMSG_ASSERT_RETURN_VAL (service != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (service->descriptor != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (input != NULL, CMSG_RET_ERR);

    method_name = service->descriptor->methods[method_index].name;

    CMSG_DEBUG (CMSG_INFO, "[PUB] publisher sending notification for: %s\n", method_name);

    cmsg_queue_filter_type action = cmsg_pub_queue_filter_lookup (publisher,
                                                                  method_name);

    switch (action)
    {
    case CMSG_QUEUE_FILTER_ERROR:
        CMSG_LOG_PUBLISHER_ERROR (publisher,
                                  "queue_lookup_filter returned an error for: %s\n",
                                  method_name);
        return CMSG_RET_ERR;

    case CMSG_QUEUE_FILTER_DROP:
        CMSG_DEBUG (CMSG_ERROR, "[PUB] dropping message: %s\n", method_name);
        return CMSG_RET_OK;

    case CMSG_QUEUE_FILTER_PROCESS:
        queue_packet = false;
        break;

    case CMSG_QUEUE_FILTER_QUEUE:
        queue_packet = true;
        break;

    default:
        CMSG_LOG_PUBLISHER_ERROR (publisher, "Bad action for queue filter. Action:%d.",
                                  action);
        return CMSG_RET_ERR;
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

        //pass parent to client for queueing using correct queue
        list_entry->client->parent = publisher->self;

        // Mark the entry is "in-use" so it cannot be removed while sending notification
        list_entry->in_use++;

        // Unlock list to send notification
        pthread_mutex_unlock (&publisher->subscriber_list_mutex);

        if (queue_packet)
        {
            ret = cmsg_client_create_packet (list_entry->client, method_name, input,
                                             &packet, &total_message_size);
            if (ret == CMSG_RET_OK)
            {
                pthread_mutex_lock (&publisher->queue_mutex);

                //todo: check return
                cmsg_send_queue_push (publisher->queue, packet,
                                      total_message_size,
                                      list_entry->client, list_entry->client->_transport,
                                      (char *) method_name);

                //send signal to  cmsg_pub_queue_process_all
                pthread_cond_signal (&publisher->queue_process_cond);
                pthread_mutex_unlock (&publisher->queue_mutex);

                // Execute callback function if configured
                if (list_entry->client->queue_callback_func != NULL)
                {
                    list_entry->client->queue_callback_func (list_entry->client,
                                                             method_name);
                }

                CMSG_FREE (packet);
            }
        }
        else
        {
            int i = 0;
            for (i = 0; i <= CMSG_TRANSPORT_CLIENT_SEND_TRIES; i++)
            {
                ret = list_entry->client->invoke ((ProtobufCService *) list_entry->client,
                                                  method_index, input, NULL, NULL);
                if (ret == CMSG_RET_ERR)
                {
                    //try again
                    CMSG_LOG_PUBLISHER_DEBUG (publisher,
                                              "Client invoke failed (method: %s).",
                                              method_name);
                }
                else
                {
                    break;
                }
            }
        }

        pthread_mutex_lock (&publisher->subscriber_list_mutex);

        // Now unset "in-use" mark after sending notification
        list_entry->in_use--;

        list_next = g_list_next (list);

        if (ret == CMSG_RET_ERR)
        {
            CMSG_LOG_PUBLISHER_ERROR (publisher,
                                      "Failed to send notification (method: %s). Removing subscription",
                                      method_name);
            _cmsg_pub_subscriber_delete_link (publisher, list);
        }

        /* Clean up the entry (in case the subscriber is mark as 'to_be_removed') */
        _cmsg_pub_subscriber_cleanup_link (publisher, list);
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
        /* Lock before calling _cmsg_pub_subscriber_delete(), which modifies the publisher.
         * But make sure the order: subscriber_list_mutex and then queue_mutex */
        pthread_mutex_lock (&publisher->subscriber_list_mutex);

        //delete queued entries for the method being un-subscribed
        if (g_queue_get_length (publisher->queue))
        {
            pthread_mutex_lock (&publisher->queue_mutex);
            cmsg_send_queue_free_by_single_transport_method (publisher->queue,
                                                             subscriber_entry->transport,
                                                             subscriber_entry->method_name);
            pthread_mutex_unlock (&publisher->queue_mutex);
        }
        _cmsg_pub_subscriber_delete (publisher, subscriber_entry);
        response.return_value = CMSG_RET_OK;

        pthread_mutex_unlock (&publisher->subscriber_list_mutex);

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
    cmsg_pub_queue_filter_set_all (publisher, CMSG_QUEUE_FILTER_QUEUE);
}

static void *
cmsg_pub_queue_process_thread (void *arg)
{
    cmsg_pub *publisher = arg;
    while (1)
    {
        cmsg_pub_queue_process_all (publisher);
    }
    return NULL;
}

void
cmsg_pub_queue_thread_start (cmsg_pub *publisher)
{
    if (!publisher->queue_thread_running)
    {
        if (pthread_create
            (&publisher->queue_thread_id, NULL, cmsg_pub_queue_process_thread,
             publisher) == 0)
        {
            publisher->queue_thread_running = true;
        }
        else
        {
            CMSG_LOG_PUBLISHER_ERROR (publisher, "Unable to start publisher queue thread");
        }

        cmsg_pthread_setname (publisher->queue_thread_id,
                              publisher->descriptor->short_name, CMSG_PUBLISHER_PREFIX);
    }
}

void
cmsg_pub_queue_free_all (cmsg_pub *publisher)
{
    pthread_mutex_lock (&publisher->queue_mutex);
    cmsg_send_queue_free_all (publisher->queue);
    pthread_mutex_unlock (&publisher->queue_mutex);
}

int32_t
cmsg_pub_queue_disable (cmsg_pub *publisher)
{
    cmsg_pub_queue_filter_set_all (publisher, CMSG_QUEUE_FILTER_PROCESS);

    return cmsg_pub_queue_process_all (publisher);
}

void
cmsg_pub_queue_thread_stop (cmsg_pub *publisher)
{
    if (publisher->queue_thread_running)
    {
        pthread_cancel (publisher->queue_thread_id);
        pthread_join (publisher->queue_thread_id, NULL);
        publisher->queue_thread_running = false;
    }
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
    //if the we run do api calls and processing in different threads wait
    //for a signal from the api thread to start processing
    if (!pthread_equal (publisher->self_thread_id, pthread_self ()))
    {
        pthread_mutex_lock (&publisher->queue_mutex);
        // Ensure mutex is unlocked if we are cancelled
        pthread_cleanup_push ((void (*)(void *)) pthread_mutex_unlock,
                              &publisher->queue_mutex);
        while (g_queue_get_length (publisher->queue) == 0)
        {
            pthread_cond_wait (&publisher->queue_process_cond, &publisher->queue_mutex);
        }
        // Unlock mutex
        pthread_cleanup_pop (1);
    }

    return _cmsg_pub_queue_process_all_direct (publisher);
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
    int ret;

    if (!queue || !descriptor)
    {
        return 0;
    }

    /* Lock before calling _cmsg_pub_subscriber_set_transport_in_use(), which modifies
     * the publisher.
     * But make sure the order: subscriber_list_mutex and then queue_mutex */
    pthread_mutex_lock (&publisher->subscriber_list_mutex);
    pthread_mutex_lock (queue_mutex);
    if (g_queue_get_length (queue))
    {
        queue_entry = (cmsg_send_queue_entry *) g_queue_pop_tail (queue);

        /* Mark the transport is in-use in the subscriber list */
        if (queue_entry)
        {
            _cmsg_pub_subscriber_set_transport_in_use (publisher, queue_entry->transport,
                                                       true);
        }
    }
    pthread_mutex_unlock (queue_mutex);
    pthread_mutex_unlock (&publisher->subscriber_list_mutex);


    while (queue_entry)
    {
        send_client = queue_entry->client;

        ret = cmsg_client_buffer_send_retry (send_client,
                                             queue_entry->queue_buffer,
                                             queue_entry->queue_buffer_size,
                                             CMSG_TRANSPORT_CLIENT_SEND_TRIES);

        /* Clear the in-use mark after sending message */
        pthread_mutex_lock (&publisher->subscriber_list_mutex);
        _cmsg_pub_subscriber_set_transport_in_use (publisher, queue_entry->transport,
                                                   false);
        pthread_mutex_unlock (&publisher->subscriber_list_mutex);

        if (ret == CMSG_RET_OK)
        {
            processed++;
        }
        else
        {
            //remove subscriber from subscription list
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

        /* Lock before modifying publisher by _cmsg_pub_subscriber_set_transport_in_use().
         * But make sure the order: subscriber_list_mutex and then queue_mutex */
        pthread_mutex_lock (&publisher->subscriber_list_mutex);
        pthread_mutex_lock (queue_mutex);
        /* Get the next entry */
        queue_entry = (cmsg_send_queue_entry *) g_queue_pop_tail (queue);
        /* Mark the transport is in-use in the subscriber list */
        if (queue_entry)
        {
            _cmsg_pub_subscriber_set_transport_in_use (publisher, queue_entry->transport,
                                                       true);
        }
        pthread_mutex_unlock (queue_mutex);
        pthread_mutex_unlock (&publisher->subscriber_list_mutex);
    }

    /* Clean up any subscriber to be removed */
    cmsg_pub_subscriber_cleanup (publisher);

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

cmsg_pub *
cmsg_create_publisher_tipc_rpc (const char *server_name, int member_id,
                                int scope, const ProtobufCServiceDescriptor *descriptor)
{
    cmsg_transport *transport = NULL;
    cmsg_pub *publisher = NULL;

    CMSG_ASSERT_RETURN_VAL (server_name != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (descriptor != NULL, NULL);

    transport = cmsg_create_transport_tipc (server_name, member_id, scope,
                                            CMSG_TRANSPORT_RPC_TIPC);
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
