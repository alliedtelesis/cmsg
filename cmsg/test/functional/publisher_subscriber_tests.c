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

/**
 * This informs the compiler that the function is, in fact, being used even though it
 * doesn't look like it. This is useful for static functions that get found by NovaProva
 * using debug symbols.
 */
#define USED __attribute__ ((used))

static const uint16_t tcp_publisher_port = 18888;
static const uint16_t tcp_subscriber_port = 18889;

static const uint16_t tipc_publisher_port = 18888;
static const uint16_t tipc_subscriber_port = 18889;
static const uint16_t tipc_instance = 1;
static const uint16_t tipc_scope = TIPC_NODE_SCOPE;

static const char *unix_sub_path = "/tmp/unix_sub_path";

static cmsg_pub *publisher = NULL;
static bool publisher_thread_run = true;
static bool publisher_ready = false;
static pthread_t publisher_thread;

static bool subscriber_run = true;

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
    publisher_ready = false;
    publisher_thread_run = true;
    subscriber_run = true;

    np_mock (cmsg_service_port_get, sm_mock_cmsg_service_port_get);

    /* Sometimes the publisher fails to connect to the subscriber on the first
     * try however retries and eventually can send the notification to the subscriber.
     * A debug syslog is logged however and this causes the test to fail. For now simply
     * ignore all syslog. */
    np_syslog_ignore (".*");

    /* Ignore SIGPIPE signal if it occurs */
    signal (SIGPIPE, SIG_IGN);

    return 0;
}

/**
 * Common functionality to run at the end of each test case.
 */
