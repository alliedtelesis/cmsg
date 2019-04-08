/*
 * Functional tests for client <-> server RPC (two-way) communication.
 *
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */

#include <np.h>
#include <stdint.h>
#include <stdlib.h>
#include <cmsg_pthread_helpers.h>
#include "cmsg_functional_tests_api_auto.h"
#include "cmsg_functional_tests_impl_auto.h"
#include "setup.h"

/**
 * This informs the compiler that the function is, in fact, being used even though it
 * doesn't look like it. This is useful for static functions that get found by NovaProva
 * using debug symbols.
 */
#define USED __attribute__ ((used))

static bool notification_received;

#define NUM_CLIENT_THREADS 32
#define NUM_SENT_MESSAGES 20
static uint32_t client_threads = 0;

/**
 * Common functionality to run before each test case.
 */
static int USED
set_up (void)
{
    /* Ignore SIGPIPE signal if it occurs */
    signal (SIGPIPE, SIG_IGN);

    notification_received = false;

    cmsg_service_listener_mock_functions ();
    cmsg_ps_mock_functions ();

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
    pthread_t server_thread;
    cmsg_server *server = cmsg_create_server_unix_rpc (CMSG_SERVICE (cmsg, test));
    cmsg_pthread_server_init (&server_thread, server);

    call_api ();

    ret = pthread_cancel (server_thread);
    NP_ASSERT_EQUAL (ret, 0);
    ret = pthread_join (server_thread, NULL);
    NP_ASSERT_EQUAL (ret, 0);

    cmsg_destroy_server_and_transport (server);
}

void
cmsg_test_impl_server_multi_threading_test (const void *service,
                                            const cmsg_uint32_msg *recv_msg)
{
    cmsg_uint32_msg send_msg = CMSG_UINT32_MSG_INIT;

    /* Sleep between 900-1100us to simulate the API call taking a while to process, to test
     * the situation where multiple calls are being processed by the server simultaneously.
     */
    usleep (900 + rand () % 200);

    CMSG_SET_FIELD_VALUE (&send_msg, value, recv_msg->value);

    cmsg_test_server_server_multi_threading_testSend (service, &send_msg);
}

static void *
client_thread_run (void *value)
{
    uintptr_t sent_value = (uintptr_t) value;
    cmsg_client *client = NULL;
    cmsg_uint32_msg send_msg = CMSG_UINT32_MSG_INIT;
    cmsg_uint32_msg *recv_msg = NULL;
    int i;

    client = cmsg_create_client_unix (CMSG_DESCRIPTOR (cmsg, test));

    CMSG_SET_FIELD_VALUE (&send_msg, value, sent_value);

    for (i = 0; i < NUM_SENT_MESSAGES; i++)
    {
        cmsg_test_api_server_multi_threading_test (client, &send_msg, &recv_msg);
        NP_ASSERT_EQUAL (recv_msg->value, sent_value);
        CMSG_FREE_RECV_MSG (recv_msg);
    }

    cmsg_destroy_client_and_transport (client);

    client_threads--;

    return NULL;
}

/**
 * Test the operation of a CMSG server running in multi-threaded mode."cmsg-test-subscriber"
 * Specifically create NUM_CLIENT_THREADS threads, where each thread
 * will create a client and connect to the server before sending
 * NUM_SENT_MESSAGES messages and testing the received message is as
 * expected.
 */
void
test_cmsg_pthread_multithreaded_server (void)
{
    int ret;
    uintptr_t i;
    pthread_t pid;
    cmsg_pthread_multithreaded_server_info *server_info = NULL;
    cmsg_server *server = NULL;

    server = cmsg_create_server_unix_rpc (CMSG_SERVICE (cmsg, test));
    server_info = cmsg_pthread_multithreaded_server_init (server, 0);

    for (i = 0; i < NUM_CLIENT_THREADS; i++)
    {
        client_threads++;
        ret = pthread_create (&pid, NULL, client_thread_run, (void *) i);
        NP_ASSERT_EQUAL (ret, 0);
    }

    /* Wait for all the child threads to complete */
    while (client_threads != 0)
    {
        usleep (100000);
    }

    cmsg_pthread_multithreaded_server_destroy (server_info);
}
