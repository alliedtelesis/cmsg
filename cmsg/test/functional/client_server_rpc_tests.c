/*
 * Functional tests for client <-> server RPC (two-way) communication.
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

#define STRING_ARRAY_LENGTH         100
#define TEST_STRING                 "The quick brown fox jumps over the lazy dog"

/* Hold test parameters */
struct t_parms
{
    cmsg_transport_type type;
    int family;
};

static const uint16_t port_number = 18888;
static const uint16_t tipc_instance = 1;
static const uint16_t tipc_scope = TIPC_NODE_SCOPE;

static cmsg_server *server = NULL;
static bool server_thread_run = true;
static bool server_ready = false;
static pthread_t server_thread;

static void
setup_udt_tcp_transport_functions (cmsg_transport *udt_transport)
{
    cmsg_transport_udt_tcp_base_init (udt_transport, false);

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

/**
 * CMSG IMPL function for the simple test. Simply assert that the received
 * message contains the correct value before sending the required message
 * back to the client.
 */
void
cmsg_test_impl_simple_rpc_test (const void *service, const cmsg_bool_msg *recv_msg)
{
    cmsg_bool_msg send_msg = CMSG_BOOL_MSG_INIT;

    NP_ASSERT_TRUE (recv_msg->value);

    CMSG_SET_FIELD_VALUE (&send_msg, value, true);

    cmsg_test_server_simple_rpc_testSend (service, &send_msg);
}

/**
 * CMSG IMPL function for the BIG test.
 *
 * Assert that the received strings and value are correct and send
 * back a similar message to the client.
 */
void
cmsg_test_impl_big_rpc_test (const void *service,
                             const cmsg_bool_plus_repeated_strings *recv_msg)
{
    cmsg_bool_plus_repeated_strings send_msg = CMSG_BOOL_PLUS_REPEATED_STRINGS_INIT;
    char *pointers[STRING_ARRAY_LENGTH];
    int i;

    NP_ASSERT_TRUE (recv_msg->value);
    NP_ASSERT_EQUAL (recv_msg->n_strings, STRING_ARRAY_LENGTH);
    for (i = 0; i < STRING_ARRAY_LENGTH; i++)
    {
        NP_ASSERT_STR_EQUAL (recv_msg->strings[i], TEST_STRING);
        pointers[i] = TEST_STRING;
    }

    CMSG_SET_FIELD_VALUE (&send_msg, value, true);
    CMSG_SET_FIELD_REPEATED (&send_msg, strings, pointers, STRING_ARRAY_LENGTH);

    cmsg_test_server_big_rpc_testSend (service, &send_msg);
}

/**
 * CMSG IMPL function for the empty msg test. This IMPL returns
 * empty message in the repeated field.
 */
void
cmsg_test_impl_empty_msg_rpc_test (const void *service)
{
    cmsg_repeated_strings send_msg = CMSG_REPEATED_STRINGS_INIT;

    CMSG_SET_FIELD_REPEATED (&send_msg, strings, NULL, 0);

    cmsg_test_server_empty_msg_rpc_testSend (service, &send_msg);
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

    switch (tpp->type)
    {
    case CMSG_TRANSPORT_RPC_TCP:
        if (tpp->family == AF_INET)
        {
            struct in_addr tcp_addr;

            tcp_addr.s_addr = INADDR_ANY;
            server = cmsg_create_server_tcp_ipv4_rpc ("cmsg-test", &tcp_addr, NULL,
                                                      CMSG_SERVICE (cmsg, test));
        }
        else if (tpp->family == AF_INET6)
        {
            struct in6_addr tcp_addr = IN6ADDR_ANY_INIT;

            server = cmsg_create_server_tcp_ipv6_rpc ("cmsg-test", &tcp_addr, 0, NULL,
                                                      CMSG_SERVICE (cmsg, test));
        }
        break;

    case CMSG_TRANSPORT_RPC_TIPC:
        server = cmsg_create_server_tipc_rpc ("cmsg-test", tipc_instance, tipc_scope,
                                              CMSG_SERVICE (cmsg, test));
        break;

    case CMSG_TRANSPORT_RPC_UNIX:
        server = cmsg_create_server_unix_rpc (CMSG_SERVICE (cmsg, test));
        break;

    case CMSG_TRANSPORT_RPC_USERDEFINED:
        server_transport = cmsg_transport_new (CMSG_TRANSPORT_RPC_USERDEFINED);
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
    struct in_addr tcp_addr;

    switch (type)
    {
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

    case CMSG_TRANSPORT_RPC_TIPC:
        client = cmsg_create_client_tipc_rpc ("cmsg-test", tipc_instance, tipc_scope,
                                              CMSG_DESCRIPTOR (cmsg, test));
        break;

    case CMSG_TRANSPORT_RPC_UNIX:
        client = cmsg_create_client_unix (CMSG_DESCRIPTOR (cmsg, test));
        break;

    case CMSG_TRANSPORT_LOOPBACK:
        client = cmsg_create_client_loopback (CMSG_SERVICE (cmsg, test));
        break;

    case CMSG_TRANSPORT_RPC_USERDEFINED:
        transport = cmsg_transport_new (CMSG_TRANSPORT_RPC_USERDEFINED);
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
    cmsg_bool_msg *recv_msg = NULL;

    CMSG_SET_FIELD_VALUE (&send_msg, value, true);

    ret = cmsg_test_api_simple_rpc_test (client, &send_msg, &recv_msg);

    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    NP_ASSERT_NOT_NULL (recv_msg);
    NP_ASSERT_TRUE (recv_msg->value);

    CMSG_FREE_RECV_MSG (recv_msg);
}

static void
run_client_server_tests (cmsg_transport_type type, int family)
{
    cmsg_client *client = NULL;

    if (type != CMSG_TRANSPORT_LOOPBACK)
    {
        create_server_and_wait (type, family);
    }

    client = create_client (type, family);

    _run_client_server_tests (client);

    if (type != CMSG_TRANSPORT_LOOPBACK)
    {
        stop_server_and_wait ();
    }
    cmsg_destroy_client_and_transport (client);
}

/**
 * Run the BIG test with a given CMSG client. Assumes the related
 * server has already been created and is ready to process any API
 * requests.
 *
 * @param client - CMSG client to run the simple test with
 */
static void
_run_client_server_tests_big (cmsg_client *client)
{
    int ret = 0;
    cmsg_bool_plus_repeated_strings send_msg = CMSG_BOOL_PLUS_REPEATED_STRINGS_INIT;
    cmsg_bool_plus_repeated_strings *recv_msg = NULL;
    char *pointers[STRING_ARRAY_LENGTH];
    int i;

    CMSG_SET_FIELD_VALUE (&send_msg, value, true);
    for (i = 0; i < STRING_ARRAY_LENGTH; i++)
    {
        pointers[i] = TEST_STRING;
    }
    CMSG_SET_FIELD_REPEATED (&send_msg, strings, pointers, STRING_ARRAY_LENGTH);

    ret = cmsg_test_api_big_rpc_test (client, &send_msg, &recv_msg);

    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    NP_ASSERT_NOT_NULL (recv_msg);
    NP_ASSERT_TRUE (recv_msg->value);
    NP_ASSERT_EQUAL (recv_msg->n_strings, STRING_ARRAY_LENGTH);
    for (i = 0; i < STRING_ARRAY_LENGTH; i++)
    {
        NP_ASSERT_STR_EQUAL (recv_msg->strings[i], TEST_STRING);
    }

    CMSG_FREE_RECV_MSG (recv_msg);
}

static void
run_client_server_tests_big (cmsg_transport_type type)
{
    cmsg_client *client = NULL;

    if (type != CMSG_TRANSPORT_LOOPBACK)
    {
        create_server_and_wait (type, AF_INET);
    }

    client = create_client (type, AF_INET);

    _run_client_server_tests_big (client);

    if (type != CMSG_TRANSPORT_LOOPBACK)
    {
        stop_server_and_wait ();
    }
    cmsg_destroy_client_and_transport (client);
}

/**
 * Run the simple client <-> server test case with a TCP transport (IPv4).
 */
void
test_client_server_rpc_tcp (void)
{
    run_client_server_tests (CMSG_TRANSPORT_RPC_TCP, AF_INET);
}

/**
 * Run the simple client <-> server test case with a TCP transport (IPv6).
 */
void
test_client_server_rpc_tcp6 (void)
{
    run_client_server_tests (CMSG_TRANSPORT_RPC_TCP, AF_INET6);
}

/**
 * Run the simple client <-> server test case with a TIPC transport.
 */
void
test_client_server_rpc_tipc (void)
{
    run_client_server_tests (CMSG_TRANSPORT_RPC_TIPC, AF_UNSPEC);
}

/**
 * Run the simple client <-> server test case with a UNIX transport.
 */
void
test_client_server_rpc_unix (void)
{
    run_client_server_tests (CMSG_TRANSPORT_RPC_UNIX, AF_UNSPEC);
}

/**
 * Run the simple client <-> server test case with a LOOPBACK transport.
 */
void
test_client_server_rpc_loopback (void)
{
    run_client_server_tests (CMSG_TRANSPORT_LOOPBACK, AF_UNSPEC);
}

/**
 * Run the simple client <-> server test case with a UDT (TCP) transport.
 */
void
test_client_server_rpc_udt (void)
{
    run_client_server_tests (CMSG_TRANSPORT_RPC_USERDEFINED, AF_UNSPEC);
}

/**
 * Run the BIG client <-> server test case with a TCP transport.
 */
void
test_client_server_rpc_tcp_big (void)
{
    run_client_server_tests_big (CMSG_TRANSPORT_RPC_TCP);
}

/**
 * Run the BIG client <-> server test case with a TIPC transport.
 */
void
test_client_server_rpc_tipc_big (void)
{
    run_client_server_tests_big (CMSG_TRANSPORT_RPC_TIPC);
}

/**
 * Run the BIG client <-> server test case with a UNIX transport.
 */
void
test_client_server_rpc_unix_big (void)
{
    run_client_server_tests_big (CMSG_TRANSPORT_RPC_UNIX);
}

/**
 * Run the BIG client <-> server test case with a LOOPBACK transport.
 */
void
test_client_server_rpc_loopback_big (void)
{
    run_client_server_tests_big (CMSG_TRANSPORT_LOOPBACK);
}

/**
 * Run the BIG client <-> server test case with a UDT (TCP) transport.
 */
void
test_client_server_rpc_udt_big (void)
{
    run_client_server_tests_big (CMSG_TRANSPORT_RPC_USERDEFINED);
}

/**
 * Run the empty msg test with a given CMSG client. Assumes the related
 * server has already been created and is ready to process any API
 * requests.
 *
 * @param client - CMSG client to run the test with
 */
static void
_run_client_server_tests_empty_msg (cmsg_client *client)
{
    int ret = 0;
    cmsg_repeated_strings *recv_msg = NULL;

    ret = cmsg_test_api_empty_msg_rpc_test (client, &recv_msg);

    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    NP_ASSERT_NOT_NULL (recv_msg);
    NP_ASSERT_EQUAL (recv_msg->n_strings, 0);

    CMSG_FREE_RECV_MSG (recv_msg);
}

static void
run_client_server_tests_empty_msg (cmsg_transport_type type)
{
    cmsg_client *client = NULL;

    if (type != CMSG_TRANSPORT_LOOPBACK)
    {
        create_server_and_wait (type, AF_INET);
    }

    client = create_client (type, AF_INET);

    _run_client_server_tests_empty_msg (client);

    if (type != CMSG_TRANSPORT_LOOPBACK)
    {
        stop_server_and_wait ();
    }
    cmsg_destroy_client_and_transport (client);
}

/**
 * Run the empty msg client <-> server test case with a TCP transport.
 */
void
test_client_server_rpc_tcp_empty_msg (void)
{
    run_client_server_tests_empty_msg (CMSG_TRANSPORT_RPC_TCP);
}

/**
 * Run the empty msg client <-> server test case with a TIPC transport.
 */
void
test_client_server_rpc_tipc_empty_msg (void)
{
    run_client_server_tests_empty_msg (CMSG_TRANSPORT_RPC_TIPC);
}

/**
 * Run the empty msg client <-> server test case with a UNIX transport.
 */
void
test_client_server_rpc_unix_empty_msg (void)
{
    run_client_server_tests_empty_msg (CMSG_TRANSPORT_RPC_UNIX);
}

/**
 * Run the empty msg client <-> server test case with a LOOPBACK transport.
 */
void
test_client_server_rpc_loopback_empty_msg (void)
{
    run_client_server_tests_empty_msg (CMSG_TRANSPORT_LOOPBACK);
}

/**
 * Run the empty msg client <-> server test case with a UDT (TCP) transport.
 */
void
test_client_server_rpc_udt_empty_msg (void)
{
    run_client_server_tests_empty_msg (CMSG_TRANSPORT_RPC_USERDEFINED);
}
