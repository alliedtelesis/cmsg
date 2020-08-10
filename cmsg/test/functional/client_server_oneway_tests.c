/*
 * Functional tests for client <-> server one-way communication.
 *
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */

#include <arpa/inet.h>
#include <np.h>
#include <stdint.h>
#include <cmsg_server.h>
#include "cmsg_functional_tests_api_auto.h"
#include "cmsg_functional_tests_impl_auto.h"
#include "setup.h"

/**
 * This informs the compiler that the function is, in fact, being used even though it
 * doesn't look like it. This is useful for static functions that get found by NovaProva
 * using debug symbols.
 */
#define USED __attribute__ ((used))

/* Hold test parameters */
struct t_parms
{
    cmsg_transport_type type;
    int family;
};

static const uint16_t port_number = 18889;
static const uint16_t tipc_instance = 1;
static const uint16_t tipc_scope = TIPC_NODE_SCOPE;

static cmsg_server *server = NULL;
static bool server_thread_run = true;
static bool server_ready = false;
static pthread_t server_thread;

static void
setup_udt_tcp_transport_functions (cmsg_transport *udt_transport)
{
    cmsg_transport_udt_tcp_base_init (udt_transport, true);

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

    udt_transport->udt_info.functions.is_congested =
        udt_transport->udt_info.base.is_congested;
    udt_transport->udt_info.functions.ipfree_bind_enable =
        udt_transport->udt_info.base.ipfree_bind_enable;

    udt_transport->udt_info.functions.server_send =
        udt_transport->udt_info.base.server_send;
}

static int
sm_mock_cmsg_service_port_get (const char *name, const char *proto)
{
    if (strcmp (name, "cmsg-test") == 0)
    {
        return port_number;
    }

    NP_FAIL;

    return 0;
}

/**
 * Common functionality to run before each test case.
 */
static int USED
set_up (void)
{
    server_ready = false;
    server_thread_run = true;

    np_mock (cmsg_service_port_get, sm_mock_cmsg_service_port_get);

    /* Ignore SIGPIPE signal if it occurs */
    signal (SIGPIPE, SIG_IGN);

    cmsg_service_listener_mock_functions ();

    return 0;
}

/**
 * Common functionality to run at the end of each test case.
 */
static int USED
tear_down (void)
{
    NP_ASSERT_NULL (server);

    return 0;
}

void
cmsg_test_impl_simple_oneway_test (const void *service, const cmsg_bool_msg *recv_msg)
{
    cmsg_bool_msg send_msg = CMSG_BOOL_MSG_INIT;

    NP_ASSERT_TRUE (recv_msg->value);

    CMSG_SET_FIELD_VALUE (&send_msg, value, true);
}

/**
 * Server processing function that should be run in a new thread.
 * Creates a server of given type and then begins polling the server
 * for any received messages. Once the main thread signals the polling
 * to stop the server is destroyed and the thread exits.
 *
 * @param arg - Enum value of the transport type of the server to
 *              create cast to a pointer
 */
static void *
server_thread_process (void *arg)
{
    cmsg_transport *server_transport = NULL;
    struct t_parms *tpp = arg;

    int my_id = 4;              //Stack member id
    int stack_tipc_port = 9500; //Stack topology sending port

    switch (tpp->type)
    {
    case CMSG_TRANSPORT_ONEWAY_TCP:
        if (tpp->family == AF_INET)
        {
            struct in_addr tcp_addr;

            tcp_addr.s_addr = INADDR_ANY;
            server = cmsg_create_server_tcp_ipv4_oneway ("cmsg-test", &tcp_addr, NULL,
                                                         CMSG_SERVICE (cmsg, test));
        }
        else if (tpp->family == AF_INET6)
        {
            struct in6_addr tcp_addr = IN6ADDR_ANY_INIT;

            server = cmsg_create_server_tcp_ipv6_oneway ("cmsg-test", &tcp_addr, 0, NULL,
                                                         CMSG_SERVICE (cmsg, test));
        }
        break;

    case CMSG_TRANSPORT_ONEWAY_TIPC:
        server = cmsg_create_server_tipc_oneway ("cmsg-test", tipc_instance, tipc_scope,
                                                 CMSG_SERVICE (cmsg, test));
        break;

    case CMSG_TRANSPORT_ONEWAY_UNIX:
        server = cmsg_create_server_unix_oneway (CMSG_SERVICE (cmsg, test));
        break;

    case CMSG_TRANSPORT_BROADCAST:
        server_transport = cmsg_transport_new (CMSG_TRANSPORT_BROADCAST);

        server_transport->config.socket.sockaddr.tipc.addrtype = TIPC_ADDR_NAMESEQ;
        server_transport->config.socket.sockaddr.tipc.scope = TIPC_CLUSTER_SCOPE;
        server_transport->config.socket.sockaddr.tipc.addr.nameseq.type = stack_tipc_port;
        server_transport->config.socket.sockaddr.tipc.addr.nameseq.lower = my_id;
        server_transport->config.socket.sockaddr.tipc.addr.nameseq.upper = my_id;

        server = cmsg_server_new (server_transport, CMSG_SERVICE (cmsg, test));
        break;

    case CMSG_TRANSPORT_ONEWAY_USERDEFINED:
        server_transport = cmsg_transport_new (CMSG_TRANSPORT_ONEWAY_USERDEFINED);
        server_transport->config.socket.family = PF_INET;
        server_transport->config.socket.sockaddr.generic.sa_family = PF_INET;
        server_transport->config.socket.sockaddr.in.sin_addr.s_addr = htonl (INADDR_ANY);
        server_transport->config.socket.sockaddr.in.sin_port = htons (port_number);

        setup_udt_tcp_transport_functions (server_transport);

        server = cmsg_server_new (server_transport, CMSG_SERVICE (cmsg, test));
        break;

    default:
        NP_FAIL;
    }

    int fd = cmsg_server_get_socket (server);
    int fd_max = fd + 1;

    fd_set readfds;
    FD_ZERO (&readfds);
    FD_SET (fd, &readfds);

    server_ready = true;

    while (server_thread_run)
    {
        cmsg_server_receive_poll (server, 1000, &readfds, &fd_max);
    }

    // Close accepted sockets before destroying server
    for (fd = 0; fd <= fd_max; fd++)
    {
        if (FD_ISSET (fd, &readfds))
        {
            close (fd);
        }
    }

    cmsg_destroy_server_and_transport (server);

    server = NULL;

    return 0;
}

