/*
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */
#ifndef __CMSG_GLIB_HELPERS_H_
#define __CMSG_GLIB_HELPERS_H_

#include <cmsg/cmsg.h>
#include <cmsg/cmsg_server.h>
#include <cmsg/cmsg_mesh.h>
#include <cmsg/cmsg_sub.h>
#include <cmsg/cmsg_sl.h>

void cmsg_glib_server_processing_start (cmsg_server *server);
int32_t cmsg_glib_server_init (cmsg_server *server);
int32_t cmsg_glib_thread_server_init (cmsg_server *server, GMainContext *context);
cmsg_server *cmsg_glib_unix_server_init (ProtobufCService *service);
cmsg_server *cmsg_glib_unix_server_init_oneway (ProtobufCService *service);
cmsg_server *cmsg_glib_tcp_server_init_oneway (const char *service_name,
                                               struct in_addr *addr,
                                               ProtobufCService *service);
cmsg_server *cmsg_glib_tcp_ipv6_server_init_oneway (const char *service_name,
                                                    struct in6_addr *addr,
                                                    uint32_t scope_id, const char *bind_dev,
                                                    ProtobufCService *service);
cmsg_tipc_mesh_conn *cmsg_glib_tipc_mesh_init (ProtobufCService *service,
                                               const char *service_entry_name,
                                               int this_node_id, int min_node_id,
                                               int max_node_id, cmsg_mesh_local_type type,
                                               bool oneway);
cmsg_subscriber *cmsg_glib_unix_subscriber_init (ProtobufCService *service,
                                                 const char **events);
cmsg_subscriber *cmsg_glib_tcp_subscriber_init (const char *service_name,
                                                struct in_addr addr,
                                                const ProtobufCService *service);
void cmsg_glib_subscriber_deinit (cmsg_subscriber *sub);
void cmsg_glib_bcast_client_processing_start (cmsg_client *broadcast_client);
void cmsg_glib_service_listener_listen (const char *service_name,
                                        cmsg_sl_event_handler_t handler, void *user_data);
cmsg_server *cmsg_glib_tipc_rpc_server_init (const char *server_name,
                                             int member_id, int scope,
                                             ProtobufCService *service);


#endif /* __CMSG_GLIB_HELPERS_H_ */
