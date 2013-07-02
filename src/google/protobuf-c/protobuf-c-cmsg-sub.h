#ifndef __CMSG_SUB_H_
#define __CMSG_SUB_H_

#include "protobuf-c-cmsg.h"
#include "protobuf-c-cmsg-client.h"
#include "protobuf-c-cmsg-server.h"
#include "protobuf-c-cmsg-sub-service.pb-c.h"


typedef struct _cmsg_sub_s   cmsg_sub;


struct _cmsg_sub_s
{
  //this is a hack to get around a check when a client method is called
  //to not change the order of the first two
  const ProtobufCServiceDescriptor* descriptor;
  void (*invoke)(ProtobufCService*       service,
                 unsigned                method_index,
                 const ProtobufCMessage* input,
                 ProtobufCClosure        closure,
                 void*                   closure_data);
  
  cmsg_server* pub_server;   //receiving messages
};


cmsg_sub*
cmsg_sub_new (cmsg_transport*   pub_server_transport,
              ProtobufCService* pub_service);

int32_t
cmsg_sub_destroy (cmsg_sub* subscriber);

int
cmsg_sub_get_server_socket (cmsg_sub* subscriber);

void
cmsg_sub_subscribe_response_handler (const Cmsg__SubEntryResponse* response,
                                     void*                         closure_data);

int32_t
cmsg_sub_server_receive (cmsg_sub* subscriber,
                         int32_t   server_socket);

int32_t
cmsg_sub_subscribe (cmsg_sub*       subscriber,
                    cmsg_transport* sub_client_transport,
                    char*           method_name);

int32_t
cmsg_sub_unsubscribe (cmsg_sub*       subscriber,
                      cmsg_transport* sub_client_transport,
                      char*           method_name);

#endif
