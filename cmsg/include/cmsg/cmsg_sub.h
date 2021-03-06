/**
 * cmsg_sub.h
 *
 * Header file for the CMSG subscriber functionality.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_SUB_H_
#define __CMSG_SUB_H_

#include <cmsg/cmsg.h>
#include <netinet/in.h>
#include <cmsg/cmsg_server.h>

typedef struct cmsg_subscriber cmsg_subscriber;

cmsg_server *cmsg_sub_unix_server_get (cmsg_subscriber *subscriber);
int cmsg_sub_unix_server_socket_get (cmsg_subscriber *subscriber);
cmsg_server *cmsg_sub_tcp_server_get (cmsg_subscriber *subscriber);
int cmsg_sub_tcp_server_socket_get (cmsg_subscriber *subscriber);

int32_t cmsg_sub_subscribe_local (cmsg_subscriber *subscriber, const char *method_name);
int32_t cmsg_sub_subscribe_remote (cmsg_subscriber *subscriber, const char *method_name,
                                   struct in_addr remote_addr);
int32_t cmsg_sub_subscribe_events_local (cmsg_subscriber *subscriber, const char **events);
int32_t cmsg_sub_subscribe_events_remote (cmsg_subscriber *subscriber, const char **events,
                                          struct in_addr remote_addr);
int32_t cmsg_sub_unsubscribe_local (cmsg_subscriber *subscriber, const char *method_name);
int32_t cmsg_sub_unsubscribe_remote (cmsg_subscriber *subscriber, const char *method_name,
                                     struct in_addr remote_addr);
int32_t cmsg_sub_unsubscribe_events_local (cmsg_subscriber *subscriber,
                                           const char **events);
int32_t cmsg_sub_unsubscribe_events_remote (cmsg_subscriber *subscriber,
                                            const char **events,
                                            struct in_addr remote_addr);
cmsg_subscriber *cmsg_subscriber_create_tcp (const char *server_name, struct in_addr addr,
                                             const char *vrf_bind_dev,
                                             const ProtobufCService *service);
cmsg_subscriber *cmsg_subscriber_create_unix (const ProtobufCService *service);
void cmsg_subscriber_destroy (cmsg_subscriber *subscriber);

#endif /* __CMSG_SUB_H_ */
