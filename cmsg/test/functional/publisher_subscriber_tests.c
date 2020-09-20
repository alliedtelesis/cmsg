/*
 * Functional tests for publisher <-> subscriber communication.
 *
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */

#include <np.h>
#include <stdint.h>
#include <cmsg_pub.h>
#include <cmsg_sub.h>
#include "cmsg_functional_tests_api_auto.h"
#include "cmsg_functional_tests_impl_auto.h"
#include "setup.h"
#include "publisher_subscriber/cmsg_ps_api_private.h"
#include "publisher_subscriber/cmsg_pub_private.h"

/* In microseconds. */
#define CMSG_PSD_WAIT_TIME (500 * 1000)

static bool subscriber_run = true;

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

    subscriber_run = true;

    /* cmsg_psd is required for these tests. */
    system ("cmsg_psd &");
    usleep (CMSG_PSD_WAIT_TIME);

    return 0;
}

/**
 * Common functionality to run at the end of each test case.
 */
static int USED
tear_down (void)
{
    system ("pkill cmsg_psd");
    usleep (CMSG_PSD_WAIT_TIME);

    return 0;
}

void
cmsg_test_impl_simple_notification_test (const void *service,
                                         const cmsg_uint32_msg *recv_msg)
{
    NP_ASSERT_EQUAL (recv_msg->value, 10);

    subscriber_run = false;

    cmsg_test_server_simple_notification_testSend (service);
}

/**
 * Publish the test notification.
 */
static void
publish_message (cmsg_publisher *publisher)
{
    cmsg_uint32_msg send_msg = CMSG_UINT32_MSG_INIT;
    int ret;

    CMSG_SET_FIELD_VALUE (&send_msg, value, 10);

    ret = cmsg_test_api_simple_notification_test ((cmsg_client *) publisher, &send_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
}

/**
 * Create the subscriber of the given type, then subscribe for the
 * required events. Finally create the publisher, send the events and
 * check that they were received.
 *
 * @param type - Transport type of the subscriber to create
 */
static void
create_sub_before_pub_and_test (cmsg_transport_type type)
{
    pthread_t subscriber_thread;
    cmsg_subscriber *sub = NULL;
    int ret = 0;
    struct in_addr addr;
    cmsg_publisher *publisher = NULL;

    switch (type)
    {
    case CMSG_TRANSPORT_RPC_TCP:
        addr.s_addr = htonl (INADDR_LOOPBACK);
        sub = cmsg_subscriber_create_tcp ("cmsg-test", addr, NULL,
                                          CMSG_SERVICE (cmsg, test));
        break;
    case CMSG_TRANSPORT_RPC_UNIX:
        sub = cmsg_subscriber_create_unix (CMSG_SERVICE (cmsg, test));
        break;
    default:
        NP_FAIL;
    }

    NP_ASSERT_NOT_NULL (sub);

    ret = cmsg_sub_subscribe_local (sub, "simple_notification_test");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    NP_ASSERT_TRUE (cmsg_pthread_server_init (&subscriber_thread,
                                              cmsg_sub_unix_server_get (sub)));

    publisher = cmsg_publisher_create (CMSG_DESCRIPTOR (cmsg, test));
    NP_ASSERT_NOT_NULL (publisher);

    publish_message (publisher);

    while (subscriber_run)
    {
        usleep (1000);
    }

    pthread_cancel (subscriber_thread);
    pthread_join (subscriber_thread, NULL);
    cmsg_subscriber_destroy (sub);
    cmsg_publisher_destroy (publisher);
}

/**
 * Run the publisher <-> subscriber test case with a TCP transport.
 */
void
test_publisher_subscriber_tcp (void)
{
    create_sub_before_pub_and_test (CMSG_TRANSPORT_RPC_TCP);
}

/**
 * Run the publisher <-> subscriber test case with a UNIX transport.
 */
void
test_publisher_subscriber_unix (void)
{
    create_sub_before_pub_and_test (CMSG_TRANSPORT_RPC_UNIX);
}

/**
 * Test that a publisher is correctly updated when a subscriber is added
 * for a method after the publisher has already been created.
 */
void
test_publisher_receives_subscription_updates (void)
{
    cmsg_publisher *publisher = NULL;
    cmsg_subscriber *sub = NULL;

    publisher = cmsg_publisher_create (CMSG_DESCRIPTOR (cmsg, test));
    NP_ASSERT_EQUAL (g_hash_table_size (publisher->subscribed_methods), 0);

    sub = cmsg_subscriber_create_unix (CMSG_SERVICE (cmsg, test));
    cmsg_sub_subscribe_local (sub, "simple_notification_test");

    NP_ASSERT_EQUAL (g_hash_table_size (publisher->subscribed_methods), 1);

    cmsg_subscriber_destroy (sub);

    NP_ASSERT_EQUAL (g_hash_table_size (publisher->subscribed_methods), 0);

    cmsg_publisher_destroy (publisher);

}
