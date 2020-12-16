/*
 * Common setup functionality for the functional tests.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include <np.h>
#include "cmsg_functional_tests_impl_auto.h"
#include "service_listener/cmsg_sl_api_private.h"
#include "publisher_subscriber/cmsg_ps_api_private.h"
#include "setup.h"

static const uint16_t port_number = 18888;

#define CMSG_SLD_WAIT_TIME (500 * 1000)

void
cmsg_service_listener_daemon_start (void)
{
    system ("cmsg_sld &");
    usleep (CMSG_SLD_WAIT_TIME);
}

void
cmsg_service_listener_daemon_stop (void)
{
    system ("pkill cmsg_sld");
    usleep (CMSG_SLD_WAIT_TIME);
}

static void
sm_mock_cmsg_service_listener_add_server (cmsg_server *server)
{
    /* Do nothing. */
}

static void
sm_mock_cmsg_service_listener_remove_server (cmsg_server *server)
{
    /* Do nothing. */
}

/**
 * The CMSG service listener will not be running unless it is explicitly
 * started by a test. Ensure we mock any API calls to it to ensure the tests
 * don't fail on syslog messages.
 */
void
cmsg_service_listener_mock_functions (void)
{
    np_mock (cmsg_service_listener_add_server, sm_mock_cmsg_service_listener_add_server);
    np_mock (cmsg_service_listener_remove_server,
             sm_mock_cmsg_service_listener_remove_server);
}

static int32_t
sm_mock_cmsg_ps_subscription_add_local (cmsg_server *sub_server, const char *method_name)
{
    /* Do nothing. */
    return CMSG_RET_OK;
}

static int32_t
sm_mock_cmsg_ps_subscription_add_remote (cmsg_server *sub_server, const char *method_name,
                                         struct in_addr remote_addr)
{
    /* Do nothing. */
    return CMSG_RET_OK;
}

static int32_t
sm_mock_cmsg_ps_subscription_remove_local (cmsg_server *sub_server, const char *method_name)
{
    /* Do nothing. */
    return CMSG_RET_OK;
}

static int32_t
sm_mock_cmsg_ps_subscription_remove_remote (cmsg_server *sub_server,
                                            const char *method_name,
                                            struct in_addr remote_addr)
{
    /* Do nothing. */
    return CMSG_RET_OK;
}

static int32_t
sm_mock_cmsg_ps_remove_subscriber (cmsg_server *sub_server)
{
    /* Do nothing. */
    return CMSG_RET_OK;
}

/**
 * The CMSG publisher subscriber storage daemon will not be running unless it
 * is explicitly started by a test. Ensure we mock any API calls to it to ensure
 * the tests don't fail on syslog messages.
 */
void
cmsg_ps_mock_functions (void)
{
    np_mock (cmsg_ps_subscription_add_local, sm_mock_cmsg_ps_subscription_add_local);
    np_mock (cmsg_ps_subscription_add_remote, sm_mock_cmsg_ps_subscription_add_remote);
    np_mock (cmsg_ps_subscription_remove_local, sm_mock_cmsg_ps_subscription_remove_local);
    np_mock (cmsg_ps_subscription_remove_remote,
             sm_mock_cmsg_ps_subscription_remove_remote);
    np_mock (cmsg_ps_remove_subscriber, sm_mock_cmsg_ps_remove_subscriber);
}

int
sm_mock_cmsg_service_port_get (const char *name, const char *proto)
{
    if (strcmp (name, "cmsg-test") == 0)
    {
        return port_number;
    }

    NP_FAIL;

    return 0;
}

static void
setup_udt_tcp_transport_functions (cmsg_transport *udt_transport, bool oneway)
{
    cmsg_transport_udt_tcp_base_init (udt_transport, oneway);

    udt_transport->udt_info.functions.recv_wrapper =
        udt_transport->udt_info.base.recv_wrapper;
    udt_transport->udt_info.functions.connect = udt_transport->udt_info.base.connect;
    udt_transport->udt_info.functions.listen = udt_transport->udt_info.base.listen;
    udt_transport->udt_info.functions.server_accept =
        udt_transport->udt_info.base.server_accept;
    udt_transport->udt_info.functions.server_recv =
        udt_transport->udt_info.base.server_recv;
    udt_transport->udt_info.functions.client_recv =
        udt_transport->udt_info.base.client_recv;
    udt_transport->udt_info.functions.client_send =
        udt_transport->udt_info.base.client_send;
    udt_transport->udt_info.functions.socket_close =
        udt_transport->udt_info.base.socket_close;

    udt_transport->udt_info.functions.get_socket = udt_transport->udt_info.base.get_socket;

    udt_transport->udt_info.functions.server_send =
        udt_transport->udt_info.base.server_send;
}

