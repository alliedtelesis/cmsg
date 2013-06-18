#ifndef __CMSG_TRANSPORT_H_
#define __CMSG_TRANSPORT_H_


#include <netinet/in.h>
#include <sys/socket.h>
#include <linux/tipc.h>
#include <sys/un.h>

#include "protobuf-c.h"
#include "protobuf-c-cmsg.h"

typedef union  _cmsg_socket_address_u   cmsg_socket_address;
typedef enum   _cmsg_transport_type_e   cmsg_transport_type;
typedef struct _cmsg_transport_s        cmsg_transport;


union _cmsg_socket_address_u
{
  struct sockaddr        generic;  // Generic socket address. Used for determining Address Family.
  struct sockaddr_in     in;       // INET socket address, for TCP based transport.
  struct sockaddr_tipc   tipc;     // TIPC socket address, for TIPC based IPC transport.
  struct sockaddr_un     un;       // UNIX socket address, for Unix-domain socket transport.
};

enum _cmsg_transport_type_e
{
  CMSG_TRANSPORT_RPC_LOCAL,
  CMSG_TRANSPORT_RPC_TCP,
  CMSG_TRANSPORT_RPC_TIPC,
  CMSG_TRANSPORT_ONEWAY_TCP,
  CMSG_TRANSPORT_ONEWAY_TIPC,
  CMSG_TRANSPORT_ONEWAY_CPG,
  CMSG_TRANSPORT_ONEWAY_CPUMAIL
};


typedef int (*client_conect_f)(void *client);
typedef int (*server_listen_f)(void *server);
typedef int (*server_recv_f)(int32_t socket, void  *server);
typedef int (*client_recv_f)(void *client);
typedef int (*send_f)(int32_t socket, void * buff, int length, int flag);
typedef void (*invoke_f)(ProtobufCService*       service,
    unsigned                method_index,
    const ProtobufCMessage* input,
    ProtobufCClosure        closure,
    void*                   closure_data);


struct _cmsg_transport_s
{
  cmsg_transport_type type;
  int family;
  cmsg_socket_address sockaddr;
  //todo: add cpg structures here
  client_conect_f connect;    //client connect function
  server_listen_f listen;     //server listen function
  server_recv_f server_recv;  //server receive function
  client_recv_f client_recv;  //receive function
  send_f send;                //send function
  ProtobufCClosure closure;   //rpc closure function
  invoke_f invoke;            //invoke function
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
cmsg_transport_oneway_cpg_init(cmsg_transport* transport);
void
cmsg_transport_oneway_cpumail_init(cmsg_transport* transport);


int32_t
cmsg_transport_destroy (cmsg_transport* transport);

#endif
