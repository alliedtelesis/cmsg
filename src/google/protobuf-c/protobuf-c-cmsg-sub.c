#include "protobuf-c-cmsg-private.h"
#include "protobuf-c-cmsg-sub.h"
#include "protobuf-c-cmsg-error.h"

static cmsg_sub * _cmsg_create_subscriber_tipc (const char *server_name, int member_id,
                                                int scope, ProtobufCService *descriptor,
                                                cmsg_transport_type transport_type);


cmsg_sub *
cmsg_sub_new (cmsg_transport *pub_server_transport, ProtobufCService *pub_service)
{
    cmsg_sub *subscriber = (cmsg_sub *) CMSG_CALLOC (1, sizeof (cmsg_sub));
    if (!subscriber)
    {
        CMSG_LOG_GEN_ERROR ("[%s%s] Unable to allocate memory for subscriber.",
                            pub_service->descriptor->name, pub_server_transport->tport_id);
        return NULL;
    }

    subscriber->pub_server = cmsg_server_new (pub_server_transport, pub_service);
    if (!subscriber->pub_server)
    {
        CMSG_LOG_GEN_ERROR ("[%s%s] Unable to create pub_server.",
                            pub_service->descriptor->name, pub_server_transport->tport_id);
        CMSG_FREE (subscriber);
        return NULL;
    }

    return subscriber;
}


void
cmsg_sub_destroy (cmsg_sub *subscriber)
{
    if (subscriber)
    {
        if (subscriber->pub_server)
        {
            cmsg_server_destroy (subscriber->pub_server);
            subscriber->pub_server = NULL;
        }

        CMSG_FREE (subscriber);
    }

    return;
}


int
cmsg_sub_get_server_socket (cmsg_sub *subscriber)
{
    CMSG_ASSERT_RETURN_VAL (subscriber != NULL, -1);

    return (cmsg_server_get_socket (subscriber->pub_server));
}


int32_t
cmsg_sub_server_receive_poll (cmsg_sub *sub, int32_t timeout_ms, fd_set *master_fdset,
                              int *fdmax)
{
    return cmsg_server_receive_poll (sub->pub_server, timeout_ms, master_fdset, fdmax);
}


int32_t
cmsg_sub_server_receive (cmsg_sub *subscriber, int32_t server_socket)
{
    CMSG_DEBUG (CMSG_INFO, "[SUB]\n");

    CMSG_ASSERT_RETURN_VAL (subscriber != NULL, CMSG_RET_ERR);

    return cmsg_server_receive (subscriber->pub_server, server_socket);
}


int32_t
cmsg_sub_server_accept (cmsg_sub *subscriber, int32_t listen_socket)
{
    CMSG_ASSERT_RETURN_VAL (subscriber != NULL, -1);

    return cmsg_server_accept (subscriber->pub_server, listen_socket);
}


/**
 * Callback function for CMSG subscriber when a new socket is accepted.
 * This function is for applications that accept sockets by other than CMSG API,
 * cmsg_sub_server_accept() (e.g. by using liboop socket utility functions).
 */
void
cmsg_sub_server_accept_callback (cmsg_sub *subscriber, int32_t sock)
{
    if (subscriber != NULL)
    {
        cmsg_server_accept_callback (subscriber->pub_server, sock);
    }
}


