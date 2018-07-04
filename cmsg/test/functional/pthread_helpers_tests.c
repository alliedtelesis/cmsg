/*
 * Functional tests for client <-> server RPC (two-way) communication.
 *
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */

#include <np.h>
#include <stdint.h>
#include <cmsg_pthread_helpers.h>
#include "cmsg_functional_tests_api_auto.h"
#include "cmsg_functional_tests_impl_auto.h"

/**
 * This informs the compiler that the function is, in fact, being used even though it
 * doesn't look like it. This is useful for static functions that get found by NovaProva
 * using debug symbols.
 */
#define USED __attribute__ ((used))

static pthread_t server_thread;

/**
 * Common functionality to run before each test case.
 */
static int USED
set_up (void)
{
    /* Ignore SIGPIPE signal if it occurs */
    signal (SIGPIPE, SIG_IGN);
    return 0;
}

/**
 * Call a CMSG API to confirm the server created with 'cmsg_pthread_server_init'
 * is running and functioning as expected.
 */
static void
call_api (void)
{
    int ret = 0;
    cmsg_bool_msg send_msg = CMSG_BOOL_MSG_INIT;
    cmsg_bool_msg *recv_msg = NULL;
    cmsg_client *client = cmsg_create_client_unix (CMSG_DESCRIPTOR (cmsg, test));

    CMSG_SET_FIELD_VALUE (&send_msg, value, true);

    ret = cmsg_test_api_simple_rpc_test (client, &send_msg, &recv_msg);

    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    NP_ASSERT_NOT_NULL (recv_msg);
    NP_ASSERT_TRUE (recv_msg->value);

    CMSG_FREE_RECV_MSG (recv_msg);
    cmsg_destroy_client_and_transport (client);
}

/**
 * Run a basic RPC test with a server created using 'cmsg_pthread_server_init'.
 */
void
test_cmsg_pthread_server_init (void)
{
    int ret = 0;
    cmsg_server *server = cmsg_create_server_unix_rpc (CMSG_SERVICE (cmsg, test));
    cmsg_pthread_server_init (&server_thread, server);

    call_api ();

    ret = pthread_cancel (server_thread);
    NP_ASSERT_EQUAL (ret, 0);
    ret = pthread_join (server_thread, NULL);
    NP_ASSERT_EQUAL (ret, 0);

    cmsg_destroy_server_and_transport (server);
}
