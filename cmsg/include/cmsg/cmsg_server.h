/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#ifndef __CMSG_SERVER_H_
#define __CMSG_SERVER_H_


#include "cmsg.h"
#include "cmsg_private.h"   // to be removed when this file is split private/public
#include "cmsg_transport.h"
#include "cmsg_queue.h"

#define CMSG_SERVICE(package,service)     ((ProtobufCService *)&package ## _ ## service ## _service)
#define CMSG_SERVICE_NOPACKAGE(service)   ((ProtobufCService *)&service ## _service)

typedef struct _cmsg_server_s cmsg_server;

typedef struct _cmsg_server_closure_info_s
{
    void *closure;
    void *closure_data;
} cmsg_server_closure_info;

typedef struct _cmsg_server_closure_data_s
{
    cmsg_server *server;
    cmsg_server_request *server_request;

    /* The socket to send the response on. */
    int reply_socket;

    /* Whether the server has decided to do something different with the method
     * call or has invoked the method. */
    cmsg_method_processing_reason method_processing_reason;
} cmsg_server_closure_data;

typedef void (*cmsg_closure_func) (const ProtobufCMessage *send_msg, void *closure_data);
typedef void (*cmsg_impl_func) (cmsg_server_closure_info *closure_info,
                                const ProtobufCMessage *input);
typedef void (*cmsg_impl_no_input_func) (cmsg_server_closure_info *closure_info);

void cmsg_server_call_impl (const ProtobufCMessage *input, cmsg_closure_func closure,
                            void *closure_data, cmsg_impl_func impl_func);
void cmsg_server_call_impl_no_input (cmsg_closure_func closure, void *closure_data,
                                     cmsg_impl_no_input_func impl_func);

typedef int32_t (*server_message_processor_f) (int socket,
                                               cmsg_server_request *server_request,
                                               cmsg_server *server, uint8_t *buffer_data);

typedef struct _cmsg_server_accept_thread_info
{
    /* Thread used to accept any incoming connection attempts. */
    pthread_t server_accept_thread;

    /* Queue to store new accepted connection sockets. This is used to
     * pass the new socket descriptors back to the server user. */
    GAsyncQueue *accept_sd_queue;

    /* An eventfd object to notify the server user that there is a new
     * socket descriptor on the accept_sd_queue.  */
    int accept_sd_eventfd;
} cmsg_server_accept_thread_info;

typedef struct _cmsg_server_s
{
    const ProtobufCService *service;
    cmsg_transport *_transport;
    server_message_processor_f message_processor;

    cmsg_object self;
    cmsg_object parent;

    // rpc closure function
    ProtobufCClosure closure;

    //queuing
    pthread_mutex_t queue_mutex;
    GQueue *queue;
    uint32_t maxQueueLength;
    pthread_mutex_t queueing_state_mutex;
    cmsg_queue_state queueing_state;
    cmsg_queue_state queueing_state_last;
    bool queue_in_process;

    pthread_mutex_t queue_filter_mutex; //hash will be modified by different thread
    GHashTable *queue_filter_hash_table;
    uint32_t queue_working;

    GHashTable *method_name_hash_table;
    //thread signaling for queuing
    cmsg_bool_t queue_process_number;
    pthread_t self_thread_id;

    fd_set accepted_fdset;
    int accepted_fdmax;

    // memory management
    // flag to tell the server whether or not the application wants to take ownership
    // of the current message, and therefore be responsible for freeing it.
    cmsg_bool_t app_owns_current_msg;   //set to false by default, and always reset to false
    //after processing of an impl has finished.
    // flag to tell the server whether or not the application wants to take ownership
    // of all received messages, and therefore be responsible for freeing them.
    cmsg_bool_t app_owns_all_msgs;      //set to false by default but can be changed so
    //that cmsg will NEVER free recv msgs for this
    //server

    // flag to tell error-level log to be suppressed to debug-level
    cmsg_bool_t suppress_errors;

    cmsg_server_accept_thread_info *accept_thread_info;

    void *event_loop_data;

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
} cmsg_server;

typedef struct _cmsg_server_list_s
{
    GList *list;
    pthread_mutex_t server_mutex;   // Used to protect list access.
} cmsg_server_list;

typedef struct _cmsg_server_thread_task_info_s
{
    cmsg_server *server;
    int timeout;
    bool running;
} cmsg_server_thread_task_info;