int32_t
cmsg_sub_subscribe (cmsg_sub *subscriber,
                    cmsg_transport *sub_client_transport, char *method_name)
{
    CMSG_ASSERT_RETURN_VAL (subscriber != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (subscriber->pub_server != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (subscriber->pub_server->_transport != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (sub_client_transport != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (method_name != NULL, CMSG_RET_ERR);

    cmsg_client *register_client = NULL;
    int32_t return_value = CMSG_RET_ERR;
    cmsg_client_closure_data closure_data = { NULL, NULL};
    cmsg_sub_entry_transport_info register_entry = CMSG_SUB_ENTRY_TRANSPORT_INFO_INIT;
    cmsg_sub_entry_response *response = NULL;

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

        register_entry.tipc_family = subscriber->pub_server->_transport->config.socket.sockaddr.tipc.family;
        register_entry.tipc_addrtype = subscriber->pub_server->_transport->config.socket.sockaddr.tipc.addrtype;
        register_entry.tipc_addr_name_domain = subscriber->pub_server->_transport->config.socket.sockaddr.tipc.addr.name.domain;
        register_entry.tipc_addr_name_name_instance = subscriber->pub_server->_transport->config.socket.sockaddr.tipc.addr.name.name.instance;
        register_entry.tipc_addr_name_name_type = subscriber->pub_server->_transport->config.socket.sockaddr.tipc.addr.name.name.type;
        register_entry.tipc_scope = subscriber->pub_server->_transport->config.socket.sockaddr.tipc.scope;
    }
    else
    {
        CMSG_LOG_GEN_ERROR
            ("[%s%s] Transport type incorrect for cmsg_sub_subscribe: type(%d).",
             subscriber->pub_server->service->descriptor->name,
             subscriber->pub_server->_transport->tport_id,
             subscriber->pub_server->_transport->type);

        return CMSG_RET_ERR;
    }

    register_client = cmsg_client_new (sub_client_transport,
                                       &cmsg_sub_service_descriptor);
    if (!register_client)
    {
        CMSG_LOG_GEN_ERROR ("[%s%s] Unable to create register client for subscriber.",
                            subscriber->pub_server->service->descriptor->name,
                            sub_client_transport->tport_id);
        CMSG_FREE (register_client);
        return CMSG_RET_ERR;
    }

    return_value = cmsg_sub_service_subscribe ((ProtobufCService *) register_client,
                                               &register_entry,
                                               NULL,
                                               &closure_data);

    if (closure_data.message)
    {
        response = closure_data.message;
        if (response->return_value == CMSG_RET_ERR)
            return_value = CMSG_RET_ERR;

        protobuf_c_message_free_unpacked (closure_data.message, closure_data.allocator);
    }

    cmsg_client_destroy (register_client);

    return return_value;
}


int32_t
cmsg_sub_unsubscribe (cmsg_sub *subscriber, cmsg_transport *sub_client_transport,
                      char *method_name)
{
    cmsg_client *register_client = NULL;
    int32_t return_value = CMSG_RET_ERR;
    cmsg_client_closure_data closure_data = { NULL, NULL};
    cmsg_sub_entry_transport_info register_entry = CMSG_SUB_ENTRY_TRANSPORT_INFO_INIT;
    cmsg_sub_entry_response *response = NULL;

    CMSG_ASSERT_RETURN_VAL (subscriber != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (subscriber->pub_server != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (subscriber->pub_server->_transport != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (sub_client_transport != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (method_name != NULL, CMSG_RET_ERR);

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

        register_entry.tipc_family = subscriber->pub_server->_transport->config.socket.sockaddr.tipc.family;
        register_entry.tipc_addrtype = subscriber->pub_server->_transport->config.socket.sockaddr.tipc.addrtype;
        register_entry.tipc_addr_name_domain = subscriber->pub_server->_transport->config.socket.sockaddr.tipc.addr.name.domain;
        register_entry.tipc_addr_name_name_instance = subscriber->pub_server->_transport->config.socket.sockaddr.tipc.addr.name.name.instance;
        register_entry.tipc_addr_name_name_type = subscriber->pub_server->_transport->config.socket.sockaddr.tipc.addr.name.name.type;
        register_entry.tipc_scope = subscriber->pub_server->_transport->config.socket.sockaddr.tipc.scope;
    }
    else
    {
        CMSG_LOG_GEN_ERROR
            ("[%s.%s] Transport type incorrect for cmsg_sub_subscribe: type(%d).",
             subscriber->pub_server->service->descriptor->name,
             subscriber->pub_server->_transport->tport_id,
             subscriber->pub_server->_transport->type);

        return CMSG_RET_ERR;
    }

    register_client = cmsg_client_new (sub_client_transport,
                                       &cmsg_sub_service_descriptor);
    if (!register_client)
    {
        CMSG_LOG_GEN_ERROR ("[%s%s] Unable to create register client for subscriber.",
                            subscriber->pub_server->service->descriptor->name,
                            sub_client_transport->tport_id);
        CMSG_FREE (register_client);
        return CMSG_RET_ERR;
    }

    return_value = cmsg_sub_service_subscribe ((ProtobufCService *) register_client,
                                               &register_entry,
                                               NULL,
                                               &closure_data);

    if (closure_data.message)
    {
        response = closure_data.message;
        if (response->return_value == CMSG_RET_ERR)
            return_value = CMSG_RET_ERR;

        protobuf_c_message_free_unpacked (closure_data.message, closure_data.allocator);
    }

    cmsg_client_destroy (register_client);

    return return_value;
}

static cmsg_sub *
_cmsg_create_subscriber_tipc (const char *server_name, int member_id, int scope,
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
        CMSG_LOG_GEN_ERROR ("[%s%s] No TIPC subscriber to %d",
                            descriptor->descriptor->name, transport->tport_id, member_id);
        return NULL;
    }

    return subscriber;
}

cmsg_sub *
cmsg_create_subscriber_tipc_rpc (const char *server_name, int member_id, int scope,
                                 ProtobufCService *descriptor)
{
    CMSG_ASSERT_RETURN_VAL (server_name != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (descriptor != NULL, NULL);

    return _cmsg_create_subscriber_tipc (server_name, member_id, scope, descriptor,
                                        CMSG_TRANSPORT_RPC_TIPC);
}

cmsg_sub *
cmsg_create_subscriber_tipc_oneway (const char *server_name, int member_id, int scope,
                                    ProtobufCService *descriptor)
{
    CMSG_ASSERT_RETURN_VAL (server_name != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (descriptor != NULL, NULL);

    return _cmsg_create_subscriber_tipc (server_name, member_id, scope, descriptor,
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
