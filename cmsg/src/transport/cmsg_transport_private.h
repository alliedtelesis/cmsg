/*
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */
#ifndef __CMSG_TRANSPORT_PRIVATE_H_
#define __CMSG_TRANSPORT_PRIVATE_H_

#include "cmsg.h"
#include "cmsg_private.h"
#include "cmsg_types_auto.h"
#include "cmsg_transport.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <linux/tipc.h>
#include <sys/un.h>

/* The default connect timeout value in seconds */
#define CONNECT_TIMEOUT_DEFAULT 5

/* The default send timeout value in seconds */
#define SEND_TIMEOUT_DEFAULT 5

/* The default recv timeout value in seconds */
#define RECV_TIMEOUT_DEFAULT 5

/* The default timeout value for peeking for the header of a received message in seconds */
#define RECV_HEADER_PEEK_TIMEOUT_DEFAULT 10

/* For transport related errors */
#define CMSG_LOG_TRANSPORT_ERROR(transport, msg, ...) \
    do { \
        syslog ((transport->suppress_errors ? LOG_DEBUG : LOG_ERR) | LOG_LOCAL6, \
                "CMSG(%d).tport.%s%s: " msg, \
                __LINE__, transport->parent_obj_id, transport->tport_id, ## __VA_ARGS__); \
    } while (0)

struct cmsg_forwarding_server_data
{
    const uint8_t *msg;
    size_t len;
    size_t pos;
    void *user_data;
};

typedef enum _cmsg_peek_code
{
    CMSG_PEEK_CODE_SUCCESS,
    CMSG_PEEK_CODE_CONNECTION_CLOSED,
    CMSG_PEEK_CODE_CONNECTION_RESET,
    CMSG_PEEK_CODE_TIMEOUT,
} cmsg_peek_code;

#define CMSG_BIND_DEV_NAME_MAX    16

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
    char vrf_bind_dev[CMSG_BIND_DEV_NAME_MAX];  // For VRF support, the device to bind to the socket
    cmsg_socket_address sockaddr;
} cmsg_socket;

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
typedef int32_t (*apply_send_timeout_f) (cmsg_transport *transport, int sockfd);
typedef int32_t (*apply_recv_timeout_f) (cmsg_transport *transport, int sockfd);
typedef void (*destroy_f) (cmsg_transport *transport);

typedef struct _cmsg_tport_functions_s
{
    cmsg_recv_func recv_wrapper;
    client_connect_f connect;       // client connect function
    server_listen_f listen;         // server listen function
    server_accept_f server_accept;  // server accept
    server_recv_f server_recv;      // server receive function
    client_recv_f client_recv;      // receive function
    client_send_f client_send;      // client send function
    server_send_f server_send;      // server send function
    socket_close_f socket_close;    // close socket function
    get_socket_f get_socket;        // gets the socket used by the transport
    apply_send_timeout_f apply_send_timeout;
    apply_recv_timeout_f apply_recv_timeout;
    destroy_f destroy;              // Called when the transport is to be destroyed
} cmsg_tport_functions;

typedef union _cmsg_transport_config_u
{
    cmsg_socket socket;
} cmsg_transport_config;


typedef enum _cmsg_transport_type_e
{
    CMSG_TRANSPORT_LOOPBACK,
    CMSG_TRANSPORT_RPC_TCP,
    CMSG_TRANSPORT_ONEWAY_TCP,
    CMSG_TRANSPORT_BROADCAST,
    CMSG_TRANSPORT_RPC_UNIX,
    CMSG_TRANSPORT_ONEWAY_UNIX,
    CMSG_TRANSPORT_FORWARDING,
} cmsg_transport_type;

#define CMSG_MAX_TPORT_ID_LEN 128

struct cmsg_transport
{
    //transport information
    cmsg_transport_type type;
    cmsg_transport_config config;
    char tport_id[CMSG_MAX_TPORT_ID_LEN + 1];

    // send timeout in seconds
    uint32_t send_timeout;

    // receive timeout in seconds
    uint32_t receive_timeout;

    // connect timeout in seconds
    uint32_t connect_timeout;

    // maximum time to wait peeking for a received header
    uint32_t receive_peek_timeout;

    // flag to tell error-level log to be suppressed to debug-level
    cmsg_bool_t suppress_errors;

    // The socket used by the transport
    int socket;

    //transport function pointers
    cmsg_tport_functions tport_funcs;

    //For debug purposes, store the object id of the parent (client/server) using this transport
    char parent_obj_id[CMSG_MAX_OBJ_ID_LEN + 1];

    /* Application defined data to store on the transport */
    void *user_data;
};

