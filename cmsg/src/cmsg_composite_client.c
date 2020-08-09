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
#include "cmsg_composite_client.h"
#include "cmsg_composite_client_private.h"
#include "cmsg_client_private.h"

extern int32_t cmsg_client_init (cmsg_client *client, cmsg_transport *transport,
                                 const ProtobufCServiceDescriptor *descriptor);
extern void cmsg_client_deinit (cmsg_client *client);

#define CMSG_COMPOSITE_CLIENT_TYPE_CHECK_ERROR \
    "Composite client function called for non composite client type"

#define CMSG_COMPOSITE_CLIENT_TYPE_CHECK(CLIENT, RET_VAL)            \
do                                                                   \
{                                                                    \
    if ((CLIENT).self.object_type != CMSG_OBJ_TYPE_COMPOSITE_CLIENT) \
    {                                                                \
        CMSG_LOG_GEN_ERROR (CMSG_COMPOSITE_CLIENT_TYPE_CHECK_ERROR); \
        return (RET_VAL);                                            \
    }                                                                \
} while (0)

#define CMSG_COMPOSITE_CLIENT_TYPE_CHECK_VOID_RETURN(CLIENT)         \
do                                                                   \
{                                                                    \
    if ((CLIENT).self.object_type != CMSG_OBJ_TYPE_COMPOSITE_CLIENT) \
    {                                                                \
        CMSG_LOG_GEN_ERROR (CMSG_COMPOSITE_CLIENT_TYPE_CHECK_ERROR); \
        return;                                                      \
    }                                                                \
} while (0)

/**
 * Send message to a group of clients. If any one message fails, an error is returned.
 * The caller must free any received data, which there may be some of even if an error
 * is returned, as the call may have work on one or more of the other clients.
 */
static void
cmsg_composite_client_invoke (ProtobufCService *service, uint32_t method_index,
                              const ProtobufCMessage *input, ProtobufCClosure closure,
                              void *_closure_data)
{
    GList *l;
    cmsg_composite_client *composite_client = (cmsg_composite_client *) service;
    cmsg_client *child;
    int i = 0;
    int ret;
    GQueue *invoke_recv_clients;
    int overall_result = CMSG_RET_OK;

    cmsg_client_closure_data *closure_data = (cmsg_client_closure_data *) _closure_data;

    if (composite_client->child_clients == NULL)
    {
        closure_data->retval = CMSG_RET_OK;
        return;
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
            /* Don't let any other error overwrite a previous CMSG_RET_ERR */
            if (overall_result != CMSG_RET_ERR)
            {
                overall_result = ret;
            }
            pthread_mutex_unlock (&child->invoke_mutex);
        }
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

        ret = child->invoke_recv (child, method_index, closure, &closure_data[i]);
        pthread_mutex_unlock (&child->invoke_mutex);

        if (ret == CMSG_RET_OK)
        {
            i++;
        }
        else
        {
            overall_result = ret;
        }
    }

    pthread_mutex_unlock (&composite_client->child_mutex);

    g_queue_free (invoke_recv_clients);

    closure_data->retval = overall_result;
}

int32_t
cmsg_composite_client_add_child (cmsg_client *_composite_client, cmsg_client *client)
{
    cmsg_composite_client *composite_client = (cmsg_composite_client *) _composite_client;

    if (!composite_client || !client)
    {
        return CMSG_RET_ERR;
    }

    CMSG_COMPOSITE_CLIENT_TYPE_CHECK (composite_client->base_client, -1);

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

    return CMSG_RET_OK;
}

int32_t
cmsg_composite_client_delete_child (cmsg_client *_composite_client, cmsg_client *client)
{
    cmsg_composite_client *composite_client = (cmsg_composite_client *) _composite_client;

    if (!composite_client || !client)
    {
        return -1;
    }

    CMSG_COMPOSITE_CLIENT_TYPE_CHECK (composite_client->base_client, -1);

    pthread_mutex_lock (&composite_client->child_mutex);

    composite_client->child_clients = g_list_remove (composite_client->child_clients,
                                                     client);
    client->parent.object = NULL;

    pthread_mutex_unlock (&composite_client->child_mutex);

    return 0;
}

void
cmsg_composite_client_deinit (cmsg_composite_client *comp_client)
{
    cmsg_client_deinit (&comp_client->base_client);

    if (comp_client->child_clients)
    {
        g_list_free (comp_client->child_clients);
    }

    pthread_mutex_destroy (&comp_client->child_mutex);
}

