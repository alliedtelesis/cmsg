#ifndef __CMSG_PUB_H_
#define __CMSG_PUB_H_

#include "protobuf-c-cmsg.h"
#include "protobuf-c-cmsg-queue.h"
#include "protobuf-c-cmsg-client.h"
#include "protobuf-c-cmsg-server.h"
#include "protobuf-c-cmsg-sub-service.pb-c.h"


typedef struct _cmsg_sub_entry_s
{
    char method_name[128];
    cmsg_client *client;
    cmsg_transport *transport;
} cmsg_sub_entry;


int32_t cmsg_sub_entry_compare (cmsg_sub_entry *one, cmsg_sub_entry *two);

int32_t cmsg_sub_entry_compare_transport (cmsg_sub_entry *one, cmsg_transport *transport);

int32_t cmsg_transport_compare (cmsg_transport *one, cmsg_transport *two);

typedef struct _cmsg_pub_s
{
    //this is a hack to get around a check when a client method is called
    //to not change the order of the first two
    const ProtobufCServiceDescriptor *descriptor;
    int32_t (*invoke) (ProtobufCService *service,
                       unsigned method_index,
                       const ProtobufCMessage *input,
                       ProtobufCClosure closure, void *closure_data);

    cmsg_server *sub_server;                                                //registering subscriber
    const ProtobufCServiceDescriptor *registration_notify_client_service;   //for calling notification methods in notify
    GList *subscriber_list;
    pthread_mutex_t subscriber_list_mutex;
    uint32_t subscriber_count;

    cmsg_object self;
    cmsg_object parent;

    //queuing
    pthread_mutex_t queue_mutex;
    GQueue *queue;
    GHashTable *queue_filter_hash_table;
    cmsg_bool_t queue_enabled;

    //thread signaling for queuing
    pthread_cond_t queue_process_cond;
    pthread_mutex_t queue_process_mutex;
    uint32_t queue_process_count;
    pthread_t self_thread_id;
} cmsg_pub;


cmsg_pub *cmsg_pub_new (cmsg_transport *sub_server_transport,
                        const ProtobufCServiceDescriptor *pub_service);

void cmsg_pub_destroy (cmsg_pub *publisher);

int cmsg_pub_get_server_socket (cmsg_pub *publisher);

int32_t cmsg_pub_initiate_all_subscriber_connections (cmsg_pub *publisher);

void cmsg_pub_initiate_subscriber_connections (cmsg_pub *publisher,
                                               cmsg_transport *transport);

int32_t cmsg_pub_subscriber_add (cmsg_pub *publisher, cmsg_sub_entry *entry);

int32_t cmsg_pub_subscriber_remove (cmsg_pub *publisher, cmsg_sub_entry *entry);

int32_t cmsg_pub_subscriber_remove_all_with_transport (cmsg_pub *publisher,
                                                       cmsg_transport *transport);

int32_t cmsg_publisher_receive_poll (cmsg_pub *publisher,
                                     int32_t timeout_ms,
                                     fd_set *master_fdset,
                                     int *fdmax);

void cmsg_pub_subscriber_remove_all (cmsg_pub *publisher);

int32_t cmsg_pub_server_receive (cmsg_pub *publisher, int32_t server_socket);

int32_t cmsg_pub_server_accept (cmsg_pub *publisher, int32_t listen_socket);

int32_t cmsg_pub_message_processor (cmsg_server *server, uint8_t *buffer_data);

int32_t cmsg_pub_invoke (ProtobufCService *service,
                         unsigned method_index,
                         const ProtobufCMessage *input,
                         ProtobufCClosure closure, void *closure_data);

//service implementation for handling register messages from the subscriber
int32_t cmsg_pub_subscribe (cmsg_sub_service_Service *service,
                            const cmsg_sub_entry_transport_info_pbc *input,
                            cmsg_sub_entry_response_pbc_Closure closure, void *closure_data);

//queue api
void cmsg_pub_queue_enable (cmsg_pub *publisher);

int32_t cmsg_pub_queue_disable (cmsg_pub *publisher);

uint32_t cmsg_pub_queue_get_length (cmsg_pub *publisher);

int32_t cmsg_pub_queue_process_all (cmsg_pub *publisher);


//queue filter
void cmsg_pub_queue_filter_set_all (cmsg_pub *publisher,
                                    cmsg_queue_filter_type filter_type);

void cmsg_pub_queue_filter_clear_all (cmsg_pub *publisher);

int32_t cmsg_pub_queue_filter_set (cmsg_pub *publisher,
                                   const char *method,
                                   cmsg_queue_filter_type filter_type);

int32_t cmsg_pub_queue_filter_clear (cmsg_pub *publisher, const char *method);

void cmsg_pub_queue_filter_init (cmsg_pub *publisher);

cmsg_queue_filter_type cmsg_pub_queue_filter_lookup (cmsg_pub *publisher,
                                                     const char *method);

void cmsg_pub_queue_filter_show (cmsg_pub *publisher);

/**
 * Print the subscriber list of the publisher passed in.
 * This function is NOT thread-safe!!
 * If you want to print the subscriber list and you don't hold the lock on it,
 * use cmsg_pub_print_subscriber_list instead.
 */
void _cmsg_pub_print_subscriber_list (cmsg_pub *publisher);

/**
 * Print the subscriber list of the publisher passed in.
 * This function is thread-safe.
 * If you want to print the subscriber list and you hold the lock on it,
 * use _cmsg_pub_print_subscriber_list instead.
 */
void cmsg_pub_print_subscriber_list (cmsg_pub *publisher);

cmsg_pub *cmsg_create_publisher_tipc_rpc (const char *server_name, int member_id,
                                          int scope,
                                          ProtobufCServiceDescriptor *descriptor);

cmsg_pub *cmsg_create_publisher_tipc_oneway (const char *server_name, int member_id,
                                             int scope,
                                             ProtobufCServiceDescriptor *descriptor);

void cmsg_destroy_publisher_and_transport (cmsg_pub *publisher);


#endif
