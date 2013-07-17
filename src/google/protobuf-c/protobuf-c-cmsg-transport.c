#include "protobuf-c-cmsg-transport.h"


cmsg_transport*
cmsg_transport_new (cmsg_transport_type type)
{
  cmsg_transport* transport = 0;
  transport = malloc (sizeof(cmsg_transport));
  memset (transport, 0, sizeof (cmsg_transport));

  transport->type = type;

  switch (type)
  {
  case CMSG_TRANSPORT_RPC_TCP:
    cmsg_transport_tcp_init (transport);
    break;
  case CMSG_TRANSPORT_ONEWAY_TCP:
    cmsg_transport_oneway_tcp_init (transport);
    break;
  case CMSG_TRANSPORT_RPC_TIPC:
    cmsg_transport_tipc_init (transport);
    break;
  case CMSG_TRANSPORT_ONEWAY_TIPC:
    cmsg_transport_oneway_tipc_init (transport);
    break;
#ifdef HAVE_VCSTACK
  case CMSG_TRANSPORT_CPG:
    cmsg_transport_cpg_init (transport);
    break;
  case CMSG_TRANSPORT_BROADCAST:
      cmsg_transport_tipc_broadcast_init (transport);
      break;
#endif
  case CMSG_TRANSPORT_ONEWAY_USERDEFINED:
    cmsg_transport_oneway_udt_init (transport);
    break;

  default:
    DEBUG (CMSG_ERROR, "[TRANSPORT] transport type not supported\n");
    free (transport);
    transport = 0;
  }

  return transport;
}

int32_t
cmsg_transport_destroy (cmsg_transport *transport)
{
  if (transport)
  {
    free (transport);
    transport = 0;
    return 0;
  }
  else
    return 1;
}
