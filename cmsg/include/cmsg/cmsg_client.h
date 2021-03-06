/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#ifndef __CMSG_CLIENT_H_
#define __CMSG_CLIENT_H_

typedef struct _cmsg_client_s cmsg_client;

#include "cmsg.h"
#include "cmsg_private.h"   // to be removed when this file is split private/public
#include "cmsg_queue.h"
#include "cmsg_transport.h"
#include "cmsg_crypto.h"


#define CMSG_DESCRIPTOR(package,service)    (&package ## _ ## service ## _descriptor)
#define CMSG_DESCRIPTOR_NOPACKAGE(service)  (&service ## _descriptor)

// Maximum stack nodes possible
#define CMSG_MAX_CLIENTS 24

// allow room for a NULL array entry at the end
#define CMSG_RECV_ARRAY_SIZE (CMSG_MAX_CLIENTS + 1)

typedef enum _cmsg_client_state_e
{
    CMSG_CLIENT_STATE_INIT,         //after creating a new client
    CMSG_CLIENT_STATE_CONNECTED,    //after successful connect
    CMSG_CLIENT_STATE_FAILED,       //after unsuccessful connect (todo: or unsuccessful send)
    CMSG_CLIENT_STATE_CLOSED,       //after successful send
    CMSG_CLIENT_STATE_QUEUED,       //after successful adding a packet to the queue
} cmsg_client_state;

typedef struct _cmsg_client_closure_data_s
{
    ProtobufCMessage *message;
    ProtobufCAllocator *allocator;
    int retval;
} cmsg_client_closure_data;

typedef int (*cmsg_queue_filter_func_t) (cmsg_client *, const char *,
                                         cmsg_queue_filter_type *);
typedef void (*cmsg_queue_callback_func_t) (cmsg_client *, const char *);

typedef struct _cmsg_client_s
{
    //this is a hack to get around a check when a client method is called
    //to not change the order of the first two
    const ProtobufCServiceDescriptor *descriptor;
    void (*invoke) (ProtobufCService *service,
                    uint32_t method_index,
                    const ProtobufCMessage *input,
                    ProtobufCClosure closure, void *closure_data);

    // pointers to the private functions used for invoke
    int32_t (*invoke_send) (cmsg_client *client, uint32_t method_index,
                            const ProtobufCMessage *input);

    int32_t (*invoke_recv) (cmsg_client *client, uint32_t method_index,
                            ProtobufCClosure closure,
                            cmsg_client_closure_data *closure_data);
    pthread_mutex_t invoke_mutex;

    void (*client_destroy) (cmsg_client *client);
    int32_t (*send_bytes) (cmsg_client *client, uint8_t *buffer, uint32_t buffer_len,
                           const char *method_name);

    ProtobufCService base_service;
    cmsg_transport *_transport;
    cmsg_client_state state;

    cmsg_object self;
    cmsg_object parent;

    cmsg_queue_filter_func_t queue_filter_func;
    cmsg_queue_callback_func_t queue_callback_func;

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

    // logging - whether to downgrade errors to debug
    cmsg_bool_t suppress_errors;

    // loopback server
    void *loopback_server;

    // mutex for safe client usage across multiple threads
    pthread_mutex_t send_mutex;

    /* SA data for encrypted connections */
    cmsg_crypto_sa *crypto_sa;
    crypto_sa_derive_func_t crypto_sa_derive_func;

    //counter information
    void *cntr_session;
    // counterd counters
    void *cntr_unknown_rpc;
    void *cntr_rpc;
    void *cntr_unknown_fields;
    void *cntr_messages_queued;
    void *cntr_messages_dropped;
    void *cntr_connect_attempts;
    void *cntr_connect_failures;
    void *cntr_errors;
    void *cntr_connection_errors;
    void *cntr_recv_errors;
    void *cntr_send_errors;
    void *cntr_pack_errors;
    void *cntr_memory_errors;
    void *cntr_protocol_errors;
    void *cntr_queue_errors;
} cmsg_client;

cmsg_client *cmsg_client_new (cmsg_transport *transport,
                              const ProtobufCServiceDescriptor *descriptor);

void cmsg_client_destroy (cmsg_client *client);

int32_t cmsg_client_connect (cmsg_client *client);

int cmsg_client_set_send_timeout (cmsg_client *client, uint32_t timeout);

int cmsg_client_set_receive_timeout (cmsg_client *client, uint32_t timeout);

int cmsg_client_set_connect_timeout (cmsg_client *client, uint32_t timeout);

cmsg_status_code cmsg_client_response_receive (cmsg_client *client,
                                               ProtobufCMessage **message);

int32_t cmsg_client_invoke_send (cmsg_client *client, uint32_t method_index,
                                 const ProtobufCMessage *input);

int32_t cmsg_client_invoke_recv (cmsg_client *client, uint32_t method_index,
                                 ProtobufCClosure closure,
                                 cmsg_client_closure_data *closure_data);

