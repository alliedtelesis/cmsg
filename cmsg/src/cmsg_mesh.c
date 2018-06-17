/*
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */

#include "cmsg_mesh.h"
#include "cmsg_broadcast_client.h"

cmsg_tipc_mesh_conn *
cmsg_tipc_mesh_connection_init (ProtobufCService *service, const char *service_entry_name,
                                uint32_t my_node_id, uint32_t lower_node_id,
                                uint32_t upper_node_id, cmsg_mesh_local_type type,
                                bool oneway, cmsg_broadcast_event_handler_t event_handler)
{
    cmsg_tipc_mesh_conn *mesh_info = NULL;
    cmsg_client *bcast_client = NULL;
    cmsg_client *loopback_client = NULL;
    cmsg_server *server = NULL;
    cmsg_server_accept_thread_info *server_thread_info = NULL;
    bool connect_to_self = (type == CMSG_MESH_LOCAL_TIPC);
    bool create_loopback = (type == CMSG_MESH_LOCAL_LOOPBACK);
    int ret = -1;

    mesh_info = CMSG_CALLOC (1, sizeof (cmsg_tipc_mesh_conn));
    if (mesh_info == NULL)
    {
        return NULL;
    }

    bcast_client = cmsg_broadcast_client_new (service->descriptor,
                                              service_entry_name, my_node_id,
                                              lower_node_id, upper_node_id,
                                              connect_to_self, oneway, event_handler);

    if (bcast_client == NULL)
    {
        CMSG_FREE (mesh_info);
        return NULL;
    }

    mesh_info->broadcast_client = bcast_client;

    if (create_loopback)
    {
        loopback_client = cmsg_create_client_loopback (service);
        if (loopback_client == NULL)
        {
            cmsg_broadcast_client_destroy (mesh_info->broadcast_client);
            CMSG_FREE (mesh_info);
            return NULL;
        }

        mesh_info->loopback_client = loopback_client;
        ret = cmsg_broadcast_client_add_loopback (mesh_info->broadcast_client,
                                                  loopback_client);
        if (ret != CMSG_RET_OK)
        {
            cmsg_destroy_client_and_transport (mesh_info->loopback_client);
            cmsg_broadcast_client_destroy (mesh_info->broadcast_client);
            CMSG_FREE (mesh_info);
            return NULL;
        }
    }

    server = cmsg_create_server_tipc_rpc (service_entry_name, my_node_id,
                                          TIPC_CLUSTER_SCOPE, service);

    if (!server)
    {
        cmsg_destroy_client_and_transport (mesh_info->loopback_client);
        cmsg_broadcast_client_destroy (mesh_info->broadcast_client);
        CMSG_FREE (mesh_info);
        return NULL;
    }
    mesh_info->server = server;

    server_thread_info = cmsg_server_accept_thread_init (mesh_info->server);
    if (server_thread_info == NULL)
    {
        cmsg_destroy_server_and_transport (mesh_info->server);
        cmsg_destroy_client_and_transport (mesh_info->loopback_client);
        cmsg_broadcast_client_destroy (mesh_info->broadcast_client);
        CMSG_FREE (mesh_info);
        return NULL;
    }
    mesh_info->server_thread_info = server_thread_info;

    return mesh_info;
}
