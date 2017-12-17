/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#ifndef __CMSG_TRANSPORT_H_
#define __CMSG_TRANSPORT_H_


#include <netinet/in.h>
#include <sys/socket.h>
#include <linux/tipc.h>
#include <sys/un.h>

#ifdef HAVE_VCSTACK
#include <corosync/cpg.h>
#endif

#include "cmsg.h"
#include "cmsg_private.h"   // to be removed when this file is split private/public

/* Allow for encrypted data possibly requiring more buffer space than plain-text data
 * to pad the end of the last data block */
#define ENCRYPT_EXTRA   64

/* When connecting the transport specify that the default timeout value should
 * be used with the connect call */
#define CONNECT_TIMEOUT_DEFAULT -1

/* This value is used to limit the timeout for client message peek to 100s */
#define MAX_CLIENT_PEEK_LOOP (100)

/* This value is used to limit the timeout for server message peek to 10s */
#define MAX_SERVER_PEEK_LOOP (10)

#ifdef HAVE_VCSTACK
typedef struct _cpg_connection_s
{
    cpg_handle_t handle;
    cpg_callbacks_t callbacks;
    int fd; //file descriptor for listening
} cmsg_cpg_connection;
#endif

typedef struct _generic_connection_s
{
    int listening_socket;
    int client_socket;
} cmsg_generic_connection;

typedef union _cmsg_connection_u
{
#ifdef HAVE_VCSTACK
    cmsg_cpg_connection cpg;
#endif
    cmsg_generic_connection sockets;
} cmsg_connection;

typedef union _cmsg_socket_address_u
{
    struct sockaddr generic;    // Generic socket address. Used for determining Address Family.
    struct sockaddr_in in;      // INET socket address, for TCP based transport.
    struct sockaddr_tipc tipc;  // TIPC socket address, for TIPC based IPC transport.
    struct sockaddr_un un;      // UNIX socket address, for Unix-domain socket transport.
    struct sockaddr_in6 in6;    // INET6 socket address, for TCP based transport over IPv6.
} cmsg_socket_address;

typedef struct _cmsg_socket_s
{
    int family;
    cmsg_socket_address sockaddr;
} cmsg_socket;

#ifdef HAVE_VCSTACK
typedef void (*cpg_configchg_cb_f) (const struct cpg_address *member_list,
                                    int member_list_entries,
                                    const struct cpg_address *left_list,
                                    int left_list_entries,
                                    const struct cpg_address *joined_list,
                                    int joined_list_entries);
#endif

typedef struct _cmsg_cpg_s
{
#ifdef HAVE_VCSTACK
    struct cpg_name group_name; // CPG address structure
    cpg_configchg_cb_f configchg_cb;
#endif
} cmsg_cpg;

typedef struct _cmsg_transport_s cmsg_transport;    //forward declaration

typedef int (*udt_connect_f) (cmsg_transport *transport);
typedef int (*udt_send_f) (void *udt_data, void *buff, int length, int flag);
typedef int (*cmsg_recv_func) (cmsg_transport *transport, int sock, void *buff, int len,
                               int flags);
typedef int (*client_connect_f) (cmsg_transport *transport, int timeout);
typedef int (*server_listen_f) (cmsg_transport *transport);
typedef int (*server_recv_f) (int socket, cmsg_transport *transport,
                              uint8_t **recv_buffer,
                              cmsg_header *processed_header, int *nbytes);
typedef int (*server_accept_f) (int32_t socket, cmsg_transport *transport);
typedef cmsg_status_code (*client_recv_f) (cmsg_transport *transport,
                                           const ProtobufCServiceDescriptor *descriptor,
                                           ProtobufCMessage **messagePtPt);
