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

/* Define the size of the sun_path field in struct sockaddr_un. This structure is very
 * old and there is no API define for the size of the sun_path array */
#define SUN_PATH_SIZE   108


//forward delarations
typedef struct _cmsg_client_s cmsg_client;
typedef struct _cmsg_server_s cmsg_server;


#ifdef HAVE_VCSTACK
typedef struct _cpg_server_connection_s
{
    cpg_handle_t handle;
    cpg_callbacks_t callbacks;
    int fd; //file descriptor for listening
} cmsg_cpg_server_connection;
#endif

typedef struct _generic_server_connection_s
{
    int listening_socket;
    int client_socket;
} cmsg_generic_sever_connection;

typedef union _client_connection_u
{
#ifdef HAVE_VCSTACK
    cpg_handle_t handle;
#endif
    int socket;
} cmsg_client_connection;

typedef union _cmsg_server_connection_u
{
#ifdef HAVE_VCSTACK
    cmsg_cpg_server_connection cpg;
#endif
    cmsg_generic_sever_connection sockets;
} cmsg_server_connection;

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
typedef void (*cpg_configchg_cb_f) (cmsg_server *server,
                                    const struct cpg_address * member_list,
                                    int member_list_entries,
                                    const struct cpg_address * left_list,
                                    int left_list_entries,
                                    const struct cpg_address * joined_list,
                                    int joined_list_entries);
#endif

typedef struct _cmsg_cpg_s
{
#ifdef HAVE_VCSTACK
    struct cpg_name group_name; // CPG address structure
    cpg_configchg_cb_f configchg_cb;
#endif
} cmsg_cpg;

typedef int (*udt_connect_f) (cmsg_client *client);
typedef int (*udt_send_f) (void *udt_data, void *buff, int length, int flag);
typedef int (*cmsg_recv_func) (void *handle, void *buff, int len, int flags);

typedef struct _cmsg_udt_s
{
    void *udt_data;
    // Functions for userdefined transport functionality
    udt_connect_f connect;
    udt_send_f send;
    cmsg_recv_func recv;
} cmsg_udt;

typedef union _cmsg_transport_config_u
{
    cmsg_socket socket;
    cmsg_cpg cpg;
    cmsg_udt udt;
    ProtobufCService *lpb_service;
} cmsg_transport_config;


typedef enum _cmsg_transport_type_e
{
    CMSG_TRANSPORT_RPC_LOCAL,
    CMSG_TRANSPORT_RPC_TCP,
    CMSG_TRANSPORT_RPC_TIPC,
    CMSG_TRANSPORT_ONEWAY_TCP,
    CMSG_TRANSPORT_ONEWAY_TIPC,
    CMSG_TRANSPORT_CPG,
    CMSG_TRANSPORT_ONEWAY_USERDEFINED,
    CMSG_TRANSPORT_BROADCAST,
    CMSG_TRANSPORT_LOOPBACK_ONEWAY,
    CMSG_TRANSPORT_RPC_UNIX,
    CMSG_TRANSPORT_ONEWAY_UNIX,
} cmsg_transport_type;

typedef int (*client_conect_f) (cmsg_client *client);
typedef int (*server_listen_f) (cmsg_server *server);
typedef int (*server_recv_f) (int32_t socket, cmsg_server *server);
typedef int (*server_accept_f) (int32_t socket, cmsg_server *server);

typedef cmsg_status_code (*client_recv_f) (cmsg_client *client,
                                           ProtobufCMessage **messagePtPt);

typedef int (*client_send_f) (cmsg_client *client, void *buff, int length, int flag);
typedef int (*server_send_f) (cmsg_server *server, void *buff, int length, int flag);

typedef int32_t (*invoke_send_f) (cmsg_client *client, unsigned method_index,
                                  const ProtobufCMessage *input);
typedef int32_t (*invoke_recv_f) (cmsg_client *client, unsigned method_index,
                                  ProtobufCClosure closure, void *closure_data);

typedef void (*client_close_f) (cmsg_client *client);
typedef void (*server_close_f) (cmsg_server *server);
typedef int (*s_get_socket_f) (cmsg_server *server);
typedef int (*c_get_socket_f) (cmsg_client *client);
typedef void (*client_destroy_f) (cmsg_client *client);
typedef void (*server_destroy_f) (cmsg_server *server);
typedef uint32_t (*is_congested_f) (cmsg_client *client);

typedef struct _cmsg_transport_s cmsg_transport;    //forward declaration

