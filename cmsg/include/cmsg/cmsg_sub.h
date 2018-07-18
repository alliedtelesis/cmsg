/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#ifndef __CMSG_SUB_H_
#define __CMSG_SUB_H_

#include "cmsg.h"
#include "cmsg_private.h"   // to be removed when this file is split private/public
#include "cmsg_client.h"
#include "cmsg_server.h"

typedef struct _cmsg_sub_s
{
    //this is a hack to get around a check when a client method is called
    //to not change the order of the first two
    const ProtobufCServiceDescriptor *descriptor;
    int32_t (*invoke) (ProtobufCService *service,
                       uint32_t method_index,
                       const ProtobufCMessage *input,
                       ProtobufCClosure closure, void *closure_data);

    cmsg_server *pub_server;    //receiving messages

} cmsg_sub;


cmsg_sub *cmsg_sub_new (cmsg_transport *pub_server_transport,
                        const ProtobufCService *pub_service);

int cmsg_sub_get_server_socket (cmsg_sub *subscriber);

int32_t cmsg_sub_server_receive_poll (cmsg_sub *sub, int32_t timeout_ms,
                                      fd_set *master_fdset, int *fdmax);

int32_t cmsg_sub_server_receive (cmsg_sub *subscriber, int32_t server_socket);
int32_t cmsg_sub_server_accept (cmsg_sub *subscriber, int32_t listen_socket);
void cmsg_sub_server_accept_callback (cmsg_sub *subscriber, int32_t sock);

int32_t cmsg_sub_subscribe (cmsg_sub *subscriber,
                            cmsg_transport *sub_client_transport, const char *method_name);
int32_t cmsg_sub_subscribe_events (cmsg_sub *subscriber,
                                   cmsg_transport *sub_client_transport,
                                   const char **events);
int32_t cmsg_sub_unsubscribe (cmsg_sub *subscriber,
                              cmsg_transport *sub_client_transport, char *method_name);
int32_t cmsg_sub_unsubscribe_events (cmsg_sub *subscriber,
                                     cmsg_transport *sub_client_transport,
                                     const char **events);

cmsg_sub *cmsg_create_subscriber_tipc_oneway (const char *server_name, int member_id,
                                              int scope, const ProtobufCService *service);
cmsg_sub *cmsg_create_subscriber_unix_oneway (const ProtobufCService *service);
void cmsg_destroy_subscriber_and_transport (cmsg_sub *subscriber);

#endif /* __CMSG_SUB_H_ */
