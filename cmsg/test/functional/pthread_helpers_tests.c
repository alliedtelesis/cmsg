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

static bool notification_received = false;

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
test_cmsg_pthread_publisher_subscriber (void)
{
    int ret = 0;
    const char *events[] = { "pthread_notification_test", NULL };
    pthread_t subscriber_thread;
    pthread_t publisher_thread;
    cmsg_pub *pub = NULL;
    cmsg_sub *sub = NULL;

    pub = cmsg_pthread_unix_publisher_init (&publisher_thread,
                                            CMSG_DESCRIPTOR (cmsg, test));
    sub = cmsg_pthread_unix_subscriber_init (&subscriber_thread,
                                             CMSG_SERVICE (cmsg, test), events);

    send_notification (pub);

    /* Give some time for the notification to be received. */
    sleep (1);

    /* Unsubscribe - This fixes a socket leak found by valgrind, somehow... */
    cmsg_transport *transport_r = cmsg_create_transport_unix (CMSG_DESCRIPTOR (cmsg, test),
                                                              CMSG_TRANSPORT_RPC_UNIX);
    cmsg_sub_unsubscribe (sub, transport_r, "pthread_notification_test");
    cmsg_transport_destroy (transport_r);

    ret = pthread_cancel (subscriber_thread);
    NP_ASSERT_EQUAL (ret, 0);
    ret = pthread_join (subscriber_thread, NULL);
    NP_ASSERT_EQUAL (ret, 0);
    cmsg_destroy_subscriber_and_transport (sub);

    ret = pthread_cancel (publisher_thread);
    NP_ASSERT_EQUAL (ret, 0);
    ret = pthread_join (publisher_thread, NULL);
    NP_ASSERT_EQUAL (ret, 0);
    cmsg_pub_queue_thread_stop (pub);
    cmsg_destroy_publisher_and_transport (pub);

    NP_ASSERT_TRUE (notification_received);
}
