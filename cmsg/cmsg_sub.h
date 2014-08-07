#ifndef __CMSG_SUB_H_
#define __CMSG_SUB_H_

#include "cmsg.h"
#include "cmsg_private.h" // to be removed when this file is split private/public
#include "cmsg_client.h"
#include "cmsg_server.h"
#include "cmsg_sub_service.pb-c.h"


typedef struct _cmsg_sub_s
{
    //this is a hack to get around a check when a client method is called
    //to not change the order of the first two
    const ProtobufCServiceDescriptor *descriptor;
    int32_t (*invoke) (ProtobufCService *service,
                       unsigned method_index,
                       const ProtobufCMessage *input,
                       ProtobufCClosure closure, void *closure_data);

    cmsg_server *pub_server;    //receiving messages

} cmsg_sub;


cmsg_sub *cmsg_sub_new (cmsg_transport *pub_server_transport,
                        ProtobufCService *pub_service);

void cmsg_sub_destroy (cmsg_sub *subscriber);

int cmsg_sub_get_server_socket (cmsg_sub *subscriber);

int32_t cmsg_sub_server_receive_poll (cmsg_sub *sub, int32_t timeout_ms,
                                      fd_set *master_fdset, int *fdmax);

int32_t cmsg_sub_server_receive (cmsg_sub *subscriber, int32_t server_socket);
int32_t cmsg_sub_server_accept (cmsg_sub *subscriber, int32_t listen_socket);
void cmsg_sub_server_accept_callback (cmsg_sub *subscriber, int32_t sock);

int32_t cmsg_sub_subscribe (cmsg_sub *subscriber,
                            cmsg_transport *sub_client_transport, char *method_name);

int32_t cmsg_sub_unsubscribe (cmsg_sub *subscriber,
                              cmsg_transport *sub_client_transport, char *method_name);

cmsg_sub *cmsg_create_subscriber_tipc_rpc (const char *server_name, int member_id,
                                           int scope, ProtobufCService *descriptor);

cmsg_sub *cmsg_create_subscriber_tipc_oneway (const char *server_name, int member_id,
                                              int scope, ProtobufCService *descriptor);

void cmsg_destroy_subscriber_and_transport (cmsg_sub *subscriber);

#endif
