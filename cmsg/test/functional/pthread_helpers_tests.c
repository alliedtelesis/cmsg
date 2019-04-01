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

extern int32_t cmsg_sub_unsubscribe (cmsg_sub *subscriber,
                                     cmsg_transport *sub_client_transport,
                                     char *method_name);

/**
 * This informs the compiler that the function is, in fact, being used even though it
 * doesn't look like it. This is useful for static functions that get found by NovaProva
 * using debug symbols.
 */
#define USED __attribute__ ((used))

static const uint16_t tipc_publisher_port = 18888;
static const uint16_t tipc_subscriber_port = 18889;
static const uint16_t tipc_instance = 1;
static const uint16_t tipc_scope = TIPC_NODE_SCOPE;

static bool notification_received;

#define NUM_CLIENT_THREADS 32
#define NUM_SENT_MESSAGES 20
static uint32_t client_threads = 0;

static int
sm_mock_cmsg_service_port_get (const char *name, const char *proto)
{
    if ((strcmp (name, "cmsg-test-publisher") == 0) && (strcmp (proto, "tipc") == 0))
    {
        return tipc_publisher_port;
    }
    if ((strcmp (name, "cmsg-test-subscriber") == 0) && (strcmp (proto, "tipc") == 0))
    {
        return tipc_subscriber_port;
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
    np_mock (cmsg_service_port_get, sm_mock_cmsg_service_port_get);

    /* Ignore SIGPIPE signal if it occurs */
    signal (SIGPIPE, SIG_IGN);

    notification_received = false;

    cmsg_service_listener_mock_functions ();
    cmsg_pss_mock_functions ();

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
cmsg_test_impl_pthread_notification_test (const void *service,
                                          const cmsg_uint32_msg *recv_msg)
{
    NP_ASSERT_EQUAL (recv_msg->value, 10);

    notification_received = true;

    cmsg_test_server_pthread_notification_testSend (service);
}

static void
send_notification (cmsg_pub *pub)
{
    cmsg_uint32_msg send_msg = CMSG_UINT32_MSG_INIT;
    int ret = 0;

    CMSG_SET_FIELD_VALUE (&send_msg, value, 10);

    ret = cmsg_test_api_pthread_notification_test ((void *) pub, &send_msg);

    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
}

/**
 * Run a basic RPC test with a server created using 'cmsg_pthread_server_init'.
 */
void
_test_cmsg_pthread_publisher_subscriber (cmsg_pub *pub, cmsg_sub *sub,
                                         cmsg_transport *transport_r,
                                         pthread_t *publisher_thread,
                                         pthread_t *subscriber_thread)
{
    int ret = 0;

    send_notification (pub);

    /* Give some time for the notification to be received. */
    sleep (1);

    /* Unsubscribe - This fixes a socket leak found by valgrind, somehow... */
    cmsg_sub_unsubscribe (sub, transport_r, "pthread_notification_test");
    cmsg_transport_destroy (transport_r);

    ret = pthread_cancel (*subscriber_thread);
    NP_ASSERT_EQUAL (ret, 0);
    ret = pthread_join (*subscriber_thread, NULL);
    NP_ASSERT_EQUAL (ret, 0);
    cmsg_destroy_subscriber_and_transport (sub);

    ret = pthread_cancel (*publisher_thread);
    NP_ASSERT_EQUAL (ret, 0);
    ret = pthread_join (*publisher_thread, NULL);
    NP_ASSERT_EQUAL (ret, 0);
    cmsg_destroy_publisher_and_transport (pub);

    NP_ASSERT_TRUE (notification_received);
}

void
test_cmsg_pthread_publisher_subscriber_unix (void)
{
    cmsg_pub *pub = NULL;
    cmsg_sub *sub = NULL;
    cmsg_transport *transport_r = NULL;
    pthread_t subscriber_thread;
    pthread_t publisher_thread;
    const char *events[] = { "pthread_notification_test", NULL };

    pub = cmsg_pthread_unix_publisher_init (&publisher_thread,
                                            CMSG_DESCRIPTOR (cmsg, test));
    sub = cmsg_pthread_unix_subscriber_init (&subscriber_thread,
                                             CMSG_SERVICE (cmsg, test), events);
    transport_r = cmsg_create_transport_unix (CMSG_DESCRIPTOR (cmsg, test),
                                              CMSG_TRANSPORT_RPC_UNIX);

    _test_cmsg_pthread_publisher_subscriber (pub, sub, transport_r, &publisher_thread,
                                             &subscriber_thread);
}

void
test_cmsg_pthread_publisher_subscriber_tipc (void)
{
    cmsg_pub *pub = NULL;
    cmsg_sub *sub = NULL;
    pthread_t subscriber_thread;
    pthread_t publisher_thread;
    cmsg_transport *transport_r = NULL;
    const char *events[] = { "pthread_notification_test", NULL };
    struct in_addr remote_addr;

    pub = cmsg_pthread_tipc_publisher_init (&publisher_thread,
                                            CMSG_DESCRIPTOR (cmsg, test),
                                            "cmsg-test-publisher", tipc_instance,
                                            tipc_scope);
    sub = cmsg_pthread_tipc_subscriber_init (&subscriber_thread,
                                             CMSG_SERVICE (cmsg, test), events,
                                             "cmsg-test-subscriber", "cmsg-test-publisher",
                                             tipc_instance, tipc_scope, remote_addr);
    transport_r = cmsg_create_transport_tipc_rpc ("cmsg-test-publisher", tipc_instance,
                                                  tipc_scope);

    _test_cmsg_pthread_publisher_subscriber (pub, sub, transport_r, &publisher_thread,
                                             &subscriber_thread);
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
 * Test the operation of a CMSG server running in multi-threaded mode.
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
