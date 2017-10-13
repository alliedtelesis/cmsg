/**
 * cmsg_composite_client.c
 *
 * Composite CMSG client
 *
 * The CMSG composite client is a group of CMSG clients that execute messages in parallel.
 * It's based on the composite design pattern, in that this client is used in essentially
 * the same way as a regular client.
 *
 * Note: Queueing/Filtering of messages is not supported on either the composite client
 *       or any of it's child clients.
 *
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */

#include "cmsg_private.h"
#include "cmsg_client.h"
#include "cmsg_error.h"

extern cmsg_client *cmsg_client_create (cmsg_transport *transport,
                                        const ProtobufCServiceDescriptor *descriptor);

/**
 * Send message to a group of clients. If any one message fails, an error is returned.
 * The caller must free any received data, which there may be some of even if an error
 * is returned, as the call may have work on one or more of the other clients.
 */
static int32_t
cmsg_composite_client_invoke (ProtobufCService *service, uint32_t method_index,
                              const ProtobufCMessage *input, ProtobufCClosure closure,
                              void *closure_data)
{
    GList *l;
    cmsg_client *composite_client = (cmsg_client *) service;
    cmsg_client *child;
    int i = 0;
    int ret;
    GQueue *invoke_recv_clients;
    int overall_result = CMSG_RET_OK;

    cmsg_client_closure_data *recv_data = (cmsg_client_closure_data *) closure_data;

    if (composite_client->child_clients == NULL)
    {
        return CMSG_RET_ERR;
    }

    invoke_recv_clients = g_queue_new ();

    pthread_mutex_lock (&composite_client->child_mutex);

    for (l = composite_client->child_clients; l != NULL; l = l->next)
    {
        child = (cmsg_client *) l->data;
        pthread_mutex_lock (&child->invoke_mutex);

        ret = child->invoke_send (child, method_index, input);
        if (ret == CMSG_RET_OK)
        {
            g_queue_push_tail (invoke_recv_clients, child);
        }
        else
        {
            overall_result = CMSG_RET_ERR;
            pthread_mutex_unlock (&child->invoke_mutex);
        }
        child->last_ret = ret;
    }

    // For each message successfully sent, receive the reply
    while ((child = g_queue_pop_head (invoke_recv_clients)) != NULL)
    {
        if (!child->invoke_recv)
        {
            // invoke_recv is NULL so nothing to do here (e.g. ONEWAY_TIPC transport type)
            pthread_mutex_unlock (&child->invoke_mutex);
            continue;
        }

        ret = child->invoke_recv (child, method_index, closure, &recv_data[i]);
        pthread_mutex_unlock (&child->invoke_mutex);

        if (ret == CMSG_RET_OK)
        {
            i++;
        }
        else
        {
            overall_result = ret;
        }
        child->last_ret = ret;
    }

    pthread_mutex_unlock (&composite_client->child_mutex);

    g_queue_free (invoke_recv_clients);

    return overall_result;
}

int32_t
cmsg_composite_client_add_child (cmsg_client *composite_client, cmsg_client *client)
{
    if (composite_client == NULL || client == NULL)
    {
        return -1;
    }

    if (client->_transport->type != CMSG_TRANSPORT_RPC_TCP &&
        client->_transport->type != CMSG_TRANSPORT_RPC_TIPC &&
        client->_transport->type != CMSG_TRANSPORT_LOOPBACK &&
        client->_transport->type != CMSG_TRANSPORT_ONEWAY_TIPC)
    {
        CMSG_LOG_CLIENT_ERROR (client,
                               "Transport type %d not supported for composite clients",
                               client->_transport->type);
        return -1;
    }

    pthread_mutex_lock (&composite_client->child_mutex);

    /* Since loopback clients execute the cmsg impl in the same thread as the api call
     * we place them at the end of the child client list so that they are invoked last.
     * This ensures the performance gains of using a composite client (i.e. executing in
     * parallel) are retained. */
    if (client->_transport->type == CMSG_TRANSPORT_LOOPBACK)
    {
        composite_client->child_clients = g_list_append (composite_client->child_clients,
                                                         client);
    }
    else
    {
        composite_client->child_clients = g_list_prepend (composite_client->child_clients,
                                                          client);
    }

    client->parent.object = composite_client;

    pthread_mutex_unlock (&composite_client->child_mutex);

    return 0;
}

int32_t
cmsg_composite_client_delete_child (cmsg_client *composite_client, cmsg_client *client)
{
    if (composite_client == NULL || client == NULL)
    {
        return -1;
    }

    pthread_mutex_lock (&composite_client->child_mutex);

    composite_client->child_clients = g_list_remove (composite_client->child_clients,
                                                     client);
    client->parent.object = NULL;

    pthread_mutex_unlock (&composite_client->child_mutex);

    return 0;
}


/**
 * Create a new composite CMSG client (but without creating counters).
 * Mostly it's the same as a regular client, but with the invoke function
 * overridden to point to the composite client version.
 */
cmsg_client *
cmsg_composite_client_new (const ProtobufCServiceDescriptor *descriptor)
{
    cmsg_client *client = cmsg_client_create (NULL, descriptor);

    if (client)
    {
        // Override the client->invoke with the composite-specific version
        client->invoke = cmsg_composite_client_invoke;
        client->base_service.invoke = cmsg_composite_client_invoke;

        if (pthread_mutex_init (&client->child_mutex, NULL) != 0)
        {
            CMSG_LOG_CLIENT_ERROR (client, "Init failed for child_mutex.");
            CMSG_FREE (client);
            return NULL;
        }
    }
    else
    {
        CMSG_LOG_GEN_ERROR ("Unable to create composite client.");
    }

    return client;
}

/**
 * Find a child client within a composite client based on tipc node id.
 *
 * @param composite_client - The composite client to look for the child client in.
 * @param id - The tipc id to lookup the child client by.
 *
 * @return - Pointer to the child client if found, NULL otherwise.
 */
cmsg_client *
cmsg_composite_client_lookup_by_tipc_id (cmsg_client *composite_client, uint32_t id)
{
    GList *l;
    cmsg_client *child;

    pthread_mutex_lock (&composite_client->child_mutex);

    for (l = composite_client->child_clients; l != NULL; l = l->next)
    {
        child = (cmsg_client *) l->data;
        if (child->_transport->type == CMSG_TRANSPORT_RPC_TIPC &&
            child->_transport->config.socket.sockaddr.tipc.addr.name.name.instance == id)
        {
            pthread_mutex_unlock (&composite_client->child_mutex);
            return child;
        }
    }

    pthread_mutex_unlock (&composite_client->child_mutex);
    return NULL;
}
