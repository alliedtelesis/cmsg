#ifndef __CMSG_PUB_H_
#define __CMSG_PUB_H_

#include <glib.h>

#include "protobuf-c-cmsg.h"
#include "protobuf-c-cmsg-client.h"
#include "protobuf-c-cmsg-server.h"
#include "protobuf-c-cmsg-sub-service.pb-c.h"


typedef struct _cmsg_publisher_request_s cmsg_publisher_request;
typedef struct _cmsg_sub_entry_s         cmsg_sub_entry;
typedef struct _cmsg_pub_s               cmsg_pub;


struct _cmsg_publisher_request_s
{
    uint32_t message_length;
    uint32_t request_id;
    uint32_t method_index;
    int client_socket;
    int32_t closure_response;
    cmsg_pub *publisher;
};


struct _cmsg_sub_entry_s
{
    char method_name[128];
    cmsg_transport transport;
};


int32_t
cmsg_sub_entry_compare (cmsg_sub_entry *one,
                        cmsg_sub_entry *two);

struct _cmsg_pub_s
{
    //this is a hack to get around a check when a client method is called
    //to not change the order of the first two
    const ProtobufCServiceDescriptor      *descriptor;
    void (*invoke) (ProtobufCService       *service,
                    unsigned                method_index,
                    const ProtobufCMessage *input,
                    ProtobufCClosure        closure,
                    void                   *closure_data);

    cmsg_server *sub_server;  //registering subscriber
    const ProtobufCServiceDescriptor *registration_notify_client_service; //for calling notification methods in notify
    GList *subscriber_list;
    uint32_t subscriber_count;
    cmsg_publisher_request *publisher_request;


    int queue_timeouts;
    pthread_mutex_t queue_mutex;
    int queue_enabled;
    GQueue* queue;
    uint32_t queue_total_size;
};


cmsg_pub *
cmsg_pub_new (cmsg_transport                   *sub_server_transport,
              const ProtobufCServiceDescriptor *pub_service);

void
cmsg_pub_destroy (cmsg_pub **publisher);

int
cmsg_pub_get_server_socket (cmsg_pub *publisher);

int32_t
cmsg_pub_subscriber_add (cmsg_pub       *publisher,
                         cmsg_sub_entry *entry);

int32_t
cmsg_pub_subscriber_remove (cmsg_pub       *publisher,
                            cmsg_sub_entry *entry);
int32_t
cmsg_publisher_receive_poll (cmsg_pub *publisher,
                             int32_t timeout_ms);

int32_t
cmsg_pub_server_receive (cmsg_pub *publisher,
                         int32_t   server_socket);

int32_t
cmsg_pub_message_processor (cmsg_server *server,
                            uint8_t     *buffer_data);

void
cmsg_pub_invoke (ProtobufCService       *service,
                 unsigned                method_index,
                 const ProtobufCMessage *input,
                 ProtobufCClosure        closure,
                 void                   *closure_data);

//service implementation for handling register messages from the subscriber
int32_t
cmsg_pub_subscribe (Cmsg__SubService_Service      *service,
                    const Cmsg__SubEntry          *input,
                    Cmsg__SubEntryResponse_Closure closure,
                    void                          *closure_data);

//queueing api
int32_t
cmsg_pub_queue_process_one (cmsg_pub *publisher);

int32_t
cmsg_pub_queue_process_all (cmsg_pub *publisher);

int32_t
cmsg_pub_queue_enable (cmsg_pub *publisher);

int32_t
cmsg_pub_queue_disable (cmsg_pub *publisher);

#endif