static void
cmsg_composite_client_destroy (cmsg_client *client)
{
    cmsg_composite_client *comp_client = (cmsg_composite_client *) client;

    cmsg_composite_client_deinit (comp_client);

    CMSG_FREE (client);
}

/**
 * Destroys a composite client and all of the child clients it contains.
 *
 * @param _composite_client - The composite client to destroy.
 */
void
cmsg_composite_client_destroy_full (cmsg_client *_composite_client)
{
    cmsg_composite_client_free_all_children (_composite_client);
    cmsg_composite_client_destroy (_composite_client);
}

/**
 * Send a buffer of bytes on a composite client. Note that sending anything other than
 * a well formed cmsg packet will be dropped by the server being sent to.
 *
 * @param client - The client to send on.
 * @param buffer - The buffer of bytes to send.
 * @param buffer_len - The length of the buffer being sent.
 * @param method_name - The name of the method being invoked.
 *
 * @returns CMSG_RET_OK on success, related error code on failure.
 */
static int32_t
cmsg_composite_client_send_bytes (cmsg_client *client, uint8_t *buffer, uint32_t buffer_len,
                                  const char *method_name)
{
    GList *l;
    cmsg_composite_client *composite_client = (cmsg_composite_client *) client;
    cmsg_client *child;
    int ret;
    int overall_result = CMSG_RET_OK;

    if (composite_client->child_clients == NULL)
    {
        return CMSG_RET_OK;
    }

    pthread_mutex_lock (&composite_client->child_mutex);

    for (l = composite_client->child_clients; l != NULL; l = l->next)
    {
        child = (cmsg_client *) l->data;

        ret = child->send_bytes (child, buffer, buffer_len, method_name);
        if (ret != CMSG_RET_OK)
        {
            /* Don't let any other error overwrite a previous CMSG_RET_ERR */
            if (overall_result != CMSG_RET_ERR)
            {
                overall_result = ret;
            }
        }
    }

    pthread_mutex_unlock (&composite_client->child_mutex);

    return overall_result;
}

int32_t
cmsg_composite_client_init (cmsg_composite_client *comp_client,
                            const ProtobufCServiceDescriptor *descriptor)
{
    if (cmsg_client_init (&comp_client->base_client, NULL, descriptor) != CMSG_RET_OK)
    {
        return CMSG_RET_ERR;
    }

    // Override the client->invoke with the composite-specific version
    comp_client->base_client.invoke = cmsg_composite_client_invoke;
    comp_client->base_client.base_service.invoke = cmsg_composite_client_invoke;
    comp_client->base_client.self.object_type = CMSG_OBJ_TYPE_COMPOSITE_CLIENT;

    comp_client->base_client.client_destroy = cmsg_composite_client_destroy;
    comp_client->base_client.send_bytes = cmsg_composite_client_send_bytes;

    comp_client->child_clients = NULL;

    if (pthread_mutex_init (&comp_client->child_mutex, NULL) != 0)
    {
        CMSG_LOG_GEN_ERROR ("Init failed for child_mutex.");
        return CMSG_RET_ERR;
    }

    return CMSG_RET_OK;
}


/**
 * Create a new composite CMSG client (but without creating counters).
 * Mostly it's the same as a regular client, but with the invoke function
 * overridden to point to the composite client version.
 */
cmsg_client *
cmsg_composite_client_new (const ProtobufCServiceDescriptor *descriptor)
{
    cmsg_composite_client *comp_client = NULL;

    comp_client = (cmsg_composite_client *) CMSG_CALLOC (1, sizeof (cmsg_composite_client));

    if (comp_client)
    {
        if (cmsg_composite_client_init (comp_client, descriptor) != CMSG_RET_OK)
        {
            CMSG_FREE (comp_client);
            return NULL;
        }
    }
    else
    {
        CMSG_LOG_GEN_ERROR ("Unable to create composite client.");
    }

    return &comp_client->base_client;
}

/**
 * Find a child client within a composite client based on tipc node id.
 *
 * @param _composite_client - The composite client to look for the child client in.
 * @param id - The tipc id to lookup the child client by.
 *
 * @return - Pointer to the child client if found, NULL otherwise.
 */
