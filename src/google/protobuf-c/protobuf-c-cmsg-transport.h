#ifndef __CMSG_TRANSPORT_H_
#define __CMSG_TRANSPORT_H_


#include <netinet/in.h>
#include <sys/socket.h>
#include <linux/tipc.h>
#include <sys/un.h>
#include <glib.h>

#ifdef HAVE_VCSTACK
#include <corosync/cpg.h>
#endif

#include "protobuf-c-cmsg.h"


//for types used in functions pointers below
typedef struct _cmsg_client_s           cmsg_client;
typedef struct _cmsg_server_s           cmsg_server;


typedef struct _cmsg_socket_address_s        cmsg_socket_address;
typedef struct _cmsg_udt_functions_s         cmsg_udt_functions;
typedef struct _cmsg_transport_arguments_s   cmsg_transport_arguments;
typedef enum   _cmsg_transport_type_e        cmsg_transport_type;
typedef struct _cmsg_transport_s             cmsg_transport;
typedef union  _client_connection_u          cmsg_client_connection;
typedef union  _server_connection_u          cmsg_server_connection;
typedef struct _cpg_server_connection_s      cmsg_cpg_server_connection;
typedef struct _generic_server_connection_s  cmsg_generic_sever_connection;


#ifdef HAVE_VCSTACK
struct _cpg_server_connection_s
{
  cpg_handle_t handle;
  cpg_callbacks_t callbacks;
  int fd;                     //file descriptor for listening
};
#endif

struct _generic_server_connection_s
{
  int listening_socket;
  int client_socket;
};

union _client_connection_u
{
#ifdef HAVE_VCSTACK
  cpg_handle_t handle;
#endif
  int socket;
};

union _server_connection_u
{
#ifdef HAVE_VCSTACK
  cmsg_cpg_server_connection cpg;
#endif
  cmsg_generic_sever_connection sockets;
};


union _cmsg_socket_address_u
{
  struct sockaddr        generic;     // Generic socket address. Used for determining Address Family.
  struct sockaddr_in     in;          // INET socket address, for TCP based transport.
  struct sockaddr_tipc   tipc;        // TIPC socket address, for TIPC based IPC transport.
  struct sockaddr_un     un;          // UNIX socket address, for Unix-domain socket transport.
#ifdef HAVE_VCSTACK
  struct cpg_name        group_name;  // CPG address structure
#endif
};

typedef int (*udt_connect_f)(cmsg_client *client);
typedef int (*udt_send_f)(void *udf_data,
                      void*   buff,
                      int     length,
                      int     flag);

struct _cmsg_udt_functions_s
{
    udt_connect_f connect;
    udt_send_f send;
};

enum _cmsg_transport_type_e
{
  CMSG_TRANSPORT_RPC_LOCAL,
  CMSG_TRANSPORT_RPC_TCP,
  CMSG_TRANSPORT_RPC_TIPC,
  CMSG_TRANSPORT_ONEWAY_TCP,
  CMSG_TRANSPORT_ONEWAY_TIPC,
  CMSG_TRANSPORT_CPG,
  CMSG_TRANSPORT_ONEWAY_USERDEFINED,
  CMSG_TRANSPORT_BROADCAST,
};

typedef int (*client_conect_f)(cmsg_client* client);
typedef int (*server_listen_f)(cmsg_server* server);
typedef int (*server_recv_f)(int32_t      socket,
                             cmsg_server* server);
typedef ProtobufCMessage* (*client_recv_f)(cmsg_client* client);
typedef int (*client_send_f)(cmsg_client *client,
                      void*   buff,
                      int     length,
                      int     flag);
typedef int (*server_send_f)(cmsg_server *server,
                      void*   buff,
                      int     length,
                      int     flag);
typedef void (*invoke_f)(ProtobufCService*       service,
                         unsigned                method_index,
                         const ProtobufCMessage* input,
                         ProtobufCClosure        closure,
                         void*                   closure_data);
typedef void (*client_close_f)(cmsg_client* client);
typedef void (*server_close_f)(cmsg_server* server);
typedef int  (*s_get_socket_f)(cmsg_server* server);
typedef int  (*c_get_socket_f)(cmsg_client* client);
typedef void (*server_destroy_f)(cmsg_server* server);


struct _cmsg_transport_s
{
  cmsg_transport_type type;
  int family;
  cmsg_socket_address sockaddr;
  cmsg_udt_functions udt_funcs; // Functions for userdefined transport functionality
  client_conect_f connect;    //client connect function
  server_listen_f listen;     //server listen function
  server_recv_f server_recv;  //server receive function
  client_recv_f client_recv;  //receive function
  client_send_f client_send;  //client send function
  server_send_f server_send;  //server send function
  ProtobufCClosure closure;   //rpc closure function
  invoke_f invoke;            //invoke function
  client_close_f client_close;//client close socket function
  server_close_f server_close;//server close socket function
  s_get_socket_f s_socket;    //
  c_get_socket_f c_socket;    //
  server_destroy_f server_destroy; //Server destroy function
};

cmsg_transport*
cmsg_transport_new (cmsg_transport_type type);
void
cmsg_transport_tipc_init(cmsg_transport* transport);
void
cmsg_transport_tcp_init(cmsg_transport* transport);
void
cmsg_transport_oneway_tipc_init(cmsg_transport* transport);
void
cmsg_transport_oneway_tcp_init(cmsg_transport* transport);
void
cmsg_transport_oneway_cpumail_init(cmsg_transport* transport);

#ifdef HAVE_VCSTACK
void
cmsg_transport_cpg_init (cmsg_transport* transport);
void
cmsg_transport_tipc_broadcast_init(cmsg_transport* transport);
#endif

int32_t
cmsg_transport_destroy (cmsg_transport* transport);

#endif
