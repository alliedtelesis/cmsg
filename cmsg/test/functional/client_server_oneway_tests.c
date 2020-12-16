/*
 * Functional tests for client <-> server one-way communication.
 *
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */

#include <arpa/inet.h>
#include <np.h>
#include <stdint.h>
#include "cmsg_functional_tests_api_auto.h"
#include "cmsg_functional_tests_impl_auto.h"
#include "setup.h"

static cmsg_server *server = NULL;
static pthread_t server_thread;
static bool message_received = false;

/**
 * Common functionality to run before each test case.
 */
static int USED
set_up (void)
{
    np_mock (cmsg_service_port_get, sm_mock_cmsg_service_port_get);

    /* Ignore SIGPIPE signal if it occurs */
    signal (SIGPIPE, SIG_IGN);

    cmsg_service_listener_mock_functions ();

    message_received = false;

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
    NP_ASSERT_TRUE (recv_msg->value);
    message_received = true;
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

    while (!message_received)
    {
        usleep (1000);
    }
}

static void
run_client_server_tests (cmsg_transport_type type, int family)
{
    cmsg_client *client = NULL;

    server = create_server (type, family, &server_thread);

    client = create_client (type, family);

    _run_client_server_tests (client);

    pthread_cancel (server_thread);
    pthread_join (server_thread, NULL);
    cmsg_destroy_server_and_transport (server);
    server = NULL;
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
    //run_client_server_tests (CMSG_TRANSPORT_BROADCAST, AF_UNSPEC);
}

/**
 * Run the simple client <-> server test case with a UDT (TCP) transport.
 */
void
test_client_server_oneway_udt (void)
{
    run_client_server_tests (CMSG_TRANSPORT_ONEWAY_USERDEFINED, AF_UNSPEC);
}