cmsg_client *
cmsg_composite_client_lookup_by_tipc_id (cmsg_client *_composite_client, uint32_t id)
{
    GList *l;
    cmsg_client *child;
    cmsg_composite_client *composite_client = (cmsg_composite_client *) _composite_client;

    CMSG_COMPOSITE_CLIENT_TYPE_CHECK (composite_client->base_client, NULL);

    pthread_mutex_lock (&composite_client->child_mutex);

    for (l = composite_client->child_clients; l != NULL; l = l->next)
    {
        child = (cmsg_client *) l->data;
        if ((child->_transport->type == CMSG_TRANSPORT_RPC_TIPC ||
             child->_transport->type == CMSG_TRANSPORT_ONEWAY_TIPC) &&
            child->_transport->config.socket.sockaddr.tipc.addr.name.name.instance == id)
        {
            pthread_mutex_unlock (&composite_client->child_mutex);
            return child;
        }
    }

    pthread_mutex_unlock (&composite_client->child_mutex);
    return NULL;
}

/**
 * Find a child client within a composite client based on ip address.
 *
 * @param _composite_client - The composite client to look for the child client in.
 * @param addr - The address to lookup the child client by.
 *
 * @return - Pointer to the child client if found, NULL otherwise.
 */
cmsg_client *
cmsg_composite_client_lookup_by_tcp_ipv4_addr (cmsg_client *_composite_client,
                                               uint32_t addr)
{
    GList *l;
    cmsg_client *child;
    cmsg_composite_client *composite_client = (cmsg_composite_client *) _composite_client;

    CMSG_COMPOSITE_CLIENT_TYPE_CHECK (composite_client->base_client, NULL);

    pthread_mutex_lock (&composite_client->child_mutex);

    for (l = composite_client->child_clients; l != NULL; l = l->next)
    {
        child = (cmsg_client *) l->data;
        if ((child->_transport->type == CMSG_TRANSPORT_RPC_TCP ||
             child->_transport->type == CMSG_TRANSPORT_ONEWAY_TCP) &&
            child->_transport->config.socket.sockaddr.in.sin_addr.s_addr == addr)
        {
            pthread_mutex_unlock (&composite_client->child_mutex);
            return child;
        }
    }

    pthread_mutex_unlock (&composite_client->child_mutex);
    return NULL;
}

/**
 * Find a child client within a composite client based on ip address.
 *
 * @param _composite_client - The composite client to look for the child client in.
 * @param addr - The address to lookup the child client by.
 *
 * @return - Pointer to the child client if found, NULL otherwise.
 */
cmsg_client *
cmsg_composite_client_lookup_by_tcp_ipv6_addr (cmsg_client *_composite_client,
                                               struct in6_addr *addr)
{
    GList *l;
    cmsg_client *child;
    cmsg_composite_client *composite_client = (cmsg_composite_client *) _composite_client;

    CMSG_COMPOSITE_CLIENT_TYPE_CHECK (composite_client->base_client, NULL);

    pthread_mutex_lock (&composite_client->child_mutex);

    for (l = composite_client->child_clients; l != NULL; l = l->next)
    {
        child = (cmsg_client *) l->data;
        if ((child->_transport->type == CMSG_TRANSPORT_RPC_TCP ||
             child->_transport->type == CMSG_TRANSPORT_ONEWAY_TCP) &&
            memcmp (&child->_transport->config.socket.sockaddr.in6.sin6_addr,
                    addr, sizeof (struct in6_addr)) == 0)
        {
            pthread_mutex_unlock (&composite_client->child_mutex);
            return child;
        }
    }

    pthread_mutex_unlock (&composite_client->child_mutex);
    return NULL;
}

int
cmsg_composite_client_num_children (cmsg_client *_composite_client)
{
    cmsg_composite_client *composite_client = (cmsg_composite_client *) _composite_client;

    CMSG_COMPOSITE_CLIENT_TYPE_CHECK (composite_client->base_client, 0);

    return g_list_length (composite_client->child_clients);
}

GList *
cmsg_composite_client_get_children (cmsg_client *_composite_client)
{
    cmsg_composite_client *composite_client = (cmsg_composite_client *) _composite_client;

    CMSG_COMPOSITE_CLIENT_TYPE_CHECK (composite_client->base_client, NULL);

    return composite_client->child_clients;
}

void
cmsg_composite_client_free_all_children (cmsg_client *_composite_client)
{
    cmsg_composite_client *composite_client = (cmsg_composite_client *) _composite_client;
    GList *l;
    cmsg_client *child;

    CMSG_COMPOSITE_CLIENT_TYPE_CHECK_VOID_RETURN (composite_client->base_client);

    pthread_mutex_lock (&composite_client->child_mutex);

    for (l = composite_client->child_clients; l != NULL; l = l->next)
    {
        child = (cmsg_client *) l->data;
        cmsg_destroy_client_and_transport (child);
    }

    g_list_free (composite_client->child_clients);
    composite_client->child_clients = NULL;

    pthread_mutex_unlock (&composite_client->child_mutex);
}
