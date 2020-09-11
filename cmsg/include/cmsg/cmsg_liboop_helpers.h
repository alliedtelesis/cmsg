/*
 * Copyright 2020, Allied Telesis Labs New Zealand, Ltd
 */
#ifndef __CMSG_LIBOOP_HELPERS_H_
#define __CMSG_LIBOOP_HELPERS_H_

#include <cmsg/cmsg.h>
#include <cmsg/cmsg_server.h>
#include <cmsg/cmsg_mesh.h>

cmsg_server *cmsg_liboop_unix_server_init (ProtobufCService *service);
void cmsg_liboop_server_destroy (cmsg_server *server);
cmsg_tipc_mesh_conn *cmsg_liboop_tipc_mesh_init (ProtobufCService *service,
                                                 const char *service_entry_name,
                                                 int this_node_id, int min_node_id,
                                                 int max_node_id, cmsg_mesh_local_type type,
                                                 bool oneway);
void cmsg_liboop_mesh_destroy (cmsg_tipc_mesh_conn *mesh);

#endif /* __CMSG_LIBOOP_HELPERS_H_ */