typedef int (*client_send_f) (cmsg_transport *transport, void *buff, int length, int flag);
typedef int (*server_send_f) (cmsg_transport *transport, void *buff, int length, int flag);
typedef void (*client_close_f) (cmsg_transport *transport);
typedef void (*server_close_f) (cmsg_transport *transport);
typedef int (*s_get_socket_f) (cmsg_transport *transport);
typedef int (*c_get_socket_f) (cmsg_transport *transport);
typedef void (*client_destroy_f) (cmsg_transport *transport);
typedef void (*server_destroy_f) (cmsg_transport *transport);
typedef uint32_t (*is_congested_f) (cmsg_transport *transport);
typedef int32_t (*send_can_block_enable_f) (cmsg_transport *transport, uint32_t enable);
typedef int32_t (*ipfree_bind_enable_f) (cmsg_transport *transport, cmsg_bool_t enable);

typedef struct _cmsg_udt_s
{
    void *udt_data;
    // Functions for userdefined transport functionality
    udt_connect_f connect;
    udt_send_f send;
    cmsg_recv_func recv;
} cmsg_udt;

typedef struct _cmsg_tport_functions_s
{
    cmsg_recv_func recv_wrapper;
    client_connect_f connect;                   // client connect function
    server_listen_f listen;                     // server listen function
    server_accept_f server_accept;              // server accept
    server_recv_f server_recv;                  // server receive function
    client_recv_f client_recv;                  // receive function
    client_send_f client_send;                  // client send function
    server_send_f server_send;                  // server send function
    client_close_f client_close;                // client close socket function
    server_close_f server_close;                // server close socket function
    s_get_socket_f s_socket;                    //
    c_get_socket_f c_socket;                    //
    server_destroy_f server_destroy;            // Server destroy function
    client_destroy_f client_destroy;            // Client destroy function
    is_congested_f is_congested;                // Check whether transport is congested
    send_can_block_enable_f send_can_block_enable;
    ipfree_bind_enable_f ipfree_bind_enable;    // Allows TCP socket to bind with a non-existent, non-local addr to avoid IPv6 DAD race condition
} cmsg_tport_functions;

typedef struct _cmsg_udt_info_s
{
    // User-defined transport functions
    cmsg_tport_functions functions;

    // Base transport functions (i.e. allow access to
    // TCP, TIPC, UNIX, ..., transport functionality if required)
    cmsg_tport_functions base;

    // User-defined transport data. It is the responsibility of the
    // application using the UDT to correctly manage/free this memory.
    void *data;
} cmsg_udt_info;

typedef union _cmsg_transport_config_u
{
    cmsg_socket socket;
    cmsg_cpg cpg;
} cmsg_transport_config;


typedef enum _cmsg_transport_type_e
{
    CMSG_TRANSPORT_LOOPBACK,
    CMSG_TRANSPORT_RPC_TCP,
    CMSG_TRANSPORT_RPC_TIPC,
    CMSG_TRANSPORT_ONEWAY_TCP,
    CMSG_TRANSPORT_ONEWAY_TIPC,
    CMSG_TRANSPORT_CPG,
    CMSG_TRANSPORT_ONEWAY_USERDEFINED,
    CMSG_TRANSPORT_RPC_USERDEFINED,
    CMSG_TRANSPORT_BROADCAST,
    CMSG_TRANSPORT_RPC_UNIX,
    CMSG_TRANSPORT_ONEWAY_UNIX,
} cmsg_transport_type;

typedef void (*cmsg_tipc_topology_callback) (struct tipc_event *event);

#define CMSG_MAX_TPORT_ID_LEN 64

struct _cmsg_transport_s
{
    //transport information
    cmsg_transport_type type;
    cmsg_transport_config config;
    cmsg_udt_info udt_info;
    char tport_id[CMSG_MAX_TPORT_ID_LEN + 1];

    // send to block if message cannot be sent
    uint32_t send_can_block;

    // sets IP_FREEBIND in socket options
    cmsg_bool_t use_ipfree_bind;

    cmsg_connection connection;
    pthread_mutex_t connection_mutex;

    //transport function pointers
    cmsg_tport_functions tport_funcs;
    //transport statistics
    uint32_t client_send_tries;

