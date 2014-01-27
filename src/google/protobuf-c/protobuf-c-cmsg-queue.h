#ifndef PROTOBUFCCMSGQUEUE_H
#define PROTOBUFCCMSGQUEUE_H

#include "protobuf-c-cmsg.h"
#include "protobuf-c-cmsg-client.h"
#include "protobuf-c-cmsg-server.h"
#include "protobuf-c-cmsg-sub.h"
#include "protobuf-c-cmsg-pub.h"
#include "protobuf-c-cmsg-transport.h"


typedef struct _cmsg_queue_filter_entry_s
{
    char method_name[128];
    cmsg_queue_filter_type type;
} cmsg_queue_filter_entry;

typedef struct _cmsg_send_queue_entry_s
{
    char method_name[128];
    cmsg_client *client;
    cmsg_transport *transport;

    uint32_t queue_buffer_size;
    uint8_t *queue_buffer;
} cmsg_send_queue_entry;

typedef struct _cmsg_receive_queue_entry_s
{
    uint32_t method_index;
    uint32_t queue_buffer_size;
    uint8_t *queue_buffer;
} cmsg_receive_queue_entry;


uint32_t cmsg_queue_get_length (GQueue *queue);


int32_t cmsg_send_client_process_all (cmsg_object obj);

int32_t cmsg_send_queue_push (GQueue *queue, uint8_t *buffer, uint32_t buffer_size,
                              cmsg_client *client, cmsg_transport *transport, char *method_name);


void cmsg_send_queue_free_all (GQueue *queue);

void cmsg_send_queue_free_all_by_transport (GQueue *queue, cmsg_transport *transport);
void cmsg_send_queue_free_by_transport_method (GQueue *queue, cmsg_transport *transport,
                                               char *method_name);

guint cmsg_queue_filter_hash_function (gconstpointer key);

gboolean cmsg_queue_filter_hash_equal_function (gconstpointer a, gconstpointer b);

void cmsg_queue_filter_set_all (GHashTable *queue_filter_hash_table,
                                const ProtobufCServiceDescriptor *descriptor,
                                cmsg_queue_filter_type filter_type);

void cmsg_queue_filter_clear_all (GHashTable *queue_filter_hash_table,
                                  const ProtobufCServiceDescriptor *descriptor);

int32_t cmsg_queue_filter_set (GHashTable *queue_filter_hash_table, const char *method,
                               cmsg_queue_filter_type filter_type);

int32_t cmsg_queue_filter_clear (GHashTable *queue_filter_hash_table, const char *method);

void cmsg_queue_filter_init (GHashTable *queue_filter_hash_table,
                             const ProtobufCServiceDescriptor *descriptor);


void cmsg_queue_filter_free (GHashTable *queue_filter_hash_table,
                             const ProtobufCServiceDescriptor *descriptor);

cmsg_queue_filter_type cmsg_queue_filter_lookup (GHashTable *queue_filter_hash_table,
                                                 const char *method);

void cmsg_queue_filter_show (GHashTable *queue_filter_hash_table,
                             const ProtobufCServiceDescriptor *descriptor);

cmsg_queue_state
cmsg_queue_filter_get_type (GHashTable *queue_filter_hash_table,
                            const ProtobufCServiceDescriptor *descriptor);

int32_t
cmsg_queue_filter_copy (GHashTable *src_queue_filter_hash_table,
                        GHashTable *dst_queue_filter_hash_table,
                        const ProtobufCServiceDescriptor *descriptor);

int32_t cmsg_send_queue_process_all (cmsg_object obj);
int32_t cmsg_receive_queue_process_one (GQueue *queue, pthread_mutex_t *queue_mutex,
                                        const ProtobufCServiceDescriptor *descriptor,
                                        cmsg_server *server);
int32_t cmsg_receive_queue_process_some (GQueue *queue, pthread_mutex_t *queue_mutex,
                                         const ProtobufCServiceDescriptor *descriptor,
                                         cmsg_server *server, uint32_t num_to_process);
int32_t cmsg_receive_queue_process_all (GQueue *queue, pthread_mutex_t *queue_mutex,
                                        const ProtobufCServiceDescriptor *descriptor,
                                        cmsg_server *server);
int32_t cmsg_receive_queue_push (GQueue *queue, uint8_t *buffer, uint32_t method_index);

void cmsg_receive_queue_free_all (GQueue *queue);
#endif // PROTOBUFCCMSGQUEUE_H
