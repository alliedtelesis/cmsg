/*
 * Copyright 2020, Allied Telesis Labs New Zealand, Ltd
 */
#ifndef __CMSG_LIBOOP_HELPERS_H_
#define __CMSG_LIBOOP_HELPERS_H_

#include <cmsg/cmsg.h>
#include <cmsg/cmsg_server.h>
#include <cmsg/cmsg_mesh.h>
#include <cmsg/cmsg_sub.h>
#include <cmsg/cmsg_sl.h>

cmsg_server *cmsg_liboop_unix_server_init (ProtobufCService *service);
void cmsg_liboop_server_processing_stop (cmsg_server *server);
void cmsg_liboop_server_destroy (cmsg_server *server);
cmsg_tipc_mesh_conn *cmsg_liboop_tipc_mesh_init (ProtobufCService *service,
                                                 const char *service_entry_name,
                                                 int this_node_id, int min_node_id,
                                                 int max_node_id, cmsg_mesh_local_type type,
                                                 bool oneway);
void cmsg_liboop_mesh_destroy (cmsg_tipc_mesh_conn *mesh);
cmsg_subscriber *cmsg_liboop_unix_subscriber_init (ProtobufCService *service,
                                                   const char **events);
void cmsg_liboop_unix_subscriber_destroy (cmsg_subscriber *subscriber);
cmsg_server *cmsg_liboop_tcp_rpc_server_init (const char *server_name, struct in_addr *addr,
                                              ProtobufCService *service);
cmsg_server *cmsg_liboop_tipc_oneway_server_init (const char *server_name, int member_id,
                                                  int scope, ProtobufCService *service);
const cmsg_sl_info *cmsg_liboop_service_listener_listen (const char *service_name,
                                                         cmsg_sl_event_handler_t handler,
                                                         void *user_data);

#endif /* __CMSG_LIBOOP_HELPERS_H_ */
