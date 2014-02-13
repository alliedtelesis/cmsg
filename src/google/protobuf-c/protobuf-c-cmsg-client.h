#ifndef __CMSG_CLIENT_H_
#define __CMSG_CLIENT_H_

#include "protobuf-c-cmsg.h"
#include "protobuf-c-cmsg-queue.h"
#include "protobuf-c-cmsg-transport.h"


#define CMSG_DESCRIPTOR(package,service)    ((ProtobufCServiceDescriptor *)&package ## _ ## service ## _descriptor)
#define CMSG_DESCRIPTOR_NOPACKAGE(service)  ((ProtobufCServiceDescriptor *)&service ## _descriptor)

typedef enum _cmsg_client_state_e
{
    CMSG_CLIENT_STATE_INIT,         //after creating a new client
    CMSG_CLIENT_STATE_CONNECTED,    //after succesful connect
    CMSG_CLIENT_STATE_FAILED,       //after unsuccessful connect (todo: or unsuccessful send)
    CMSG_CLIENT_STATE_CLOSED,       //after successful send
    CMSG_CLIENT_STATE_QUEUED,       //after successful adding a packet to the queue
} cmsg_client_state;

typedef struct _cmsg_client_closure_data_s
{
    void *message;
    ProtobufCAllocator *allocator;
} cmsg_client_closure_data;

typedef struct _cmsg_client_s
{
    //this is a hack to get around a check when a client method is called
    //to not change the order of the first two
    const ProtobufCServiceDescriptor *descriptor;
    int32_t (*invoke) (ProtobufCService *service,
                       unsigned method_index,
                       const ProtobufCMessage *input,
                       ProtobufCClosure closure, void *closure_data);

    ProtobufCAllocator *allocator;
    ProtobufCService base_service;
    cmsg_transport *_transport;
    cmsg_client_state state;
    cmsg_client_connection connection;
    pthread_mutex_t connection_mutex;

    cmsg_object self;
    cmsg_object parent;

    int queue_enabled_from_parent;

    //queuing
    pthread_mutex_t queue_mutex;
    GQueue *queue;
    pthread_mutex_t queue_filter_mutex; //TODO make thread save -- hash will be modified by different thread
    GHashTable *queue_filter_hash_table;

    //thread signaling for queuing
    pthread_cond_t queue_process_cond;
    pthread_mutex_t queue_process_mutex;
    uint32_t queue_process_count;
    pthread_t self_thread_id;

#ifdef HAVE_CMSG_PROFILING
    cmsg_prof prof;
#endif
} cmsg_client;

cmsg_client *cmsg_client_new (cmsg_transport *transport,
                              const ProtobufCServiceDescriptor *descriptor);

void cmsg_client_destroy (cmsg_client *client);

int32_t cmsg_client_connect (cmsg_client *client);

cmsg_status_code cmsg_client_response_receive (cmsg_client *client,
                                               ProtobufCMessage **message);

int32_t cmsg_client_invoke_rpc (ProtobufCService *service,
                                unsigned method_index,
                                const ProtobufCMessage *input,
                                ProtobufCClosure closure, void *closure_data);

int32_t cmsg_client_invoke_oneway (ProtobufCService *service,
                                   unsigned method_index,
                                   const ProtobufCMessage *input,
                                   ProtobufCClosure closure, void *closure_data);

int32_t cmsg_client_send_echo_request (cmsg_client *client);

cmsg_status_code cmsg_client_recv_echo_reply (cmsg_client *client);

int32_t cmsg_client_get_socket (cmsg_client *client);

int32_t cmsg_client_transport_is_congested (cmsg_client *client);

//queue api
void cmsg_client_queue_enable (cmsg_client *client);

int32_t cmsg_client_queue_disable (cmsg_client *client);

uint32_t cmsg_client_queue_get_length (cmsg_client *client);

int32_t
cmsg_client_buffer_send_retry_once (cmsg_client *client, uint8_t *queue_buffer,
                                    uint32_t queue_buffer_size, const char *method_name);

int32_t
cmsg_client_buffer_send_retry (cmsg_client *client, uint8_t *queue_buffer,
                               uint32_t queue_buffer_size, int max_tries);

int32_t
cmsg_client_buffer_send (cmsg_client *client, uint8_t *buffer, uint32_t buffer_size);

int32_t cmsg_client_queue_process_all (cmsg_client *client);

//queue filter
void cmsg_client_queue_filter_set_all (cmsg_client *client,
                                       cmsg_queue_filter_type filter_type);

void cmsg_client_queue_filter_clear_all (cmsg_client *client);

int32_t cmsg_client_queue_filter_set (cmsg_client *client, const char *method,
                                      cmsg_queue_filter_type filter_type);

int32_t cmsg_client_queue_filter_clear (cmsg_client *client, const char *method);

void cmsg_client_queue_filter_init (cmsg_client *client);

cmsg_queue_filter_type cmsg_client_queue_filter_lookup (cmsg_client *client,
                                                        const char *method);

void cmsg_client_queue_filter_show (cmsg_client *client);

cmsg_client *cmsg_create_client_tipc_rpc (const char *server_name, int member_id,
                                          int scope,
                                          ProtobufCServiceDescriptor *descriptor);

cmsg_client *cmsg_create_client_tipc_oneway (const char *server_name, int member_id,
                                             int scope,
                                             ProtobufCServiceDescriptor *descriptor);

void cmsg_destroy_client_and_transport (cmsg_client *client);
#endif
