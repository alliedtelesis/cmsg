#ifndef __CMSG_CLIENT_H_
#define __CMSG_CLIENT_H_

#include "protobuf-c-cmsg.h"
#include "protobuf-c-cmsg-transport.h"
#include "protobuf-c-cmsg-pub.h"

#define CMSG_DESCRIPTOR(package,service)  ((ProtobufCServiceDescriptor *)&package ## _ ## service ## _descriptor)

typedef enum   _cmsg_client_state_e     cmsg_client_state;
typedef struct _cmsg_client_s           cmsg_client;

//todo: queueing
typedef struct _cmsg_queue_entry_s       cmsg_queue_entry;

struct _cmsg_queue_entry_s
{
    cmsg_transport transport;
    uint32_t queue_buffer_size;
    uint8_t *queue_buffer;
};

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
    const ProtobufCServiceDescriptor *descriptor;
    void (*invoke) (ProtobufCService       *service,
                    unsigned                method_index,
                    const ProtobufCMessage *input,
                    ProtobufCClosure        closure,
                    void                   *closure_data);

    ProtobufCAllocator    *allocator;
    ProtobufCService       base_service;
    cmsg_transport        *transport;
    uint32_t               request_id;
    cmsg_client_state      state;
    cmsg_client_connection connection;

    cmsg_parent_type parent_type;
    void *parent;

    pthread_mutex_t queue_mutex;
    int queue_enabled;
    GQueue* queue;
    uint32_t queue_total_size;
};


cmsg_client *
cmsg_client_new (cmsg_transport                   *transport,
                 const ProtobufCServiceDescriptor *descriptor);

int32_t
cmsg_client_destroy (cmsg_client *client);

int32_t
cmsg_client_connect (cmsg_client *client);

ProtobufCMessage *
cmsg_client_response_receive (cmsg_client *client);

void
cmsg_client_invoke_rpc (ProtobufCService       *service,
                        unsigned                method_index,
                        const ProtobufCMessage *input,
                        ProtobufCClosure        closure,
                        void                   *closure_data);

void
cmsg_client_invoke_oneway (ProtobufCService       *service,
                           unsigned                method_index,
                           const ProtobufCMessage *input,
                           ProtobufCClosure        closure,
                           void                   *closure_data);

//queueing api
int32_t
cmsg_client_queue_process_one (cmsg_client *client);

int32_t
cmsg_client_queue_process_all (cmsg_client *client);

int32_t
cmsg_client_queue_enable (cmsg_client *client);

int32_t
cmsg_client_queue_disable (cmsg_client *client);

#endif