static int USED
tear_down (void)
{
    NP_ASSERT_NULL (publisher);

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
 * Publisher processing function that should be run in a new thread.
 * Creates a publisher of given type and then begins polling the server
 * for any subscription requests. Once a subscriber has subscribed to the
 * publisher send a notification before waiting for the subscriber to
 * unsubscribe. The polling is then stopped and the publisher is
 * destroyed before the thread exits.
 *
 * @param arg - Enum value of the transport type of the publisher to
 *              create cast to a pointer
 */
static void *
publisher_thread_process (void *arg)
{
    cmsg_transport_type transport_type = (uintptr_t) arg;
    cmsg_transport *publisher_transport = NULL;

    switch (transport_type)
    {
    case CMSG_TRANSPORT_RPC_TCP:
        publisher_transport = cmsg_transport_new (CMSG_TRANSPORT_RPC_TCP);
        publisher_transport->config.socket.sockaddr.in.sin_addr.s_addr = htonl (INADDR_ANY);
        publisher_transport->config.socket.sockaddr.in.sin_port =
            htons ((unsigned short) tcp_publisher_port);

        publisher = cmsg_pub_new (publisher_transport, CMSG_DESCRIPTOR (cmsg, test));
        break;
    case CMSG_TRANSPORT_RPC_TIPC:
        publisher =
            cmsg_create_publisher_tipc_rpc ("cmsg-test-publisher", tipc_instance,
                                            tipc_scope, CMSG_DESCRIPTOR (cmsg, test));
        break;
    case CMSG_TRANSPORT_RPC_UNIX:
        publisher_transport = cmsg_create_transport_unix (CMSG_DESCRIPTOR (cmsg, test),
                                                          CMSG_TRANSPORT_RPC_UNIX);
        publisher = cmsg_pub_new (publisher_transport, CMSG_DESCRIPTOR (cmsg, test));
        break;
    default:
        NP_FAIL;
    }

    NP_ASSERT_NOT_NULL (publisher);
    int fd = cmsg_pub_get_server_socket (publisher);
    int fd_max = fd + 1;
    int ret;
    bool seen_subsriber = false;

    fd_set readfds;
    FD_ZERO (&readfds);
    FD_SET (fd, &readfds);

    publisher_ready = true;

    while (publisher_thread_run)
    {
        cmsg_publisher_receive_poll (publisher, 1000, &readfds, &fd_max);

        if (publisher->subscriber_count > 0)
        {
            cmsg_uint32_msg send_msg = CMSG_UINT32_MSG_INIT;
            CMSG_SET_FIELD_VALUE (&send_msg, value, 10);

            ret =
                cmsg_test_api_simple_notification_test ((cmsg_client *) publisher,
                                                        &send_msg);
            NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

            seen_subsriber = true;
        }
        else if (publisher->subscriber_count == 0 && seen_subsriber)
        {
            break;
        }
    }

    // Close accepted sockets before destroying publisher
    for (fd = 0; fd <= fd_max; fd++)
    {
        if (FD_ISSET (fd, &readfds))
        {
            close (fd);
        }
    }

    cmsg_destroy_publisher_and_transport (publisher);

    publisher = NULL;

    return 0;
}

/**
 * Create the publisher used to process subscriptions and send notifications
 * in a new thread. Once the new thread is created the function
 * waits until the new thread signals that it is ready for processing.
 *
 * @param type - Transport type of the publisher to create
 */
static void
create_publisher_and_wait (cmsg_transport_type type)
{
    int ret = 0;
    uintptr_t cast_type = 0;

    cast_type = (uintptr_t) type;
    ret =
        pthread_create (&publisher_thread, NULL, &publisher_thread_process,
                        (void *) cast_type);

    NP_ASSERT_EQUAL (ret, 0);

    while (!publisher_ready)
    {
        usleep (100000);
    }
}

/**
 * Wait for the publisher running in a separate thread to exit.
 */
static void
wait_for_publisher_to_exit (void)
{
    pthread_join (publisher_thread, NULL);
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
    cmsg_transport *pub_transport = NULL;
    cmsg_transport *sub_transport = NULL;
    cmsg_sub *subscriber = NULL;
    int ret = 0;

    switch (type)
    {
    case CMSG_TRANSPORT_RPC_TCP:
        pub_transport = cmsg_transport_new (CMSG_TRANSPORT_RPC_TCP);
        NP_ASSERT_NOT_NULL (pub_transport);
        pub_transport->config.socket.sockaddr.in.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
        pub_transport->config.socket.sockaddr.in.sin_port =
            htons ((unsigned short) tcp_publisher_port);

        sub_transport = cmsg_transport_new (CMSG_TRANSPORT_ONEWAY_TCP);
        NP_ASSERT_NOT_NULL (sub_transport);
        sub_transport->config.socket.sockaddr.in.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
        sub_transport->config.socket.sockaddr.in.sin_port =
            htons ((unsigned short) tcp_subscriber_port);

        subscriber = cmsg_sub_new (sub_transport, CMSG_SERVICE (cmsg, test));
        NP_ASSERT_NOT_NULL (subscriber);
        break;
    case CMSG_TRANSPORT_RPC_TIPC:
        subscriber =
            cmsg_create_subscriber_tipc_oneway ("cmsg-test-subscriber", tipc_instance,
                                                tipc_scope, CMSG_SERVICE (cmsg, test));
        NP_ASSERT_NOT_NULL (subscriber);

        pub_transport =
            cmsg_create_transport_tipc_rpc ("cmsg-test-publisher", tipc_instance,
                                            tipc_scope);
        NP_ASSERT_NOT_NULL (pub_transport);
        break;
    case CMSG_TRANSPORT_RPC_UNIX:
        pub_transport = cmsg_create_transport_unix (CMSG_DESCRIPTOR (cmsg, test),
                                                    CMSG_TRANSPORT_RPC_UNIX);
        NP_ASSERT_NOT_NULL (pub_transport);

        sub_transport = cmsg_transport_new (CMSG_TRANSPORT_ONEWAY_UNIX);
        NP_ASSERT_NOT_NULL (sub_transport);
        sub_transport->config.socket.family = AF_UNIX;
        sub_transport->config.socket.sockaddr.un.sun_family = AF_UNIX;
        strncpy (sub_transport->config.socket.sockaddr.un.sun_path, unix_sub_path,
                 sizeof (sub_transport->config.socket.sockaddr.un.sun_path) - 1);

        subscriber = cmsg_sub_new (sub_transport, CMSG_SERVICE (cmsg, test));
        NP_ASSERT_NOT_NULL (subscriber);
        break;
    default:
        NP_FAIL;
    }

    ret = cmsg_sub_subscribe (subscriber, pub_transport, "simple_notification_test");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    int fd = cmsg_sub_get_server_socket (subscriber);
    int fd_max = fd + 1;

    fd_set readfds;
    FD_ZERO (&readfds);
    FD_SET (fd, &readfds);

    while (subscriber_run)
    {
        cmsg_sub_server_receive_poll (subscriber, 1000, &readfds, &fd_max);
    }

    // Close accepted sockets before destroying subscriber
    for (fd = 0; fd <= fd_max; fd++)
    {
        if (FD_ISSET (fd, &readfds))
        {
            close (fd);
        }
    }

    cmsg_sub_unsubscribe (subscriber, pub_transport, "simple_notification_test");

    cmsg_destroy_subscriber_and_transport (subscriber);
    close (pub_transport->socket);
    cmsg_transport_destroy (pub_transport);
}

static void
run_publisher_subscriber_tests (cmsg_transport_type type)
{
    create_publisher_and_wait (type);

    create_subscriber_and_test (type);

    wait_for_publisher_to_exit ();
}

/**
 * Run the publisher <-> subscriber test case with a TIPC transport.
 */
void
test_publisher_subscriber_tipc (void)
{
    run_publisher_subscriber_tests (CMSG_TRANSPORT_RPC_TIPC);
}

/**
 * Run the publisher <-> subscriber test case with a TCP transport.
 */
void
test_publisher_subscriber_tcp (void)
{
    run_publisher_subscriber_tests (CMSG_TRANSPORT_RPC_TCP);
}

/**
 * Run the publisher <-> subscriber test case with a UNIX transport.
 */
void
test_publisher_subscriber_unix (void)
{
    run_publisher_subscriber_tests (CMSG_TRANSPORT_RPC_UNIX);
}
