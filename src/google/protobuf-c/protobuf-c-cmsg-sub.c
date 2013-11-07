#include "protobuf-c-cmsg-sub.h"


cmsg_sub *
cmsg_sub_new (cmsg_transport *pub_server_transport, ProtobufCService *pub_service)
{
    CMSG_ASSERT (pub_server_transport);
    CMSG_ASSERT (pub_service);

    cmsg_sub *subscriber = CMSG_CALLOC (1, sizeof (cmsg_sub));
    if (!subscriber)
    {
        syslog (LOG_CRIT | LOG_LOCAL6, "[SUB] error: unable to allocate buffer. line(%d)\n",
                __LINE__);
        return NULL;
    }

    subscriber->pub_server = cmsg_server_new (pub_server_transport, pub_service);
    if (!subscriber->pub_server)
    {
        DEBUG (CMSG_ERROR, "[SUB] error: could not create server\n");
        CMSG_FREE (subscriber);
        return NULL;
    }

    return subscriber;
}


void
cmsg_sub_destroy (cmsg_sub *subscriber)
{
    CMSG_ASSERT (subscriber);

    if (subscriber->pub_server)
    {
        cmsg_server_destroy (subscriber->pub_server);
        subscriber->pub_server = NULL;
    }

    CMSG_FREE (subscriber);

    return;
}


int
cmsg_sub_get_server_socket (cmsg_sub *subscriber)
{
    CMSG_ASSERT (subscriber);

    return (cmsg_server_get_socket (subscriber->pub_server));
}


int32_t
cmsg_sub_server_receive (cmsg_sub *subscriber, int32_t server_socket)
{
    DEBUG (CMSG_INFO, "[SUB]\n");

    CMSG_ASSERT (subscriber);
    CMSG_ASSERT (server_socket > 0);

    return cmsg_server_receive (subscriber->pub_server, server_socket);
}


int32_t
cmsg_sub_server_accept (cmsg_sub *subscriber, int32_t listen_socket)
{
    return cmsg_server_accept (subscriber->pub_server, listen_socket);
}


void
cmsg_sub_subscribe_response_handler (const Cmsg__SubEntryResponse *response,
                                     void *closure_data)
{
    int32_t *return_value = (int32_t *) closure_data;

    if (response == 0)
    {
        CMSG_LOG_ERROR ("[SUB] error: processing register response");
        *return_value = CMSG_STATUS_CODE_SERVICE_FAILED;
    }
    else
    {
        DEBUG (CMSG_INFO, "[SUB] register response received\n");
        *return_value = response->return_value;
    }
}


int32_t
cmsg_sub_subscribe (cmsg_sub *subscriber,
                    cmsg_transport *sub_client_transport, char *method_name)
{
    CMSG_ASSERT (subscriber);
    CMSG_ASSERT (subscriber->pub_server);
    CMSG_ASSERT (subscriber->pub_server->_transport);
    CMSG_ASSERT (sub_client_transport);
    CMSG_ASSERT (method_name);

    cmsg_client *register_client = NULL;
    int32_t return_value = CMSG_RET_ERR;
    Cmsg__SubEntry register_entry = CMSG__SUB_ENTRY__INIT;

    register_entry.add = 1;
    register_entry.method_name = method_name;
    register_entry.transport_type = subscriber->pub_server->_transport->type;

    if (register_entry.transport_type == CMSG_TRANSPORT_ONEWAY_TCP)
    {
        register_entry.has_in_sin_addr_s_addr = 1;
        register_entry.has_in_sin_port = 1;

        register_entry.in_sin_addr_s_addr =
            subscriber->pub_server->_transport->config.socket.sockaddr.in.sin_addr.s_addr;
        register_entry.in_sin_port =
            subscriber->pub_server->_transport->config.socket.sockaddr.in.sin_port;
    }
    else if (register_entry.transport_type == CMSG_TRANSPORT_ONEWAY_TIPC)
    {
        register_entry.has_tipc_family = 1;
        register_entry.has_tipc_addrtype = 1;
        register_entry.has_tipc_addr_name_domain = 1;
        register_entry.has_tipc_addr_name_name_instance = 1;
        register_entry.has_tipc_addr_name_name_type = 1;
        register_entry.has_tipc_scope = 1;

        register_entry.tipc_family =
            subscriber->pub_server->_transport->config.socket.sockaddr.tipc.family;
        register_entry.tipc_addrtype =
            subscriber->pub_server->_transport->config.socket.sockaddr.tipc.addrtype;
        register_entry.tipc_addr_name_domain =
            subscriber->pub_server->_transport->config.socket.sockaddr.tipc.addr.name.domain;
        register_entry.tipc_addr_name_name_instance =
            subscriber->pub_server->_transport->config.socket.sockaddr.tipc.addr.name.name.instance;
        register_entry.tipc_addr_name_name_type =
            subscriber->pub_server->_transport->config.socket.sockaddr.tipc.addr.name.name.type;
        register_entry.tipc_scope =
            subscriber->pub_server->_transport->config.socket.sockaddr.tipc.scope;
    }
    else
    {
        CMSG_LOG_ERROR ("[SUB] error cmsg_sub_subscribe transport incorrect: %d",
                        subscriber->pub_server->_transport->type);

        return CMSG_RET_ERR;
    }

    register_client = cmsg_client_new (sub_client_transport,
                                       &cmsg__sub_service__descriptor);
    if (!register_client)
    {
        CMSG_LOG_ERROR ("[SUB] error could not create register client");
        CMSG_FREE (register_client);
        return CMSG_RET_ERR;
    }

    cmsg__sub_service__subscribe ((ProtobufCService *) register_client,
                                  &register_entry,
                                  cmsg_sub_subscribe_response_handler, &return_value);

    if (register_client->invoke_return_state == CMSG_RET_ERR)
    {
        CMSG_LOG_ERROR ("[SUB] error: couldn't subscribe to notification (method: %s)", method_name);
    }

    cmsg_client_destroy (register_client);

    return return_value;
}


