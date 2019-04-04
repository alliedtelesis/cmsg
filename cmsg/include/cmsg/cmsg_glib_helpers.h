/*
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */
#ifndef __CMSG_GLIB_HELPERS_H_
#define __CMSG_GLIB_HELPERS_H_

#include "cmsg.h"
#include "cmsg_server.h"
#include "cmsg_mesh.h"
#include "cmsg_sub.h"

void cmsg_glib_server_processing_start (cmsg_server_accept_thread_info *info);
cmsg_server_accept_thread_info *cmsg_glib_server_init (cmsg_server *server);
cmsg_server_accept_thread_info *cmsg_glib_unix_server_init (ProtobufCService *service);
cmsg_tipc_mesh_conn *cmsg_glib_tipc_mesh_init (ProtobufCService *service,
                                               const char *service_entry_name,
                                               int this_node_id, int min_node_id,
                                               int max_node_id,
                                               cmsg_mesh_local_type type, bool oneway);
cmsg_subscriber *cmsg_glib_unix_subscriber_init (ProtobufCService *service,
                                                 const char **events);
void cmsg_glib_subscriber_deinit (cmsg_subscriber *sub);
void cmsg_glib_bcast_client_processing_start (cmsg_client *broadcast_client);

#endif /* __CMSG_GLIB_HELPERS_H_ */
