#include "protobuf-c-cmsg-sub.h"


cmsg_sub *
cmsg_sub_new (cmsg_transport *pub_server_transport, ProtobufCService *pub_service)
{
    CMSG_ASSERT (pub_server_transport);
    CMSG_ASSERT (pub_service);

    cmsg_sub *subscriber = malloc (sizeof (cmsg_sub));
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
        free (subscriber);
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

    free (subscriber);

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
    uint32_t *return_value = (uint32_t *) closure_data;

    if (response == 0)
    {
        DEBUG (CMSG_ERROR, "[SUB] error: processing register response\n");
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
    u_int32_t return_value = CMSG_RET_OK;
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
        DEBUG (CMSG_ERROR,
               "[SUB] error cmsg_sub_subscribe transport incorrect: %d\n",
               subscriber->pub_server->_transport->type);

        return CMSG_RET_ERR;
    }

    register_client = cmsg_client_new (sub_client_transport,
                                       &cmsg__sub_service__descriptor);
    if (!register_client)
    {
        DEBUG (CMSG_WARN, "[SUB] error could not create register client\n");
        free (register_client);
        return CMSG_RET_OK;
    }

    cmsg__sub_service__subscribe ((ProtobufCService *) register_client,
                                  &register_entry,
                                  cmsg_sub_subscribe_response_handler, &return_value);

    cmsg_client_destroy (register_client);

    return return_value;
}


int32_t
cmsg_sub_unsubscribe (cmsg_sub *subscriber, cmsg_transport *sub_client_transport,
                      char *method_name)
{
    cmsg_client *register_client = 0;
    u_int32_t return_value = CMSG_RET_OK;
    Cmsg__SubEntry register_entry = CMSG__SUB_ENTRY__INIT;

    CMSG_ASSERT (subscriber);
    CMSG_ASSERT (subscriber->pub_server);
    CMSG_ASSERT (subscriber->pub_server->_transport);
    CMSG_ASSERT (sub_client_transport);
    CMSG_ASSERT (method_name);

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
        free (register_client);
        return CMSG_RET_OK;
    }

    cmsg__sub_service__subscribe ((ProtobufCService *) register_client, &register_entry,
                                  cmsg_sub_subscribe_response_handler, &return_value);

    cmsg_client_destroy (register_client);

    return return_value;
}


/*****************************************************************************/
/****************** Filtering & Queuing functions ****************************/
/*****************************************************************************/
void
cmsg_sub_drop_all (cmsg_sub *sub)
{
    cmsg_server_queue_filter_set_all (sub->pub_server, CMSG_QUEUE_FILTER_DROP);
}

void
cmsg_sub_queue_enable (cmsg_sub *sub)
{
    cmsg_server_queue_filter_set_all (sub->pub_server, CMSG_QUEUE_FILTER_QUEUE);
}

int32_t
cmsg_sub_queue_disable (cmsg_sub *sub)
{
    cmsg_server_queue_filter_set_all (sub->pub_server, CMSG_QUEUE_FILTER_PROCESS);

    return cmsg_server_queue_process_all (sub->pub_server);
}


int32_t
cmsg_sub_queue_process_one (cmsg_sub *sub)
{
    return cmsg_receive_queue_process_one (sub->pub_server->queue,
                                           sub->pub_server->queue_mutex,
                                           sub->pub_server->service->descriptor,
                                           sub->pub_server);
}


/**
 * Processes the upto the given number of items to process out of the queue
 */
int32_t
cmsg_sub_queue_process_some (cmsg_sub *sub, uint32_t num_to_process)
{
    return cmsg_receive_queue_process_some (sub->pub_server->queue,
                                            sub->pub_server->queue_mutex,
                                            sub->pub_server->service->descriptor,
                                            sub->pub_server, num_to_process);
}


/**
 * Processes all the items in the queue.
 *
 * @returns the number of items processed off the queue
 */
int32_t
cmsg_sub_queue_process_all (cmsg_sub *sub)
{
    return cmsg_server_queue_process_all (sub->pub_server);
}

void
cmsg_sub_queue_filter_set_all (cmsg_sub *sub, cmsg_queue_filter_type filter_type)
{
    cmsg_queue_filter_set_all (sub->pub_server->queue_filter_hash_table,
                               sub->pub_server->service->descriptor, filter_type);
}

void
cmsg_sub_queue_filter_clear_all (cmsg_sub *sub)
{
    cmsg_queue_filter_clear_all (sub->pub_server->queue_filter_hash_table,
                                 sub->pub_server->service->descriptor);
}

int32_t
cmsg_sub_queue_filter_set (cmsg_sub *sub,
                           const char *method, cmsg_queue_filter_type filter_type)
{
    return cmsg_queue_filter_set (sub->pub_server->queue_filter_hash_table,
                                  method, filter_type);
}

int32_t
cmsg_sub_queue_filter_clear (cmsg_sub *sub, const char *method)
{
    return cmsg_queue_filter_clear (sub->pub_server->queue_filter_hash_table, method);
}

void
cmsg_sub_queue_filter_show (cmsg_sub *sub)
{
    cmsg_queue_filter_show (sub->pub_server->queue_filter_hash_table,
                            sub->pub_server->service->descriptor);
}

uint32_t
cmsg_sub_queue_max_length_get (cmsg_sub *sub)
{
    return sub->pub_server->maxQueueLength;
}

uint32_t
cmsg_sub_queue_current_length_get (cmsg_sub *sub)
{
    return g_queue_get_length (sub->pub_server->queue);
}