/**
 * Create the client that will be used to run a functional test.
 *
 * @param type - Transport type of the client to create
 * @param family - If a TCP based transport whether it is IPv4 or IPv6
 *
 * @returns The client.
 */
cmsg_client *
create_client (cmsg_transport_type type, int family)
{
    cmsg_transport *transport = NULL;
    cmsg_client *client = NULL;
    struct in_addr tcp_addr;

    switch (type)
    {
    case CMSG_TRANSPORT_ONEWAY_TCP:
        if (family == AF_INET)
        {
            tcp_addr.s_addr = INADDR_ANY;

            client = cmsg_create_client_tcp_ipv4_oneway ("cmsg-test", &tcp_addr, NULL,
                                                         CMSG_DESCRIPTOR (cmsg, test));
        }
        else if (family == AF_INET6)
        {
            struct in6_addr tcp_addr = IN6ADDR_ANY_INIT;

            client = cmsg_create_client_tcp_ipv6_oneway ("cmsg-test", &tcp_addr, 0, NULL,
                                                         CMSG_DESCRIPTOR (cmsg, test));
        }
        break;

    case CMSG_TRANSPORT_RPC_TCP:
        if (family == AF_INET)
        {
            tcp_addr.s_addr = INADDR_ANY;
            client = cmsg_create_client_tcp_ipv4_rpc ("cmsg-test", &tcp_addr, NULL,
                                                      CMSG_DESCRIPTOR (cmsg, test));
        }
        else if (family == AF_INET6)
        {
            struct in6_addr tcp_addr = IN6ADDR_ANY_INIT;

            client = cmsg_create_client_tcp_ipv6_rpc ("cmsg-test", &tcp_addr, 0, NULL,
                                                      CMSG_DESCRIPTOR (cmsg, test));
        }
        break;

    case CMSG_TRANSPORT_ONEWAY_UNIX:
        client = cmsg_create_client_unix_oneway (CMSG_DESCRIPTOR (cmsg, test));
        break;

    case CMSG_TRANSPORT_RPC_UNIX:
        client = cmsg_create_client_unix (CMSG_DESCRIPTOR (cmsg, test));
        break;

    case CMSG_TRANSPORT_BROADCAST:
        transport = cmsg_transport_new (CMSG_TRANSPORT_BROADCAST);

        transport->config.socket.sockaddr.tipc.addrtype = TIPC_ADDR_MCAST;
        transport->config.socket.sockaddr.tipc.addr.nameseq.type = 9500;
        transport->config.socket.sockaddr.tipc.addr.nameseq.lower = 1;
        transport->config.socket.sockaddr.tipc.addr.nameseq.upper = 8;
        client = cmsg_client_new (transport, CMSG_DESCRIPTOR (cmsg, test));
        break;

    case CMSG_TRANSPORT_ONEWAY_USERDEFINED:
        transport = cmsg_transport_new (CMSG_TRANSPORT_ONEWAY_USERDEFINED);
        transport->config.socket.family = PF_INET;
        transport->config.socket.sockaddr.generic.sa_family = PF_INET;
        transport->config.socket.sockaddr.in.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
        transport->config.socket.sockaddr.in.sin_port = htons (port_number);

        setup_udt_tcp_transport_functions (transport, true);

        client = cmsg_client_new (transport, CMSG_DESCRIPTOR (cmsg, test));
        break;

    case CMSG_TRANSPORT_RPC_USERDEFINED:
        transport = cmsg_transport_new (CMSG_TRANSPORT_RPC_USERDEFINED);
        transport->config.socket.family = PF_INET;
        transport->config.socket.sockaddr.generic.sa_family = PF_INET;
        transport->config.socket.sockaddr.in.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
        transport->config.socket.sockaddr.in.sin_port = htons (port_number);

        setup_udt_tcp_transport_functions (transport, false);

        client = cmsg_client_new (transport, CMSG_DESCRIPTOR (cmsg, test));
        break;

    case CMSG_TRANSPORT_LOOPBACK:
        client = cmsg_create_client_loopback (CMSG_SERVICE (cmsg, test));
        break;

    default:
        NP_FAIL;
    }

    return client;
}

