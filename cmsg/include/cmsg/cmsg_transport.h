/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#ifndef __CMSG_TRANSPORT_H_
#define __CMSG_TRANSPORT_H_


#include <netinet/in.h>
#include <sys/socket.h>
#include <linux/tipc.h>
#include <sys/un.h>

#include "cmsg.h"
#include "cmsg_private.h"   // to be removed when this file is split private/public

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

typedef struct _cmsg_transport_s cmsg_transport;    //forward declaration

typedef int (*udt_connect_f) (cmsg_transport *transport);
typedef int (*udt_send_f) (void *udt_data, void *buff, int length, int flag);
typedef int (*cmsg_recv_func) (cmsg_transport *transport, int sock, void *buff, int len,
                               int flags);
typedef int (*client_connect_f) (cmsg_transport *transport);
typedef int (*server_listen_f) (cmsg_transport *transport);
typedef int (*server_recv_f) (int socket, cmsg_transport *transport,
                              uint8_t **recv_buffer,
                              cmsg_header *processed_header, int *nbytes);
typedef int (*server_accept_f) (cmsg_transport *transport);
typedef cmsg_status_code (*client_recv_f) (cmsg_transport *transport,
                                           const ProtobufCServiceDescriptor *descriptor,
                                           ProtobufCMessage **messagePtPt);
typedef int (*client_send_f) (cmsg_transport *transport, void *buff, int length, int flag);
typedef int (*server_send_f) (int socket, cmsg_transport *transport, void *buff, int length,
                              int flag);
typedef void (*socket_close_f) (cmsg_transport *transport);
typedef int (*get_socket_f) (cmsg_transport *transport);
typedef bool (*is_congested_f) (cmsg_transport *transport);
typedef int32_t (*apply_send_timeout_f) (cmsg_transport *transport, int sockfd);
typedef int32_t (*apply_recv_timeout_f) (cmsg_transport *transport, int sockfd);
typedef int32_t (*ipfree_bind_enable_f) (cmsg_transport *transport, cmsg_bool_t enable);
typedef void (*destroy_f) (cmsg_transport *transport);

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
    socket_close_f socket_close;                // close socket function
    get_socket_f get_socket;                    // gets the socket used by the transport
    is_congested_f is_congested;                // Check whether transport is congested
    apply_send_timeout_f apply_send_timeout;
    apply_recv_timeout_f apply_recv_timeout;
    ipfree_bind_enable_f ipfree_bind_enable;    // Allows TCP socket to bind with a non-existent, non-local addr to avoid IPv6 DAD race condition
    destroy_f destroy;                          // Called when the transport is to be destroyed
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
} cmsg_transport_config;


typedef enum _cmsg_transport_type_e
{
    CMSG_TRANSPORT_LOOPBACK,
    CMSG_TRANSPORT_RPC_TCP,
    CMSG_TRANSPORT_RPC_TIPC,
    CMSG_TRANSPORT_ONEWAY_TCP,
    CMSG_TRANSPORT_ONEWAY_TIPC,
    CMSG_TRANSPORT_ONEWAY_USERDEFINED,
    CMSG_TRANSPORT_RPC_USERDEFINED,
    CMSG_TRANSPORT_BROADCAST,
    CMSG_TRANSPORT_RPC_UNIX,
    CMSG_TRANSPORT_ONEWAY_UNIX,
} cmsg_transport_type;

typedef void (*cmsg_tipc_topology_callback) (struct tipc_event *event, void *user_cb_data);

#define CMSG_MAX_TPORT_ID_LEN 128

struct _cmsg_transport_s
{
    //transport information
    cmsg_transport_type type;
    cmsg_transport_config config;
    cmsg_udt_info udt_info;
    char tport_id[CMSG_MAX_TPORT_ID_LEN + 1];

    // send timeout in seconds
    uint32_t send_timeout;

    // receive timeout in seconds
    uint32_t receive_timeout;

    // connect timeout in seconds
    uint32_t connect_timeout;

    // maximum time to wait peeking for a received header
    uint32_t receive_peek_timeout;

    // sets IP_FREEBIND in socket options
    cmsg_bool_t use_ipfree_bind;

    // flag to tell error-level log to be suppressed to debug-level
    cmsg_bool_t suppress_errors;

    // The socket used by the transport
    int socket;

    //transport function pointers
    cmsg_tport_functions tport_funcs;

    //For debug purposes, store the object id of the parent (client/server) using this transport
    char parent_obj_id[CMSG_MAX_OBJ_ID_LEN + 1];
};

cmsg_transport *cmsg_transport_new (cmsg_transport_type type);

void cmsg_transport_destroy (cmsg_transport *transport);

int32_t cmsg_transport_ipfree_bind_enable (cmsg_transport *transport,
                                           cmsg_bool_t ipfree_bind_enable);

int32_t cmsg_transport_server_recv (int32_t server_socket, cmsg_transport *transport,
                                    uint8_t **recv_buffer, cmsg_header *processed_header,
                                    int *nbytes);
int32_t cmsg_transport_rpc_server_send (int socket, cmsg_transport *transport, void *buff,
                                        int length, int flag);
int32_t cmsg_transport_oneway_server_send (int socket, cmsg_transport *transport,
                                           void *buff, int length, int flag);
int cmsg_transport_get_socket (cmsg_transport *transport);
void cmsg_transport_socket_close (cmsg_transport *transport);

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

int cmsg_tipc_topology_subscription_read (int sock, void *user_cb_data);

void cmsg_tipc_topology_tracelog_tipc_event (const char *tracelog_string,
                                             const char *event_str,
                                             struct tipc_event *event);

void cmsg_transport_write_id (cmsg_transport *tport, const char *parent_obj_id);

cmsg_transport *cmsg_create_transport_unix (const ProtobufCServiceDescriptor *descriptor,
                                            cmsg_transport_type transport_type);
cmsg_transport *cmsg_create_transport_tcp (cmsg_socket *config,
                                           cmsg_transport_type transport_type);

char *cmsg_transport_unix_sun_path (const ProtobufCServiceDescriptor *descriptor);
void cmsg_transport_unix_sun_path_free (char *sun_path);

const char *cmsg_transport_counter_app_tport_id (cmsg_transport *transport);

void cmsg_transport_udt_tcp_base_init (cmsg_transport *transport, bool oneway);

bool cmsg_transport_compare (const cmsg_transport *one, const cmsg_transport *two);

cmsg_transport *cmsg_create_transport_tcp_ipv4 (const char *service_name,
                                                struct in_addr *addr, bool oneway);

cmsg_transport *cmsg_transport_copy (const cmsg_transport *transport);

#endif /* __CMSG_TRANSPORT_H_ */
