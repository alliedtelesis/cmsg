/*
 * Functional tests for client <-> server RPC (two-way) communication.
 *
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */

#include <arpa/inet.h>
#include <np.h>
#include <stdint.h>
#include "cmsg_functional_tests_api_auto.h"
#include "cmsg_functional_tests_impl_auto.h"
#include "setup.h"

#define STRING_ARRAY_LENGTH         100
#define TEST_STRING                 "The quick brown fox jumps over the lazy dog"

static cmsg_server *server = NULL;
static pthread_t server_thread;

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

static void
run_client_server_tests (cmsg_transport_type type, int family, void func (cmsg_client *))
{
    cmsg_client *client = NULL;

    if (type != CMSG_TRANSPORT_LOOPBACK)
    {
        server = create_server (type, family, &server_thread);
    }

    client = create_client (type, family);

    func (client);

    if (type != CMSG_TRANSPORT_LOOPBACK)
    {
        pthread_cancel (server_thread);
        pthread_join (server_thread, NULL);
        cmsg_destroy_server_and_transport (server);
        server = NULL;
    }
    cmsg_destroy_client_and_transport (client);
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

/**
 * Run the simple client <-> server test case with a TCP transport (IPv4).
 */
void
test_client_server_rpc_tcp (void)
{
    run_client_server_tests (CMSG_TRANSPORT_RPC_TCP, AF_INET, _run_client_server_tests);
}

/**
 * Run the simple client <-> server test case with a TCP transport (IPv6).
 */
void
test_client_server_rpc_tcp6 (void)
{
    run_client_server_tests (CMSG_TRANSPORT_RPC_TCP, AF_INET6, _run_client_server_tests);
}

/**
 * Run the simple client <-> server test case with a UNIX transport.
 */
void
test_client_server_rpc_unix (void)
{
    run_client_server_tests (CMSG_TRANSPORT_RPC_UNIX, AF_UNSPEC, _run_client_server_tests);
}

/**
 * Run the simple client <-> server test case with a LOOPBACK transport.
 */
void
test_client_server_rpc_loopback (void)
{
    run_client_server_tests (CMSG_TRANSPORT_LOOPBACK, AF_UNSPEC, _run_client_server_tests);
}

/**
 * Run the simple client <-> server test case with a UDT (TCP) transport.
 */
void
test_client_server_rpc_udt (void)
{
    run_client_server_tests (CMSG_TRANSPORT_RPC_USERDEFINED, AF_UNSPEC,
                             _run_client_server_tests);
}

/**
 * Run the BIG client <-> server test case with a TCP transport.
 */
void
test_client_server_rpc_tcp_big (void)
{
    run_client_server_tests (CMSG_TRANSPORT_RPC_TCP, AF_INET, _run_client_server_tests_big);
}

/**
 * Run the BIG client <-> server test case with a UNIX transport.
 */
void
test_client_server_rpc_unix_big (void)
{
    run_client_server_tests (CMSG_TRANSPORT_RPC_UNIX, AF_UNSPEC,
                             _run_client_server_tests_big);
}

/**
 * Run the BIG client <-> server test case with a LOOPBACK transport.
 */
void
test_client_server_rpc_loopback_big (void)
{
    run_client_server_tests (CMSG_TRANSPORT_LOOPBACK, AF_UNSPEC,
                             _run_client_server_tests_big);
}

/**
 * Run the BIG client <-> server test case with a UDT (TCP) transport.
 */
void
test_client_server_rpc_udt_big (void)
{
    run_client_server_tests (CMSG_TRANSPORT_RPC_USERDEFINED, AF_UNSPEC,
                             _run_client_server_tests_big);
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

/**
 * Run the empty msg client <-> server test case with a TCP transport.
 */
void
test_client_server_rpc_tcp_empty_msg (void)
{
    run_client_server_tests (CMSG_TRANSPORT_RPC_TCP, AF_INET,
                             _run_client_server_tests_empty_msg);
}

/**
 * Run the empty msg client <-> server test case with a UNIX transport.
 */
void
test_client_server_rpc_unix_empty_msg (void)
{
    run_client_server_tests (CMSG_TRANSPORT_RPC_UNIX, AF_UNSPEC,
                             _run_client_server_tests_empty_msg);
}

/**
 * Run the empty msg client <-> server test case with a LOOPBACK transport.
 */
void
test_client_server_rpc_loopback_empty_msg (void)
{
    run_client_server_tests (CMSG_TRANSPORT_LOOPBACK, AF_UNSPEC,
                             _run_client_server_tests_empty_msg);
}

/**
 * Run the empty msg client <-> server test case with a UDT (TCP) transport.
 */
void
test_client_server_rpc_udt_empty_msg (void)
{
    run_client_server_tests (CMSG_TRANSPORT_RPC_USERDEFINED, AF_UNSPEC,
                             _run_client_server_tests_empty_msg);
}
