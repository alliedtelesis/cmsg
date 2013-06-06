#ifndef __CMSG_TRANSPORT_H_
#define __CMSG_TRANSPORT_H_


#include <netinet/in.h>
#include <sys/socket.h>
#include <linux/tipc.h>
#include <sys/un.h>

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

struct _cmsg_transport_s
{
    cmsg_transport_type type;
    int family;
    cmsg_socket_address sockaddr;
    //todo: add cpg structures here
};

cmsg_transport*
cmsg_transport_new (cmsg_transport_type type);

int32_t
cmsg_transport_destroy (cmsg_transport* transport);

#endif