void cmsg_transport_tcp_init (cmsg_transport *transport);
void cmsg_transport_oneway_tcp_init (cmsg_transport *transport);
void cmsg_transport_oneway_cpumail_init (cmsg_transport *transport);
void cmsg_transport_loopback_init (cmsg_transport *transport);
void cmsg_transport_tipc_broadcast_init (cmsg_transport *transport);
void cmsg_transport_rpc_unix_init (cmsg_transport *transport);
void cmsg_transport_oneway_unix_init (cmsg_transport *transport);
void cmsg_transport_forwarding_init (cmsg_transport *transport);

int connect_nb (int sockfd, const struct sockaddr *addr, socklen_t addrlen, int timeout);
ssize_t cmsg_transport_socket_send (int sockfd, const void *buf, size_t len, int flags);
ssize_t cmsg_transport_socket_recv (int sockfd, void *buf, size_t len, int flags);

cmsg_status_code cmsg_transport_client_recv (cmsg_transport *transport,
                                             const ProtobufCServiceDescriptor *descriptor,
                                             ProtobufCMessage **messagePtPt);

int32_t cmsg_transport_connect (cmsg_transport *transport);
int32_t cmsg_transport_accept (cmsg_transport *transport);
int32_t cmsg_transport_set_connect_timeout (cmsg_transport *transport, uint32_t timeout);
int32_t cmsg_transport_set_send_timeout (cmsg_transport *transport, uint32_t timeout);
int32_t cmsg_transport_set_recv_peek_timeout (cmsg_transport *transport, uint32_t timeout);
int32_t cmsg_transport_apply_send_timeout (cmsg_transport *transport, int sockfd);
int32_t cmsg_transport_apply_recv_timeout (cmsg_transport *transport, int sockfd);

cmsg_transport_info *cmsg_transport_info_create (const cmsg_transport *transport);
void cmsg_transport_info_free (cmsg_transport_info *transport_info);
cmsg_transport *cmsg_transport_info_to_transport (const cmsg_transport_info
                                                  *transport_info);
bool cmsg_transport_info_compare (const cmsg_transport_info *transport_info_a,
                                  const cmsg_transport_info *transport_info_b);
cmsg_transport_info *cmsg_transport_info_copy (const cmsg_transport_info *transport_info);
void cmsg_transport_tcp_cache_set (struct in_addr *address, bool present);

void cmsg_transport_forwarding_func_set (cmsg_transport *transport,
                                         cmsg_forwarding_transport_send_f send_func);
void cmsg_transport_forwarding_user_data_set (cmsg_transport *transport, void *user_data);
void *cmsg_transport_forwarding_user_data_get (cmsg_transport *transport);

cmsg_transport *cmsg_create_transport_unix (const ProtobufCServiceDescriptor *descriptor,
                                            cmsg_transport_type transport_type);

char *cmsg_transport_unix_sun_path (const ProtobufCServiceDescriptor *descriptor);
void cmsg_transport_unix_sun_path_free (char *sun_path);

int32_t cmsg_transport_server_recv (int32_t server_socket, cmsg_transport *transport,
                                    uint8_t **recv_buffer, cmsg_header *processed_header,
                                    int *nbytes);
int32_t cmsg_transport_rpc_server_send (int socket, cmsg_transport *transport, void *buff,
                                        int length, int flag);
int32_t cmsg_transport_oneway_server_send (int socket, cmsg_transport *transport,
                                           void *buff, int length, int flag);

cmsg_peek_code
cmsg_transport_peek_for_header (cmsg_recv_func recv_wrapper, cmsg_transport *transport,
                                int32_t socket, time_t seconds_to_wait,
                                void *header_received, int header_size);
cmsg_status_code cmsg_transport_peek_to_status_code (cmsg_peek_code peek_code);

bool cmsg_transport_compare (const cmsg_transport *one, const cmsg_transport *two);

cmsg_transport *cmsg_create_transport_tcp_ipv4 (const char *service_name,
                                                struct in_addr *addr,
                                                const char *vrf_bind_dev, bool oneway);
cmsg_transport *cmsg_create_transport_tcp_ipv6 (const char *service_name,
                                                struct in6_addr *addr, uint32_t scope_id,
                                                const char *vrf_bind_dev, bool oneway);

int cmsg_transport_get_socket (cmsg_transport *transport);
void cmsg_transport_socket_close (cmsg_transport *transport);

void cmsg_transport_write_id (cmsg_transport *tport, const char *parent_obj_id);

const char *cmsg_transport_counter_app_tport_id (cmsg_transport *transport);

cmsg_transport *cmsg_transport_new (cmsg_transport_type type);

void cmsg_transport_destroy (cmsg_transport *transport);

#endif /* __CMSG_TRANSPORT_PRIVATE_H_ */
