#ifndef __CMSG_CLIENT_H_
#define __CMSG_CLIENT_H_


#include "protobuf-c-cmsg.h"
#include "protobuf-c-cmsg-transport.h"

#define CMSG_DESCRIPTOR(package,service)  ((ProtobufCServiceDescriptor *)&package ## __ ## service ## __descriptor)

typedef enum   _cmsg_client_state_e     cmsg_client_state;
typedef struct _cmsg_client_s           cmsg_client;


enum _cmsg_client_state_e
{
  CMSG_CLIENT_STATE_INIT,
  CMSG_CLIENT_STATE_NAME_LOOKUP,
  CMSG_CLIENT_STATE_CONNECTING,
  CMSG_CLIENT_STATE_CONNECTED,
  CMSG_CLIENT_STATE_FAILED_WAITING,
  CMSG_CLIENT_STATE_FAILED,
  CMSG_CLIENT_STATE_DESTROYED
};

struct _cmsg_client_s
{
  //this is a hack to get around a check when a client method is called
  //to not change the order of the first two
  const ProtobufCServiceDescriptor* descriptor;
  void (*invoke)(ProtobufCService*       service,
                 unsigned                method_index,
                 const ProtobufCMessage* input,
                 ProtobufCClosure        closure,
                 void*                   closure_data);

  ProtobufCAllocator* allocator;
  ProtobufCService base_service;
  cmsg_transport* transport;
  int socket;
  uint32_t request_id;
  cmsg_client_state state;
};


cmsg_client*
cmsg_client_new (cmsg_transport*             transport,
                 const ProtobufCServiceDescriptor*  descriptor);

int32_t
cmsg_client_destroy(cmsg_client* client);

int32_t
cmsg_client_connect (cmsg_client *client);

ProtobufCMessage*
cmsg_client_response_receive (cmsg_client *client);

void
cmsg_client_invoke_rpc (ProtobufCService*       service,
                        unsigned                method_index,
                        const ProtobufCMessage* input,
                        ProtobufCClosure        closure,
                        void*                   closure_data);

void
cmsg_client_invoke_oneway (ProtobufCService*       service,
                           unsigned                method_index,
                           const ProtobufCMessage* input,
                           ProtobufCClosure        closure,
                           void*                   closure_data);

#endif
