
#ifndef __CMSG_SERVER_H_
#define __CMSG_SERVER_H_


#include "protobuf-c-cmsg.h"

#define CMSG_SERVICE(package,service)   ((ProtobufCService *)&package ## _ ## service ## _service)

typedef enum _cmsg_queue_filter_type_e cmsg_queue_filter_type;

typedef struct _cmsg_server_request_s cmsg_server_request;
typedef struct _cmsg_server_s cmsg_server;
typedef struct _closure_data_s cmsg_closure_data;

struct _closure_data_s
{
    cmsg_server *server;
    /* Whether the server has decided to do something different with the method
     * call or has invoked the method.
     */
    cmsg_method_processing_reason method_processing_reason;
};

struct _cmsg_server_request_s
{
    uint32_t message_length;
    uint32_t method_index;
};

typedef int32_t (*server_message_processor_f) (cmsg_server *server, uint8_t *buffer_data);

struct _cmsg_server_s
{
    ProtobufCAllocator *allocator;
    ProtobufCService *service;
    cmsg_transport *_transport;
    cmsg_server_request *server_request;
    server_message_processor_f message_processor;

    cmsg_object self;
    cmsg_object parent;

    cmsg_server_connection connection;

    int queue_enabled_from_parent;

    //queuing
    pthread_mutex_t queue_mutex;
    GQueue *queue;
    uint32_t maxQueueLength;
    GHashTable *queue_filter_hash_table;

    //thread signaling for queuing
    pthread_cond_t queue_process_cond;
    pthread_mutex_t queue_process_mutex;
    pthread_t self_thread_id;

    fd_set accepted_fdset;
    int accepted_fdmax;
};


cmsg_server *cmsg_server_new (cmsg_transport *transport, ProtobufCService *service);

void cmsg_server_destroy (cmsg_server *server);

int cmsg_server_get_socket (cmsg_server *server);

int32_t cmsg_server_receive_poll (cmsg_server *server, int32_t timeout_ms,
                                  fd_set *master_fdset, int *fdmax);
int32_t cmsg_server_receive_poll_list (GList *server_list, int32_t timeout_ms);

int32_t cmsg_server_receive (cmsg_server *server, int32_t server_socket);
int32_t cmsg_server_accept (cmsg_server *server, int32_t listen_socket);

int32_t cmsg_server_message_processor (cmsg_server *server, uint8_t *buffer_data);

void cmsg_server_closure_rpc (const ProtobufCMessage *message, void *closure_data);

void cmsg_server_closure_oneway (const ProtobufCMessage *message, void *closure_data);

int32_t cmsg_server_queue_filter_set (cmsg_server *server,
                                      const char *method,
                                      cmsg_queue_filter_type filter_type);

void cmsg_server_queue_filter_set_all (cmsg_server *server,
                                       cmsg_queue_filter_type filter_type);

void cmsg_server_queue_filter_clear_all (cmsg_server *server);

int32_t cmsg_server_queue_process_all (cmsg_server *server);

uint32_t cmsg_server_queue_max_length_get (cmsg_server *server);

uint32_t cmsg_server_queue_current_length_get (cmsg_server *server);

void cmsg_server_invoke (cmsg_server *server, uint32_t method_index,
                         ProtobufCMessage *message,
                         cmsg_method_processing_reason process_reason);

#endif
