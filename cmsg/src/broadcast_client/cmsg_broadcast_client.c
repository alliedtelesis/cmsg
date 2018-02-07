/**
 * cmsg_broadcast_client.c
 *
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */

#include "cmsg_broadcast_client_private.h"
#include "cmsg_broadcast_client.h"
#include "cmsg_error.h"
#include "cmsg_composite_client.h"

static cmsg_broadcast_client *
cmsg_broadcast_client_create (const ProtobufCServiceDescriptor *descriptor)
{
    cmsg_broadcast_client *broadcast_client = NULL;

    broadcast_client =
        (cmsg_broadcast_client *) CMSG_CALLOC (1, sizeof (cmsg_broadcast_client));

    if (broadcast_client)
    {
        if (!cmsg_composite_client_init (&broadcast_client->base_client, descriptor))
        {
            CMSG_FREE (broadcast_client);
            return NULL;
        }

        broadcast_client->oneway_children = false;
        broadcast_client->service_entry_name = NULL;
        broadcast_client->server = NULL;
        broadcast_client->loopback_client = NULL;
        broadcast_client->my_node_id = 0;
        broadcast_client->lower_node_id = 0;
        broadcast_client->upper_node_id = 0;
        broadcast_client->tipc_subscription_sd = -1;
        broadcast_client->type = CMSG_BROADCAST_LOCAL_NONE;
        broadcast_client->accept_sd_queue = NULL;
        broadcast_client->accept_sd_eventfd = -1;
    }
    else
    {
        CMSG_LOG_GEN_ERROR ("Unable to create broadcast client.");
    }

    return broadcast_client;
}

static cmsg_server *
cmsg_broadcast_server_create (ProtobufCService *service, const char *service_entry_name,
                              uint32_t my_node_id, bool oneway)
{
    cmsg_server *server = NULL;

    if (oneway)
    {
        server = cmsg_create_server_tipc_oneway (service_entry_name, my_node_id,
                                                 TIPC_CLUSTER_SCOPE, service);
    }
    else
    {
        server = cmsg_create_server_tipc_rpc (service_entry_name, my_node_id,
                                              TIPC_CLUSTER_SCOPE, service);
    }

    return server;
}

/**
 * Create a cmsg broadcast client
 *
 * @param service - Pointer to the ProtobufCService service structure.
 * @param service_entry_name - The name of the service to look up the TIPC port from
 *                             in the /etc/services file.
 * @param my_node_id - The TIPC ID to use for this node.
 * @param lower_node_id - The lowest TIPC ID we are interested in broadcasting to.
 *                        (STK_NODEID_MIN)
 * @param upper_node_id - The highest TIPC ID we are interest in broadcasting to.
 *                        (STK_NODEID_MAX)
 * @param type - The type of cmsg broadcast to initialise.
 * @param one_way - Whether to do one way broadcasting (true), or RPC broadcasting (false).
 *
 * @return pointer to the client on success, NULL otherwise.
 */
cmsg_client *
cmsg_broadcast_client_new (ProtobufCService *service, const char *service_entry_name,
                           uint32_t my_node_id, uint32_t lower_node_id,
                           uint32_t upper_node_id, cmsg_broadcast_local_type type,
                           bool oneway)
{
    int ret = CMSG_RET_OK;
    cmsg_broadcast_client *broadcast_client = NULL;
    cmsg_client *loopback_client = NULL;

    CMSG_ASSERT_RETURN_VAL (service != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (service_entry_name != NULL, NULL);

    broadcast_client = cmsg_broadcast_client_create (service->descriptor);
    if (!broadcast_client)
    {
        return NULL;
    }

    broadcast_client->service_entry_name = service_entry_name;
    broadcast_client->oneway_children = oneway;
    broadcast_client->my_node_id = my_node_id;
    broadcast_client->lower_node_id = lower_node_id;
    broadcast_client->upper_node_id = upper_node_id;
    broadcast_client->type = type;

    if (broadcast_client->type == CMSG_BROADCAST_LOCAL_LOOPBACK)
    {
        loopback_client = cmsg_create_client_loopback (service);
        if (!loopback_client)
        {
            cmsg_composite_client_deinit (&broadcast_client->base_client);
            CMSG_FREE (broadcast_client);
            return NULL;
        }
    }

    broadcast_client->server = cmsg_broadcast_server_create (service, service_entry_name,
                                                             my_node_id, oneway);
    if (!broadcast_client->server)
    {
        cmsg_destroy_client_and_transport (loopback_client);
        cmsg_composite_client_deinit (&broadcast_client->base_client);
        CMSG_FREE (broadcast_client);
        return NULL;
    }

    ret = cmsg_broadcast_conn_mgmt_init (broadcast_client);
    if (ret != CMSG_RET_OK)
    {
        cmsg_destroy_client_and_transport (loopback_client);
        cmsg_composite_client_deinit (&broadcast_client->base_client);
        cmsg_destroy_server_and_transport (broadcast_client->server);
        CMSG_FREE (broadcast_client);
        return NULL;
    }

    /* Setup was successful, add the loopback client onto the broadcast composite */
    cmsg_composite_client_add_child ((cmsg_client *) &broadcast_client->base_client,
                                     loopback_client);
    broadcast_client->loopback_client = loopback_client;

    return (cmsg_client *) broadcast_client;
}

/**
 * Destroy the broadcast client.
 */
void
cmsg_broadcast_client_destroy (cmsg_client *client)
{
    cmsg_broadcast_client *broadcast_client = (cmsg_broadcast_client *) client;
    GList *children = NULL;
    GList *l;
    cmsg_client *child;

    /* Connection management must be stopped before destroying client */
    cmsg_broadcast_conn_mgmt_deinit (broadcast_client);

    children =
        cmsg_composite_client_get_children ((cmsg_client *) &broadcast_client->base_client);
    for (l = children; l != NULL; l = l->next)
    {
        child = (cmsg_client *) l->data;
        cmsg_destroy_client_and_transport (child);
    }

    cmsg_composite_client_deinit (&broadcast_client->base_client);

    cmsg_destroy_server_and_transport (broadcast_client->server);

    CMSG_FREE (broadcast_client);
}

cmsg_server *
cmsg_broadcast_client_get_server (cmsg_client *client)
{
    cmsg_broadcast_client *broadcast_client = (cmsg_broadcast_client *) client;

    CMSG_ASSERT_RETURN_VAL (broadcast_client != NULL, NULL);

    return broadcast_client->server;
}

GAsyncQueue *
cmsg_broadcast_client_get_accept_queue (cmsg_client *client)
{
    cmsg_broadcast_client *broadcast_client = (cmsg_broadcast_client *) client;

    CMSG_ASSERT_RETURN_VAL (broadcast_client != NULL, NULL);

    return broadcast_client->accept_sd_queue;
}

int
cmsg_broadcast_client_get_accept_eventfd (cmsg_client *client)
{
    cmsg_broadcast_client *broadcast_client = (cmsg_broadcast_client *) client;

    CMSG_ASSERT_RETURN_VAL (broadcast_client != NULL, -1);

    return broadcast_client->accept_sd_eventfd;
}

cmsg_client *
cmsg_broadcast_client_get_loopback_client (cmsg_client *client)
{
    cmsg_broadcast_client *broadcast_client = (cmsg_broadcast_client *) client;

    CMSG_ASSERT_RETURN_VAL (broadcast_client != NULL, NULL);

    return broadcast_client->loopback_client;
}
