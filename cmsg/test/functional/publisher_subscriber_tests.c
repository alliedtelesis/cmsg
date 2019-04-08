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

/**
 * This informs the compiler that the function is, in fact, being used even though it
 * doesn't look like it. This is useful for static functions that get found by NovaProva
 * using debug symbols.
 */
#define USED __attribute__ ((used))

/* In microseconds. */
#define CMSG_PSD_WAIT_TIME (500 * 1000)

static const uint16_t subscriber_port = 18889;

static const uint16_t tipc_instance = 1;
static const uint16_t tipc_scope = TIPC_NODE_SCOPE;

static bool subscriber_run = true;

static int
sm_mock_cmsg_service_port_get (const char *name, const char *proto)
{
    if (strcmp (name, "cmsg-test-subscriber") == 0)
    {
        return subscriber_port;
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
 * Create the publisher and publish the test notification.
 */
static void
create_publisher_and_send (void)
{
    cmsg_uint32_msg send_msg = CMSG_UINT32_MSG_INIT;
    cmsg_publisher *publisher = cmsg_publisher_create (CMSG_DESCRIPTOR (cmsg, test));
    int ret;

    CMSG_SET_FIELD_VALUE (&send_msg, value, 10);

    ret = cmsg_test_api_simple_notification_test ((cmsg_client *) publisher, &send_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    cmsg_publisher_destroy (publisher);
}

/**
 * Create the subsriber of the given type and then run the
 * functional tests.
 *
 * @param type - Transport type of the subscriber to create
 */
static void
create_subscriber_and_test (cmsg_transport_type type)
{
    cmsg_subscriber *sub = NULL;
    int ret = 0;
    struct in_addr addr;

    switch (type)
    {
    case CMSG_TRANSPORT_RPC_TCP:
        addr.s_addr = htonl (INADDR_LOOPBACK);
        sub = cmsg_subscriber_create_tcp ("cmsg-test-subscriber", addr,
                                          CMSG_SERVICE (cmsg, test));
        break;
    case CMSG_TRANSPORT_RPC_TIPC:
        sub = cmsg_subscriber_create_tipc ("cmsg-test-subscriber", tipc_instance,
                                           tipc_scope, CMSG_SERVICE (cmsg, test));
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

    int fd = cmsg_sub_get_server_socket (sub);
    int fd_max = fd + 1;

    fd_set readfds;
    FD_ZERO (&readfds);
    FD_SET (fd, &readfds);

    create_publisher_and_send ();

    while (subscriber_run)
    {
        cmsg_sub_server_receive_poll (sub, 1000, &readfds, &fd_max);
    }

    // Close accepted sockets before destroying subscriber
    for (fd = 0; fd <= fd_max; fd++)
    {
        if (FD_ISSET (fd, &readfds))
        {
            close (fd);
        }
    }

    cmsg_subscriber_destroy (sub);
}

/**
 * Run the publisher <-> subscriber test case with a TIPC transport.
 */
void
test_publisher_subscriber_tipc (void)
{
    create_subscriber_and_test (CMSG_TRANSPORT_RPC_TIPC);
}

/**
 * Run the publisher <-> subscriber test case with a TCP transport.
 */
void
test_publisher_subscriber_tcp (void)
{
    create_subscriber_and_test (CMSG_TRANSPORT_RPC_TCP);
}

/**
 * Run the publisher <-> subscriber test case with a UNIX transport.
 */
void
test_publisher_subscriber_unix (void)
{
    create_subscriber_and_test (CMSG_TRANSPORT_RPC_UNIX);
}