int32_t cmsg_client_invoke_send_direct (cmsg_client *client, uint32_t method_index,
                                        const ProtobufCMessage *input);

int32_t cmsg_client_send_echo_request (cmsg_client *client);

cmsg_status_code cmsg_client_recv_echo_reply (cmsg_client *client);

int32_t cmsg_client_get_socket (cmsg_client *client);

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

int32_t cmsg_client_queue_process_all (cmsg_client *client);

//queue filter : check "CMSG Library" wikipage for detailed API descriptions
void cmsg_client_queue_filter_set_all (cmsg_client *client,
                                       cmsg_queue_filter_type filter_type);

void cmsg_client_queue_filter_clear_all (cmsg_client *client);

int32_t cmsg_client_queue_filter_set (cmsg_client *client, const char *method,
                                      cmsg_queue_filter_type filter_type);

int32_t cmsg_client_queue_filter_clear (cmsg_client *client, const char *method);

void cmsg_client_msg_queue_filter_func_set (cmsg_client *client,
                                            cmsg_queue_filter_func_t func);

void cmsg_client_msg_queue_callback_func_set (cmsg_client *client,
                                              cmsg_queue_callback_func_t func);

void cmsg_client_queue_filter_init (cmsg_client *client);

cmsg_queue_filter_type cmsg_client_queue_filter_lookup (cmsg_client *client,
                                                        const char *method);

void cmsg_client_suppress_error (cmsg_client *client, cmsg_bool_t enable);

int32_t cmsg_client_create_packet (cmsg_client *client, const char *method_name,
                                   const ProtobufCMessage *input, uint8_t **buffer_ptr,
                                   uint32_t *total_message_size_ptr);

cmsg_client *cmsg_create_client_unix (const ProtobufCServiceDescriptor *descriptor);
cmsg_client *cmsg_create_client_unix_oneway (const ProtobufCServiceDescriptor *descriptor);
int32_t cmsg_client_unix_server_ready (const ProtobufCServiceDescriptor *descriptor);

cmsg_client *cmsg_create_client_loopback (ProtobufCService *service);

void cmsg_destroy_client_and_transport (cmsg_client *client);

cmsg_client *cmsg_create_client_tcp_ipv4_rpc (const char *service_name,
                                              struct in_addr *addr,
                                              const char *vrf_bind_dev,
                                              const ProtobufCServiceDescriptor *descriptor);
cmsg_client *cmsg_create_client_tcp_ipv4_oneway (const char *service_name,
                                                 struct in_addr *addr,
                                                 const char *vrf_bind_dev,
                                                 const ProtobufCServiceDescriptor
                                                 *descriptor);
cmsg_client *cmsg_create_client_tcp_ipv6_rpc (const char *service_name,
                                              struct in6_addr *addr,
                                              uint32_t scope_id, const char *vrf_bind_dev,
                                              const ProtobufCServiceDescriptor *descriptor);

cmsg_client *cmsg_create_client_tcp_ipv6_oneway (const char *service_name,
                                                 struct in6_addr *addr,
                                                 uint32_t scope_id,
                                                 const char *vrf_bind_dev,
                                                 const ProtobufCServiceDescriptor
                                                 *descriptor);

cmsg_client *cmsg_create_client_tipc_broadcast (const ProtobufCServiceDescriptor
                                                *descriptor, const char *service_name,
                                                int lower_addr, int upper_addr);
void cmsg_client_tipc_broadcast_set_destination (cmsg_client *client, int lower_addr,
                                                 int upper_addr);

cmsg_client *cmsg_create_client_forwarding (const ProtobufCServiceDescriptor *descriptor,
                                            void *user_data,
                                            cmsg_forwarding_transport_send_f send_func);
void cmsg_client_forwarding_data_set (cmsg_client *client, void *user_data);

typedef struct
{
    const char *filename;
    const char *msg;
    int return_code;
} service_support_parameters;

typedef struct
{
    const service_support_parameters *service_support;
    const char *response_filename;
} cmsg_method_client_extensions;

typedef struct
{
    const ProtobufCServiceDescriptor *service_desc;
    const cmsg_method_client_extensions **method_extensions;
} cmsg_api_descriptor;

int cmsg_api_invoke (cmsg_client *client, const cmsg_api_descriptor *cmsg_desc,
                     int method_index, const ProtobufCMessage *send_msg,
                     ProtobufCMessage **recv_msg);
#ifdef HAVE_UNITTEST
int cmsg_api_invoke_real (cmsg_client *client, const cmsg_api_descriptor *cmsg_desc,
                          int method_index,
                          const ProtobufCMessage *send_msg, ProtobufCMessage **recv_msg);
#endif /*HAVE_UNITTEST */

int32_t cmsg_client_crypto_enable (cmsg_client *client, cmsg_crypto_sa *sa,
                                   crypto_sa_derive_func_t derive_func);
bool cmsg_client_crypto_enabled (cmsg_client *client);

#endif /* __CMSG_CLIENT_H_ */