/**
 * Create the server used to process the CMSG IMPL functions
 * in a new thread. Once the new thread is created the function
 * waits until the new thread signals that it is ready for processing.
 *
 * @param type - Transport type of the server to create
 */
static void
create_server_and_wait (cmsg_transport_type type, int family)
{
    int ret = 0;
    struct t_parms tp;

    tp.type = type;
    tp.family = family;

    ret = pthread_create (&server_thread, NULL, &server_thread_process, &tp);

    NP_ASSERT_EQUAL (ret, 0);

    while (!server_ready)
    {
        usleep (100000);
    }
}

/**
 * Signal the server in the different thread to stop processing
 * and then wait for the server to be destroyed and the thread
 * to exit.
 */
static void
stop_server_and_wait (void)
{
    server_thread_run = false;
    pthread_join (server_thread, NULL);
}

/**
 * Create the client that will be used to run a functional test.
 *
 * @param type - Transport type of the client to create
 */
static cmsg_client *
create_client (cmsg_transport_type type, int family)
{
    cmsg_transport *transport = NULL;
    cmsg_client *client = NULL;
    int stack_tipc_port = 9500; //Stack topology sending port
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

    case CMSG_TRANSPORT_ONEWAY_TIPC:
        client = cmsg_create_client_tipc_oneway ("cmsg-test", tipc_instance, tipc_scope,
                                                 CMSG_DESCRIPTOR (cmsg, test));
        break;

    case CMSG_TRANSPORT_ONEWAY_UNIX:
        client = cmsg_create_client_unix_oneway (CMSG_DESCRIPTOR (cmsg, test));
        break;

    case CMSG_TRANSPORT_BROADCAST:
        transport = cmsg_transport_new (CMSG_TRANSPORT_BROADCAST);

        transport->config.socket.sockaddr.tipc.addrtype = TIPC_ADDR_MCAST;
        transport->config.socket.sockaddr.tipc.addr.nameseq.type = stack_tipc_port;
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

        setup_udt_tcp_transport_functions (transport);

        client = cmsg_client_new (transport, CMSG_DESCRIPTOR (cmsg, test));
        break;

    default:
        NP_FAIL;
    }

    return client;
}

/**
 * Run the simple test with a given CMSG client. Assumes the related
 * server has already been created and is ready to process any API
 * requests.
 *
 * @param client - CMSG client to run the simple test with
 */
static void
_run_client_server_tests (cmsg_client *client)
{
    int ret = 0;
    cmsg_bool_msg send_msg = CMSG_BOOL_MSG_INIT;

    CMSG_SET_FIELD_VALUE (&send_msg, value, true);

    ret = cmsg_test_api_simple_oneway_test (client, &send_msg);

    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
}

static void
run_client_server_tests (cmsg_transport_type type, int family)
{
    cmsg_client *client = NULL;

    create_server_and_wait (type, family);

    client = create_client (type, family);

    _run_client_server_tests (client);

    stop_server_and_wait ();
    cmsg_destroy_client_and_transport (client);
}

/**
 * Run the simple client <-> server test case with a TCP transport (IPv4).
 */
void
test_client_server_oneway_tcp (void)
{
    run_client_server_tests (CMSG_TRANSPORT_ONEWAY_TCP, AF_INET);
}

/**
 * Run the simple client <-> server test case with a TCP transport (IPv6).
 */
void
test_client_server_oneway_tcp6 (void)
{
    run_client_server_tests (CMSG_TRANSPORT_ONEWAY_TCP, AF_INET6);
}

/**
 * Run the simple client <-> server test case with a TIPC transport.
 */
void
test_client_server_oneway_tipc (void)
{
    run_client_server_tests (CMSG_TRANSPORT_ONEWAY_TIPC, AF_UNSPEC);
}

/**
 * Run the simple client <-> server test case with a UNIX transport.
 */
void
test_client_server_oneway_unix (void)
{
    run_client_server_tests (CMSG_TRANSPORT_ONEWAY_UNIX, AF_UNSPEC);
}

/**
 * Run the simple client <-> server test case with a TIPC broadcast transport.
 */
void
test_client_server_oneway_tipc_broadcast (void)
{
    run_client_server_tests (CMSG_TRANSPORT_BROADCAST, AF_UNSPEC);
}

/**
 * Run the simple client <-> server test case with a UDT (TCP) transport.
 */
void
test_client_server_oneway_udt (void)
{
    run_client_server_tests (CMSG_TRANSPORT_ONEWAY_USERDEFINED, AF_UNSPEC);
}
