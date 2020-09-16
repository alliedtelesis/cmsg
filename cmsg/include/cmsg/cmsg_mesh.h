/*
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */
#ifndef __CMSG_MESH_H_
#define __CMSG_MESH_H_

#include "cmsg_client.h"
#include "cmsg_server.h"
#include "cmsg_broadcast_client.h"

typedef struct _cmsg_tipc_mesh_conn
{
    cmsg_server *server;
    cmsg_client *broadcast_client;
    cmsg_client *loopback_client;
} cmsg_tipc_mesh_conn;

/**
* Type of CMSG mesh connection to use.
*
* CMSG_MESH_LOCAL_NONE - Messages are not sent back to the sending node
* CMSG_MESH_LOCAL_LOOPBACK - Messages are sent back to the sending node via
*                            a loopback client (i.e. in the same thread that is sending).
* CMSG_MESH_LOCAL_TIPC - Messages are sent back to the sending node via a
*                        TIPC client. This assumes the required TIPC server is running
*                        in a separate thread to the one that is sending.
*/
typedef enum _cmsg_mesh_local_e
{
    CMSG_MESH_LOCAL_NONE,
    CMSG_MESH_LOCAL_LOOPBACK,
    CMSG_MESH_LOCAL_TIPC,
} cmsg_mesh_local_type;

cmsg_tipc_mesh_conn *cmsg_tipc_mesh_connection_init (ProtobufCService *service,
                                                     const char *service_entry_name,
                                                     uint32_t my_node_id,
                                                     uint32_t lower_node_id,
                                                     uint32_t upper_node_id,
                                                     cmsg_mesh_local_type type,
                                                     bool oneway,
                                                     cmsg_broadcast_event_handler_t
                                                     event_handler);
void cmsg_tipc_mesh_connection_destroy (cmsg_tipc_mesh_conn *mesh);

#endif /* __CMSG_MESH_H_ */