typedef int32_t (*send_called_multi_threads_enable_f) (cmsg_transport *transport,
                                                       uint32_t enable);

typedef int32_t (*send_can_block_enable_f) (cmsg_transport *transport, uint32_t enable);

typedef int32_t (*ipfree_bind_enable_f) (cmsg_transport *transport, cmsg_bool_t enable);

typedef void (*cmsg_tipc_topology_callback) (struct tipc_event * event);

#define CMSG_MAX_TPORT_ID_LEN 64

struct _cmsg_transport_s
{
    //transport information
    cmsg_transport_type type;
    cmsg_transport_config config;
    char tport_id[CMSG_MAX_TPORT_ID_LEN + 1];

    // send features
    // lock - to allow send to be called from multiple threads
    uint32_t send_called_multi_enabled;
    pthread_mutex_t send_lock;

    // send to block if message cannot be sent
    uint32_t send_can_block;

    // sets IP_FREEBIND in socket options
    cmsg_bool_t use_ipfree_bind;

    //transport function pointers
    client_conect_f connect;                                                // client connect function
    server_listen_f listen;                                                 // server listen function
    server_accept_f server_accept;                                          // server accept
    server_recv_f server_recv;                                              // server receive function
    client_recv_f client_recv;                                              // receive function
    client_send_f client_send;                                              // client send function
    server_send_f server_send;                                              // server send function
    ProtobufCClosure closure;                                               // rpc closure function
    invoke_send_f invoke_send;                                              // invoke send function
    invoke_recv_f invoke_recv;                                              // invoke recv function
    client_close_f client_close;                                            // client close socket function
    server_close_f server_close;                                            // server close socket function
    s_get_socket_f s_socket;                                                //
    c_get_socket_f c_socket;                                                //
    server_destroy_f server_destroy;                                        // Server destroy function
    client_destroy_f client_destroy;                                        // Client destroy function
    is_congested_f is_congested;                                            // Check whether transport is congested
    send_called_multi_threads_enable_f send_called_multi_threads_enable;    // Sets whether the send functionality handles being called from multiple threads
    send_can_block_enable_f send_can_block_enable;
    ipfree_bind_enable_f ipfree_bind_enable;                                // Allows TCP socket to bind with a non-existent, non-local addr to avoid IPv6 DAD race condition
    //transport statistics
    uint32_t client_send_tries;
};

cmsg_transport *cmsg_transport_new (cmsg_transport_type type);
void cmsg_transport_tipc_init (cmsg_transport *transport);
void cmsg_transport_tcp_init (cmsg_transport *transport);
void cmsg_transport_oneway_tipc_init (cmsg_transport *transport);
void cmsg_transport_oneway_tcp_init (cmsg_transport *transport);
void cmsg_transport_oneway_cpumail_init (cmsg_transport *transport);
void cmsg_transport_oneway_loopback_init (cmsg_transport *transport);

#ifdef HAVE_VCSTACK
void cmsg_transport_cpg_init (cmsg_transport *transport);
void cmsg_transport_tipc_broadcast_init (cmsg_transport *transport);
#endif

int32_t cmsg_transport_server_process_message (cmsg_recv_func recv, void *handle,
                                               cmsg_server *server);

int32_t cmsg_transport_server_process_message_with_peek (cmsg_recv_func recv, void *handle,
                                                         cmsg_server *server);

int32_t cmsg_transport_destroy (cmsg_transport *transport);

int32_t cmsg_transport_send_called_multi_threads_enable (cmsg_transport *transport,
                                                         uint32_t enable);

int32_t cmsg_transport_send_can_block_enable (cmsg_transport *transport,
                                              uint32_t send_can_block);

int32_t cmsg_transport_ipfree_bind_enable (cmsg_transport *transport,
                                           cmsg_bool_t ipfree_bind_enable);

int32_t cmsg_transport_server_recv (cmsg_recv_func recv, void *handle, cmsg_server *server);

int32_t cmsg_transport_server_recv_with_peek (cmsg_recv_func recv, void *handle,
                                              cmsg_server *server);

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

void cmsg_transport_write_id (cmsg_transport *tport);

void cmsg_transport_rpc_unix_init (cmsg_transport *transport);
void cmsg_transport_oneway_unix_init (cmsg_transport *transport);
cmsg_transport *cmsg_create_transport_unix (const char *sun_path,
                                            cmsg_transport_type transport_type);
#endif
