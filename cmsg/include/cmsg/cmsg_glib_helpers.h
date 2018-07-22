/*
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */
#ifndef __CMSG_GLIB_HELPERS_H_
#define __CMSG_GLIB_HELPERS_H_

#include "cmsg.h"
#include "cmsg_server.h"
#include "cmsg_mesh.h"
#include "cmsg_pub.h"
#include "cmsg_sub.h"

cmsg_server_accept_thread_info *cmsg_glib_server_init (cmsg_server *server);
cmsg_server_accept_thread_info *cmsg_glib_unix_server_init (ProtobufCService *service);
cmsg_tipc_mesh_conn *cmsg_glib_tipc_mesh_init (ProtobufCService *service,
                                               const char *service_entry_name,
                                               int this_node_id, int min_node_id,
                                               int max_node_id,
                                               cmsg_mesh_local_type type, bool oneway);
cmsg_pub *cmsg_glib_tipc_publisher_init (const char *service_entry_name, int this_node_id,
                                         int scope,
                                         const ProtobufCServiceDescriptor *descriptor);
cmsg_pub *cmsg_glib_unix_publisher_init (const ProtobufCServiceDescriptor *descriptor);
cmsg_sub *cmsg_glib_unix_subscriber_init (ProtobufCService *service, const char **events);
void cmsg_glib_subscriber_deinit (cmsg_sub *sub);

#endif /* __CMSG_GLIB_HELPERS_H_ */