    //For debug purposes, store the object id of the parent (client/server) using this transport
    char parent_obj_id[CMSG_MAX_OBJ_ID_LEN + 1];
};

cmsg_transport *cmsg_transport_new (cmsg_transport_type type);
void cmsg_transport_tipc_init (cmsg_transport *transport);
void cmsg_transport_tcp_init (cmsg_transport *transport);
void cmsg_transport_oneway_tipc_init (cmsg_transport *transport);
void cmsg_transport_oneway_tcp_init (cmsg_transport *transport);
void cmsg_transport_oneway_cpumail_init (cmsg_transport *transport);
void cmsg_transport_loopback_init (cmsg_transport *transport);

#ifdef HAVE_VCSTACK
void cmsg_transport_cpg_init (cmsg_transport *transport);
#endif /* HAVE_VCSTACK */

void cmsg_transport_tipc_broadcast_init (cmsg_transport *transport);

int32_t cmsg_transport_destroy (cmsg_transport *transport);

int32_t cmsg_transport_send_can_block_enable (cmsg_transport *transport,
                                              uint32_t send_can_block);

int32_t cmsg_transport_ipfree_bind_enable (cmsg_transport *transport,
                                           cmsg_bool_t ipfree_bind_enable);

int32_t cmsg_transport_server_recv (int32_t server_socket, cmsg_transport *transport,
                                    uint8_t **recv_buffer, cmsg_header *processed_header,
                                    int *nbytes);

cmsg_status_code cmsg_transport_client_recv (cmsg_recv_func recv_wrapper, int socket,
                                             cmsg_transport *transport,
                                             const ProtobufCServiceDescriptor *descriptor,
                                             ProtobufCMessage **messagePtPt);

cmsg_transport *cmsg_create_transport_tipc (const char *server_name, int member_id,
                                            int scope, cmsg_transport_type transport_type);

cmsg_transport *cmsg_create_transport_tipc_rpc (const char *server_name, int member_id,
                                                int scope);

cmsg_transport *cmsg_create_transport_tipc_oneway (const char *server_name, int member_id,
                                                   int scope);

int cmsg_tipc_topology_service_connect (void);

int
cmsg_tipc_topology_do_subscription (int sock, const char *server_name, uint32_t lower,
                                    uint32_t upper, cmsg_tipc_topology_callback callback);

int cmsg_tipc_topology_connect_subscribe (const char *server_name, uint32_t lower,
                                          uint32_t upper,
                                          cmsg_tipc_topology_callback callback);

int cmsg_tipc_topology_subscription_read (int sock);

void cmsg_tipc_topology_tracelog_tipc_event (const char *tracelog_string,
                                             const char *event_str,
                                             struct tipc_event *event);

void cmsg_transport_write_id (cmsg_transport *tport, const char *parent_obj_id);
void cmsg_transport_rpc_unix_init (cmsg_transport *transport);
void cmsg_transport_oneway_unix_init (cmsg_transport *transport);
cmsg_transport *cmsg_create_transport_unix (const ProtobufCServiceDescriptor *descriptor,
                                            cmsg_transport_type transport_type);
cmsg_transport *cmsg_create_transport_tcp (cmsg_socket *config,
                                           cmsg_transport_type transport_type);

char *cmsg_transport_unix_sun_path (const ProtobufCServiceDescriptor *descriptor);
void cmsg_transport_unix_sun_path_free (char *sun_path);

cmsg_status_code cmsg_transport_peek_for_header (cmsg_recv_func recv_wrapper,
                                                 cmsg_transport *transport, int32_t socket,
                                                 time_t seconds_to_wait,
                                                 cmsg_header *header_received);

const char *cmsg_transport_counter_app_tport_id (cmsg_transport *transport);

void cmsg_transport_udt_tcp_base_init (cmsg_transport *transport, bool oneway);

bool cmsg_transport_compare (cmsg_transport *one, cmsg_transport *two);

#endif /* __CMSG_TRANSPORT_H_ */
