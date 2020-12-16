/*
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */

#include "cmsg_mesh.h"
#include "cmsg_broadcast_client.h"

/**
 * Create a mesh connection.
 *
 * @param service - The protobuf service for this connection.
 * @param service_entry_name - The name in the /etc/services file to get the TCP port number
 * @param my_node_addr - The IP address of this local node
 * @param type - The type of mesh connection to create.
 * @param oneway - Whether the connections are oneway or rpc.
 * @param event_handler - A function to run when a node joins/leaves the mesh connection (optional).
 *
 * @returns A pointer to the mesh connection structure on success, NULL otherwise.
 */
cmsg_mesh_conn *
cmsg_mesh_connection_init (ProtobufCService *service, const char *service_entry_name,
                           struct in_addr my_node_addr, cmsg_mesh_local_type type,
                           bool oneway, cmsg_broadcast_event_handler_t event_handler)
{
    cmsg_mesh_conn *mesh_info = NULL;
    cmsg_client *bcast_client = NULL;
    cmsg_client *loopback_client = NULL;
    cmsg_server *server = NULL;
    bool connect_to_self = (type == CMSG_MESH_LOCAL_TCP);
    bool create_loopback = (type == CMSG_MESH_LOCAL_LOOPBACK);
    int ret = -1;

    mesh_info = CMSG_CALLOC (1, sizeof (cmsg_mesh_conn));
    if (mesh_info == NULL)
    {
        return NULL;
    }

    bcast_client = cmsg_broadcast_client_new (service->descriptor,
                                              service_entry_name, my_node_addr,
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
            cmsg_broadcast_client_destroy (mesh_info->broadcast_client);
            CMSG_FREE (mesh_info);
            return NULL;
        }
    }

    if (oneway)
    {
        server = cmsg_create_server_tcp_ipv4_oneway (service_entry_name, &my_node_addr,
                                                     NULL, service);
    }
    else
    {
        server = cmsg_create_server_tcp_ipv4_rpc (service_entry_name, &my_node_addr, NULL,
                                                  service);
    }

    if (!server)
    {
        cmsg_broadcast_client_destroy (mesh_info->broadcast_client);
        CMSG_FREE (mesh_info);
        return NULL;
    }
    mesh_info->server = server;

    if (cmsg_server_accept_thread_init (mesh_info->server) != CMSG_RET_OK)
    {
        cmsg_destroy_server_and_transport (mesh_info->server);
        cmsg_broadcast_client_destroy (mesh_info->broadcast_client);
        CMSG_FREE (mesh_info);
        return NULL;
    }

    return mesh_info;
}

/**
 * Destroy a mesh connection.
 *
 * @param mesh_info - The pointer returned from the call to 'cmsg_mesh_connection_init'.
 */
void
cmsg_mesh_connection_destroy (cmsg_mesh_conn *mesh_info)
{
    if (mesh_info)
    {
        cmsg_destroy_server_and_transport (mesh_info->server);
        cmsg_broadcast_client_destroy (mesh_info->broadcast_client);
        CMSG_FREE (mesh_info);
    }
}
