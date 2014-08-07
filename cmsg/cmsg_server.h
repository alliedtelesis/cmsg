
#ifndef __CMSG_SERVER_H_
#define __CMSG_SERVER_H_


#include "cmsg.h"
#include "cmsg_private.h" // to be removed when this file is split private/public
#include "cmsg_transport.h"
#include "cmsg_queue.h"

#define CMSG_SERVICE(package,service)     ((ProtobufCService *)&package ## _ ## service ## _service)
#define CMSG_SERVICE_NOPACKAGE(service)   ((ProtobufCService *)&service ## _service)

typedef struct _cmsg_server_closure_data_s
{
    cmsg_server *server;
    /* Whether the server has decided to do something different with the method
     * call or has invoked the method.
     */
    cmsg_method_processing_reason method_processing_reason;
} cmsg_server_closure_data;

typedef int32_t (*server_message_processor_f) (cmsg_server *server, uint8_t *buffer_data);

typedef struct _cmsg_server_s
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
    pthread_mutex_t queueing_state_mutex;
    cmsg_queue_state queueing_state;
    cmsg_queue_state queueing_state_last;
    uint32_t queue_in_process;

    pthread_mutex_t queue_filter_mutex; //hash will be modified by different thread
    GHashTable *queue_filter_hash_table;
    uint32_t queue_working;

    GHashTable *method_name_hash_table;
    //thread signaling for queuing
    cmsg_bool_t queue_process_number;
    pthread_t self_thread_id;

    fd_set accepted_fdset;
    int accepted_fdmax;

    //counter information
    void *cntr_session;
    // counterd counters
    void *cntr_unknown_rpc;
    void *cntr_rpc;
    void *cntr_unknown_fields;
    void *cntr_messages_queued;
    void *cntr_messages_dropped;
    void *cntr_connections_accepted;
    void *cntr_connections_closed;
    void *cntr_errors;
    void *cntr_poll_errors;
    void *cntr_recv_errors;
    void *cntr_send_errors;
    void *cntr_pack_errors;
    void *cntr_memory_errors;
    void *cntr_protocol_errors;
    void *cntr_queue_errors;
#ifdef HAVE_CMSG_PROFILING
    cmsg_prof prof;
#endif
} cmsg_server;


typedef struct _cmsg_server_list_s
{
    GList *list;
    pthread_mutex_t server_mutex; // Used to protect list access.
} cmsg_server_list;

cmsg_server *cmsg_server_new (cmsg_transport *transport, ProtobufCService *service);

void cmsg_server_destroy (cmsg_server *server);

int cmsg_server_get_socket (cmsg_server *server);

int32_t cmsg_server_receive_poll (cmsg_server *server,
                                  int32_t timeout_ms, fd_set *master_fdset, int *fdmax);

int32_t cmsg_server_receive_poll_list (cmsg_server_list *server_list, int32_t timeout_ms);

int32_t cmsg_server_receive (cmsg_server *server, int32_t server_socket);

int32_t cmsg_server_accept (cmsg_server *server, int32_t listen_socket);

void cmsg_server_accept_callback (cmsg_server *server, int32_t sock);

void cmsg_server_invoke (cmsg_server *server,
                         uint32_t method_index,
                         ProtobufCMessage *message,
                         cmsg_method_processing_reason process_reason);

int32_t cmsg_server_message_processor (cmsg_server *server, uint8_t *buffer_data);

void cmsg_server_empty_method_reply_send (cmsg_server *server,
                                          cmsg_status_code status_code,
                                          uint32_t method_index);

void cmsg_server_closure_rpc (const ProtobufCMessage *message, void *closure_data);

void cmsg_server_closure_oneway (const ProtobufCMessage *message, void *closure_data);

int32_t cmsg_server_queue_process (cmsg_server *server);

int32_t cmsg_server_queue_process_some (cmsg_server *server, int32_t number_to_process);

int32_t cmsg_server_queue_process_list (GList *server_list);

void cmsg_server_drop_all (cmsg_server *server);

void cmsg_server_queue_enable (cmsg_server *server);

int32_t cmsg_server_queue_disable (cmsg_server *server);

uint32_t cmsg_server_queue_get_length (cmsg_server *server);

uint32_t cmsg_server_queue_max_length_get (cmsg_server *server);

int32_t cmsg_server_queue_request_process_one (cmsg_server *server);

int32_t cmsg_server_queue_request_process_some (cmsg_server *server,
                                                uint32_t num_to_process);

int32_t cmsg_server_queue_request_process_all (cmsg_server *server);

void cmsg_server_queue_filter_set_all (cmsg_server *server,
                                       cmsg_queue_filter_type filter_type);

void cmsg_server_queue_filter_clear_all (cmsg_server *server);

int32_t cmsg_server_queue_filter_set (cmsg_server *server,
                                      const char *method,
                                      cmsg_queue_filter_type filter_type);

int32_t cmsg_server_queue_filter_clear (cmsg_server *server, const char *method);

void cmsg_server_queue_filter_init (cmsg_server *server);

cmsg_queue_filter_type cmsg_server_queue_filter_lookup (cmsg_server *server,
                                                        const char *method);

void cmsg_server_queue_filter_show (cmsg_server *server);

cmsg_server *cmsg_create_server_tipc_rpc (const char *server_name, int member_id,
                                          int scope, ProtobufCService *descriptor);

cmsg_server *cmsg_create_server_tipc_oneway (const char *server_name, int member_id,
                                             int scope, ProtobufCService *descriptor);

cmsg_server *cmsg_create_server_loopback_oneway (ProtobufCService *service);

void cmsg_destroy_server_and_transport (cmsg_server *server);


cmsg_server_list *cmsg_server_list_new (void);

void cmsg_server_list_destroy (cmsg_server_list *server_list);

int cmsg_server_list_is_empty (cmsg_server_list *server_list);

void cmsg_server_list_add_server (cmsg_server_list *server_list, cmsg_server *server);

void cmsg_server_list_remove_server (cmsg_server_list *server_list, cmsg_server *server);

#endif