/**
 * Create the server used to process the CMSG IMPL functions in a new thread.
 *
 * @param type - Transport type of the server to create
 * @param family - If a TCP based transport whether it is IPv4 or IPv6
 * @param thread - The thread to run the server on.
 *
 * @returns The server.
 */
cmsg_server *
create_server (cmsg_transport_type type, int family, pthread_t *thread)
{
    bool ret;
    cmsg_transport *server_transport = NULL;
    cmsg_server *server = NULL;

    switch (type)
    {
    case CMSG_TRANSPORT_ONEWAY_TCP:
        if (family == AF_INET)
        {
            struct in_addr tcp_addr;

            tcp_addr.s_addr = INADDR_ANY;
            server = cmsg_create_server_tcp_ipv4_oneway ("cmsg-test", &tcp_addr, NULL,
                                                         CMSG_SERVICE (cmsg, test));
        }
        else if (family == AF_INET6)
        {
            struct in6_addr tcp_addr = IN6ADDR_ANY_INIT;

            server = cmsg_create_server_tcp_ipv6_oneway ("cmsg-test", &tcp_addr, 0, NULL,
                                                         CMSG_SERVICE (cmsg, test));
        }
        break;

    case CMSG_TRANSPORT_RPC_TCP:
        if (family == AF_INET)
        {
            struct in_addr tcp_addr;

            tcp_addr.s_addr = INADDR_ANY;
            server = cmsg_create_server_tcp_ipv4_rpc ("cmsg-test", &tcp_addr, NULL,
                                                      CMSG_SERVICE (cmsg, test));
        }
        else if (family == AF_INET6)
        {
            struct in6_addr tcp_addr = IN6ADDR_ANY_INIT;

            server = cmsg_create_server_tcp_ipv6_rpc ("cmsg-test", &tcp_addr, 0, NULL,
                                                      CMSG_SERVICE (cmsg, test));
        }
        break;

    case CMSG_TRANSPORT_ONEWAY_UNIX:
        server = cmsg_create_server_unix_oneway (CMSG_SERVICE (cmsg, test));
        break;

    case CMSG_TRANSPORT_RPC_UNIX:
        server = cmsg_create_server_unix_rpc (CMSG_SERVICE (cmsg, test));
        break;

    case CMSG_TRANSPORT_BROADCAST:
        server_transport = cmsg_transport_new (CMSG_TRANSPORT_BROADCAST);

        server_transport->config.socket.sockaddr.tipc.addrtype = TIPC_ADDR_NAMESEQ;
        server_transport->config.socket.sockaddr.tipc.scope = TIPC_CLUSTER_SCOPE;
        server_transport->config.socket.sockaddr.tipc.addr.nameseq.type = 9500;
        server_transport->config.socket.sockaddr.tipc.addr.nameseq.lower = 4;
        server_transport->config.socket.sockaddr.tipc.addr.nameseq.upper = 4;

        server = cmsg_server_new (server_transport, CMSG_SERVICE (cmsg, test));
        break;

    case CMSG_TRANSPORT_ONEWAY_USERDEFINED:
        server_transport = cmsg_transport_new (CMSG_TRANSPORT_ONEWAY_USERDEFINED);
        server_transport->config.socket.family = PF_INET;
        server_transport->config.socket.sockaddr.generic.sa_family = PF_INET;
        server_transport->config.socket.sockaddr.in.sin_addr.s_addr = htonl (INADDR_ANY);
        server_transport->config.socket.sockaddr.in.sin_port = htons (port_number);

        setup_udt_tcp_transport_functions (server_transport, true);

        server = cmsg_server_new (server_transport, CMSG_SERVICE (cmsg, test));
        break;

    case CMSG_TRANSPORT_RPC_USERDEFINED:
        server_transport = cmsg_transport_new (CMSG_TRANSPORT_RPC_USERDEFINED);
        server_transport->config.socket.family = PF_INET;
        server_transport->config.socket.sockaddr.generic.sa_family = PF_INET;
        server_transport->config.socket.sockaddr.in.sin_addr.s_addr = htonl (INADDR_ANY);
        server_transport->config.socket.sockaddr.in.sin_port = htons (port_number);

        setup_udt_tcp_transport_functions (server_transport, false);

        server = cmsg_server_new (server_transport, CMSG_SERVICE (cmsg, test));
        break;

    default:
        NP_FAIL;
    }

    ret = cmsg_pthread_server_init (thread, server);

    NP_ASSERT_TRUE (ret);

    return server;
}
