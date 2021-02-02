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

typedef struct _cmsg_transport_s cmsg_transport;    //forward declaration

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

typedef bool (*cmsg_forwarding_transport_send_f) (void *user_data, void *buff, int length);

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

struct _cmsg_transport_s
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

cmsg_transport *cmsg_transport_copy (const cmsg_transport *transport);

#endif /* __CMSG_TRANSPORT_H_ */
