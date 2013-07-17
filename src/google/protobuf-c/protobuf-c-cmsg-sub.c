#include "protobuf-c-cmsg-sub.h"


cmsg_sub*
cmsg_sub_new (cmsg_transport*   pub_server_transport,
              ProtobufCService* pub_service)
{
  cmsg_sub *subscriber = 0;

  subscriber = malloc(sizeof(cmsg_sub));
  subscriber->pub_server = cmsg_server_new(pub_server_transport, pub_service);
  if (!subscriber->pub_server)
    {
      DEBUG (CMSG_ERROR, "[SUB] error could not create server\n");
      free(subscriber);
      return 0;
    }

  return subscriber;
}


int32_t
cmsg_sub_destroy (cmsg_sub* subscriber)
{
  if (!subscriber)
    return 1;

  if (subscriber->pub_server)
    {
      cmsg_server_destroy(subscriber->pub_server);
      subscriber->pub_server = 0;
    }

  free(subscriber);
  subscriber = 0;
  return 0;
}


int
cmsg_sub_get_server_socket (cmsg_sub* subscriber)
{
  return (cmsg_server_get_socket (subscriber->pub_server));
}


int32_t
cmsg_sub_server_receive (cmsg_sub* subscriber,
                         int32_t   server_socket)
{
  DEBUG (CMSG_INFO, "[SUB] cmsg_sub_server_receive\n");
  
  return cmsg_server_receive(subscriber->pub_server,
                             server_socket);
}


void
cmsg_sub_subscribe_response_handler (const Cmsg__SubEntryResponse *response,
                                     void *closure_data)
{
  uint32_t* return_value;
  cmsg_sub* subscriber = (cmsg_sub*)closure_data;

  if (response == 0)
    {
      DEBUG (CMSG_ERROR, "[SUB] [Error] processing register response\n");
      *return_value = CMSG_STATUS_CODE_SERVICE_FAILED;
    }
  else
    {
      DEBUG (CMSG_INFO, "[SUB] register response received\n");
      *return_value = response->return_value;
    }
}


int32_t
cmsg_sub_subscribe (cmsg_sub*       subscriber,
                    cmsg_transport* sub_client_transport,
                    char*           method_name)
{
  cmsg_client* register_client = 0;
  u_int32_t return_value;
  Cmsg__SubEntry register_entry = CMSG__SUB_ENTRY__INIT;

  register_entry.add = 1;
  register_entry.method_name = method_name;
  register_entry.transport_type = subscriber->pub_server->transport->type;

  if (register_entry.transport_type == CMSG_TRANSPORT_ONEWAY_TCP)
    {
      register_entry.has_in_sin_addr_s_addr = 1;
      register_entry.has_in_sin_port = 1;
      
      register_entry.in_sin_addr_s_addr = subscriber->pub_server->transport->connection_info.sockaddr.addr.in.sin_addr.s_addr;
      register_entry.in_sin_port = subscriber->pub_server->transport->connection_info.sockaddr.addr.in.sin_port;
    }
  else if (register_entry.transport_type == CMSG_TRANSPORT_ONEWAY_TIPC)
    {
      register_entry.has_tipc_family = 1;
      register_entry.has_tipc_addrtype = 1;
      register_entry.has_tipc_addr_name_domain = 1;
      register_entry.has_tipc_addr_name_name_instance = 1;
      register_entry.has_tipc_addr_name_name_type = 1;
      register_entry.has_tipc_scope = 1;

      register_entry.tipc_family = subscriber->pub_server->transport->connection_info.sockaddr.addr.tipc.family;
      register_entry.tipc_addrtype = subscriber->pub_server->transport->connection_info.sockaddr.addr.tipc.addrtype;
      register_entry.tipc_addr_name_domain = subscriber->pub_server->transport->connection_info.sockaddr.addr.tipc.addr.name.domain;
      register_entry.tipc_addr_name_name_instance = subscriber->pub_server->transport->connection_info.sockaddr.addr.tipc.addr.name.name.instance;
      register_entry.tipc_addr_name_name_type = subscriber->pub_server->transport->connection_info.sockaddr.addr.tipc.addr.name.name.type;
      register_entry.tipc_scope = subscriber->pub_server->transport->connection_info.sockaddr.addr.tipc.scope;
    }
  else
    {
      DEBUG (CMSG_ERROR,
             "[SUB] error cmsg_sub_subscribe transport incorrect: %d\n",
             subscriber->pub_server->transport->type);

      return -1;
    }

  register_client = cmsg_client_new(sub_client_transport,
                                    &cmsg__sub_service__descriptor);
  if (!register_client)
    {
      DEBUG (CMSG_WARN, "[SUB] error could not create register client\n");
      free(register_client);
      return 0;
    }

  cmsg__sub_service__subscribe((ProtobufCService*)register_client,
                               &register_entry,
                               cmsg_sub_subscribe_response_handler,
                               &return_value);

  cmsg_client_destroy(register_client);

  return return_value;
}


