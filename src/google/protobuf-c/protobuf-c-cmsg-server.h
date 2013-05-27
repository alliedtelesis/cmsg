#ifndef __CMSG_SERVER_H_
#define __CMSG_SERVER_H_


#include "protobuf-c-cmsg.h"
#include "protobuf-c-cmsg-transport.h"


typedef struct _cmsg_server_request_s   cmsg_server_request;
typedef struct _cmsg_server_s           cmsg_server;


struct _cmsg_server_request_s
{
  uint32_t message_length;
  uint32_t request_id;
  uint32_t method_index;
  int client_socket;
  int32_t closure_response;
};

struct _cmsg_server_s
{
  ProtobufCAllocator* allocator;
  ProtobufCService* service;
  cmsg_transport* transport;
  int listening_socket;
  int client_socket;
};



cmsg_server*
cmsg_server_new (cmsg_transport*     transport,
                 ProtobufCService*   service);

int32_t
cmsg_server_destroy (cmsg_server* server);

int
cmsg_server_get_socket (cmsg_server* server);

int32_t
cmsg_server_receive (cmsg_server* server,
                     int32_t      server_socket);

int32_t
cmsg_server_message_processor (cmsg_server*         server,
                               uint8_t*             buffer_data,
                               cmsg_server_request* server_request);

void
cmsg_server_closure_rpc (const ProtobufCMessage* message,
                         void*                   closure_data);

void
cmsg_server_closure_oneway (const ProtobufCMessage* message,
                            void*                   closure_data);

#endif