int32_t
cmsg_sub_unsubscribe (cmsg_sub *subscriber, cmsg_transport *sub_client_transport,
                      char *method_name)
{
    cmsg_client *register_client = 0;
    u_int32_t return_value = CMSG_RET_ERR;
    Cmsg__SubEntry register_entry = CMSG__SUB_ENTRY__INIT;

    CMSG_ASSERT (subscriber);
    CMSG_ASSERT (subscriber->pub_server);
    CMSG_ASSERT (subscriber->pub_server->_transport);
    CMSG_ASSERT (sub_client_transport);
    CMSG_ASSERT (method_name);

    register_entry.add = 0;
    register_entry.method_name = method_name;
    register_entry.transport_type = subscriber->pub_server->_transport->type;

    if (register_entry.transport_type == CMSG_TRANSPORT_ONEWAY_TCP)
    {
        register_entry.has_in_sin_addr_s_addr = 1;
        register_entry.has_in_sin_port = 1;

        register_entry.in_sin_addr_s_addr =
            subscriber->pub_server->_transport->config.socket.sockaddr.in.sin_addr.s_addr;
        register_entry.in_sin_port =
            subscriber->pub_server->_transport->config.socket.sockaddr.in.sin_port;
    }
    else if (register_entry.transport_type == CMSG_TRANSPORT_ONEWAY_TIPC)
    {
        register_entry.has_tipc_family = 1;
        register_entry.has_tipc_addrtype = 1;
        register_entry.has_tipc_addr_name_domain = 1;
        register_entry.has_tipc_addr_name_name_instance = 1;
        register_entry.has_tipc_addr_name_name_type = 1;
        register_entry.has_tipc_scope = 1;

        register_entry.tipc_family =
            subscriber->pub_server->_transport->config.socket.sockaddr.tipc.family;
        register_entry.tipc_addrtype =
            subscriber->pub_server->_transport->config.socket.sockaddr.tipc.addrtype;
        register_entry.tipc_addr_name_domain =
            subscriber->pub_server->_transport->config.socket.sockaddr.tipc.addr.name.domain;
        register_entry.tipc_addr_name_name_instance =
            subscriber->pub_server->_transport->config.socket.sockaddr.tipc.addr.name.name.instance;
        register_entry.tipc_addr_name_name_type =
            subscriber->pub_server->_transport->config.socket.sockaddr.tipc.addr.name.name.type;
        register_entry.tipc_scope =
            subscriber->pub_server->_transport->config.socket.sockaddr.tipc.scope;
    }
    else
    {
        DEBUG (CMSG_ERROR,
               "[SUB] error: cmsg_sub_subscribe transport incorrect: %d\n",
               subscriber->pub_server->_transport->type);

        return CMSG_RET_ERR;
    }

    register_client = cmsg_client_new (sub_client_transport,
                                       &cmsg__sub_service__descriptor);
    if (!register_client)
    {
        DEBUG (CMSG_ERROR, "[SUB] error: could not create register client\n");
        CMSG_FREE (register_client);
        return CMSG_RET_ERR;
    }

    cmsg__sub_service__subscribe ((ProtobufCService *) register_client, &register_entry,
                                  cmsg_sub_subscribe_response_handler, &return_value);

    if (register_client->invoke_return_state == CMSG_RET_ERR)
    {
        CMSG_LOG_ERROR ("[SUB] error: couldn't unsubscribe to notification (method: %s)", method_name);
    }

    cmsg_client_destroy (register_client);

    return return_value;
}

static cmsg_sub *
cmsg_create_subscriber_tipc (const char *server_name, int member_id, int scope,
                             ProtobufCService *descriptor,
                             cmsg_transport_type transport_type)
{
    cmsg_transport *transport = NULL;
    cmsg_sub *subscriber = NULL;

    transport = cmsg_create_transport_tipc (server_name, member_id, scope, transport_type);
    if (transport == NULL)
    {
        return NULL;
    }

    subscriber = cmsg_sub_new (transport, descriptor);
    if (subscriber == NULL)
    {
        cmsg_transport_destroy (transport);
        CMSG_LOG_ERROR ("No TIPC subscriber to %d", member_id);
        return NULL;
    }

    return subscriber;
}

cmsg_sub *
cmsg_create_subscriber_tipc_rpc (const char *server_name, int member_id, int scope,
                                 ProtobufCService *descriptor)
{
    return cmsg_create_subscriber_tipc (server_name, member_id, scope, descriptor,
                                        CMSG_TRANSPORT_RPC_TIPC);
}

cmsg_sub *
cmsg_create_subscriber_tipc_oneway (const char *server_name, int member_id, int scope,
                                    ProtobufCService *descriptor)
{
    return cmsg_create_subscriber_tipc (server_name, member_id, scope, descriptor,
                                        CMSG_TRANSPORT_ONEWAY_TIPC);
}

void
cmsg_destroy_subscriber_and_transport (cmsg_sub *subscriber)
{
    cmsg_transport *transport;

    if (subscriber)
    {
        transport = subscriber->pub_server->_transport;
        cmsg_sub_destroy (subscriber);

        cmsg_transport_destroy (transport);
    }
}
