#ifndef __CMSG_SERVER_H_
#define __CMSG_SERVER_H_


#include "protobuf-c-cmsg.h"


#define CMSG_SERVICE(package,service)   ((ProtobufCService *)&package ## _ ## service ## _service)

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

typedef int32_t (*server_message_processor_f) (cmsg_server *server,
                                               uint8_t     *buffer_data);

struct _cmsg_server_s
{
    ProtobufCAllocator *allocator;
    ProtobufCService *service;
    cmsg_transport *transport;
    cmsg_server_request *server_request;
    server_message_processor_f message_processor;

    cmsg_object self;
    cmsg_object parent;

    cmsg_server_connection connection;
};


cmsg_server *
cmsg_server_new (cmsg_transport   *transport,
                 ProtobufCService *service);

void
cmsg_server_destroy (cmsg_server *server);

int
cmsg_server_get_socket (cmsg_server *server);

int32_t
cmsg_server_receive_poll (cmsg_server *server,
                          int32_t timeout_ms);

int32_t
cmsg_server_receive (cmsg_server *server,
                     int32_t      server_socket);

int32_t
cmsg_server_message_processor (cmsg_server *server,
                               uint8_t     *buffer_data);

void cmsg_server_closure_rpc (const ProtobufCMessage *message,
                              void                   *closure_data);

void
cmsg_server_closure_oneway (const ProtobufCMessage *message,
                            void                   *closure_data);

#endif