int32_t
cmsg_sub_unsubscribe (cmsg_sub*       subscriber,
                      cmsg_transport* sub_client_transport,
                      char*           method_name)
{
  cmsg_client* register_client = 0;
  u_int32_t return_value;
  Cmsg__SubEntry register_entry = CMSG__SUB_ENTRY__INIT;

  register_entry.add = 1;
  register_entry.method_name = method_name;
  register_entry.transport_type = subscriber->pub_server->transport->type;

  if (register_entry.transport_type == CMSG_TRANSPORT_ONEWAY_TCP)
    {
      register_entry.has_in_sin_addr_s_addr = 1;
      register_entry.has_in_sin_port = 1;
      
      register_entry.in_sin_addr_s_addr = subscriber->pub_server->transport->connection_info.sockaddr.addr.in.sin_addr.s_addr;
      register_entry.in_sin_port = subscriber->pub_server->transport->connection_info.sockaddr.addr.in.sin_port;
    }
  else if (register_entry.transport_type == CMSG_TRANSPORT_ONEWAY_TIPC)
    {
      register_entry.has_tipc_family = 1;
      register_entry.has_tipc_addrtype = 1;
      register_entry.has_tipc_addr_name_domain = 1;
      register_entry.has_tipc_addr_name_name_instance = 1;
      register_entry.has_tipc_addr_name_name_type = 1;
      register_entry.has_tipc_scope = 1;

      register_entry.tipc_family = subscriber->pub_server->transport->connection_info.sockaddr.addr.tipc.family;
      register_entry.tipc_addrtype = subscriber->pub_server->transport->connection_info.sockaddr.addr.tipc.addrtype;
      register_entry.tipc_addr_name_domain = subscriber->pub_server->transport->connection_info.sockaddr.addr.tipc.addr.name.domain;
      register_entry.tipc_addr_name_name_instance = subscriber->pub_server->transport->connection_info.sockaddr.addr.tipc.addr.name.name.instance;
      register_entry.tipc_addr_name_name_type = subscriber->pub_server->transport->connection_info.sockaddr.addr.tipc.addr.name.name.type;
      register_entry.tipc_scope = subscriber->pub_server->transport->connection_info.sockaddr.addr.tipc.scope;
    }
  else
    {
      DEBUG (CMSG_ERROR,
             "[SUB] error cmsg_sub_subscribe transport incorrect: %d\n",
             subscriber->pub_server->transport->type);

      return -1;
    }

  register_client = cmsg_client_new(sub_client_transport,
                                    &cmsg__sub_service__descriptor);
  if (!register_client)
    {
      DEBUG (CMSG_ERROR, "[SUB] error could not create register client\n");
      free(register_client);
      return 0;
    }

  cmsg__sub_service__subscribe((ProtobufCService*)register_client,
                               &register_entry,
                               cmsg_sub_subscribe_response_handler,
                               &return_value);

  cmsg_client_destroy(register_client);

  return return_value;
}