cmsg_server *cmsg_server_new (cmsg_transport *transport, const ProtobufCService *service);

void cmsg_server_destroy (cmsg_server *server);

int cmsg_server_get_socket (cmsg_server *server);

int32_t cmsg_server_thread_receive_poll (cmsg_server *server,
                                         int32_t timeout_ms, fd_set *master_fdset,
                                         int *fdmax);

int32_t cmsg_server_receive_poll_list (cmsg_server_list *server_list, int32_t timeout_ms);

int32_t cmsg_server_receive (cmsg_server *server, int32_t server_socket);

void cmsg_server_invoke (int socket, cmsg_server_request *server_request,
                         cmsg_server *server, ProtobufCMessage *message,
                         cmsg_method_processing_reason process_reason);

void cmsg_server_closure_rpc (const ProtobufCMessage *message, void *closure_data);

void cmsg_server_closure_oneway (const ProtobufCMessage *message, void *closure_data);

void cmsg_server_send_response (const ProtobufCMessage *message, const void *service);

int32_t cmsg_server_queue_process (cmsg_server *server);

int32_t cmsg_server_queue_process_some (cmsg_server *server, int32_t number_to_process);

int32_t cmsg_server_queue_process_all (cmsg_server *server);

void cmsg_server_drop_all (cmsg_server *server);

void cmsg_server_queue_enable (cmsg_server *server);

int32_t cmsg_server_queue_disable (cmsg_server *server);

uint32_t cmsg_server_queue_get_length (cmsg_server *server);

uint32_t cmsg_server_queue_max_length_get (cmsg_server *server);

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

cmsg_server *cmsg_create_server_tipc_rpc (const char *server_name, int member_id,
                                          int scope, ProtobufCService *descriptor);

cmsg_server *cmsg_create_server_tipc_oneway (const char *server_name, int member_id,
                                             int scope, ProtobufCService *descriptor);

cmsg_server *cmsg_create_server_unix_rpc (ProtobufCService *descriptor);

cmsg_server *cmsg_create_server_unix_oneway (ProtobufCService *descriptor);

void cmsg_destroy_server_and_transport (cmsg_server *server);


cmsg_server_list *cmsg_server_list_new (void);

void cmsg_server_list_destroy (cmsg_server_list *server_list);

bool cmsg_server_list_is_empty (cmsg_server_list *server_list);

void cmsg_server_list_add_server (cmsg_server_list *server_list, cmsg_server *server);

void cmsg_server_list_remove_server (cmsg_server_list *server_list, cmsg_server *server);

void cmsg_server_app_owns_current_msg_set (cmsg_server *server);

void cmsg_server_app_owns_all_msgs_set (cmsg_server *server, cmsg_bool_t app_is_owner);

cmsg_server *cmsg_create_server_tcp_rpc (cmsg_socket *config, ProtobufCService *descriptor);

void cmsg_server_invoke_direct (cmsg_server *server, const ProtobufCMessage *input,
                                uint32_t method_index);

int32_t cmsg_server_accept_thread_init (cmsg_server *server);
void cmsg_server_accept_thread_deinit (cmsg_server *server);

void cmsg_server_suppress_error (cmsg_server *server, cmsg_bool_t enable);

cmsg_server *cmsg_create_server_tcp_ipv4_rpc (const char *service_name,
                                              struct in_addr *addr,
                                              const char *vrf_bind_dev,
                                              const ProtobufCService *service);
cmsg_server *cmsg_create_server_tcp_ipv4_oneway (const char *service_name,
                                                 struct in_addr *addr,
                                                 const char *vrf_bind_dev,
                                                 const ProtobufCService *service);
cmsg_server *cmsg_create_server_tcp_ipv6_rpc (const char *service_name,
                                              struct in6_addr *addr,
                                              uint32_t scope_id, const char *vrf_bind_dev,
                                              const ProtobufCService *service);
cmsg_server *cmsg_create_server_tcp_ipv6_oneway (const char *service_name,
                                                 struct in6_addr *addr,
                                                 uint32_t scope_id,
                                                 const char *vrf_bind_dev,
                                                 const ProtobufCService *service);

const cmsg_server *cmsg_server_from_service_get (const void *service);

cmsg_server_thread_task_info *cmsg_server_thread_task_info_create (cmsg_server *server,
                                                                   int timeout);
void *cmsg_server_thread_task (void *_info);

#endif /* __CMSG_SERVER_H_ */
