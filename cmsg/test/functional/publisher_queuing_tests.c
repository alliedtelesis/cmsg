/*
 * Functional tests for publisher side queuing.
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

typedef struct recv_counters_s
{
    uint32_t recv_test_1;
    uint32_t recv_test_2;
    uint32_t recv_test_3;
} recv_counters_s;

static recv_counters_s counters[3] = {
    { 0, 0, 0 },
    { 0, 0, 0 },
    { 0, 0, 0 },
};

static bool threads_run = true;

static pthread_t publisher_thread;
static pthread_t subscriber1_thread;
static pthread_t subscriber2_thread;
static pthread_t subscriber3_thread;

static bool publisher_ready = false;
static bool subscriber1_ready = false;
static bool subscriber2_ready = false;
static bool subscriber3_ready = false;

static cmsg_transport *publisher_transport = NULL;
static cmsg_pub *publisher = NULL;

static cmsg_transport *pub_transport_1 = NULL;
static cmsg_transport *sub_transport_1 = NULL;
static cmsg_sub *subscriber_1 = NULL;
static cmsg_transport *pub_transport_2 = NULL;
static cmsg_transport *sub_transport_2 = NULL;
static cmsg_sub *subscriber_2 = NULL;
static cmsg_transport *pub_transport_3 = NULL;
static cmsg_transport *sub_transport_3 = NULL;
static cmsg_sub *subscriber_3 = NULL;

/**
 * Common functionality to run before each test case.
 */
static int USED
set_up (void)
{
    publisher_ready = false;
    subscriber1_ready = false;
    subscriber2_ready = false;
    subscriber3_ready = false;
    threads_run = true;

    /* Sometimes the publisher fails to connect to the subscriber on the first
     * try however retries and eventually can send the notification to the subscriber.
     * A debug syslog is logged however and this causes the test to fail. For now simply
     * ignore all syslog. */
    np_syslog_ignore (".*");

    /* Ignore SIGPIPE signal if it occurs */
    signal (SIGPIPE, SIG_IGN);

    cmsg_service_listener_mock_functions ();

    return 0;
}

/**
 * Get the counter structure for a given subscriber to record
 * what notifications it has successfully received.
 */
static recv_counters_s *
get_subscriber_counters (cmsg_server *server)
{
    if (server == subscriber_1->pub_server)
    {
        return &counters[0];
    }
    else if (server == subscriber_2->pub_server)
    {
        return &counters[1];
    }
    else if (server == subscriber_3->pub_server)
    {
        return &counters[2];
    }

    return NULL;
}

void
cmsg_test_impl_simple_pub_queue_test_1 (const void *service,
                                        const cmsg_uint32_msg *recv_msg)
{
    void *_closure_data = ((const cmsg_server_closure_info *) service)->closure_data;
    cmsg_server_closure_data *closure_data = (cmsg_server_closure_data *) _closure_data;
    recv_counters_s *counters = get_subscriber_counters (closure_data->server);

    counters->recv_test_1++;

    cmsg_test_server_simple_pub_queue_test_1Send (service);
}

void
cmsg_test_impl_simple_pub_queue_test_2 (const void *service,
                                        const cmsg_uint32_msg *recv_msg)
{
    void *_closure_data = ((const cmsg_server_closure_info *) service)->closure_data;
    cmsg_server_closure_data *closure_data = (cmsg_server_closure_data *) _closure_data;

    recv_counters_s *counters = get_subscriber_counters (closure_data->server);
    counters->recv_test_2++;

    cmsg_test_server_simple_pub_queue_test_2Send (service);
}

void
cmsg_test_impl_simple_pub_queue_test_3 (const void *service,
                                        const cmsg_uint32_msg *recv_msg)
{
    void *_closure_data = ((const cmsg_server_closure_info *) service)->closure_data;
    cmsg_server_closure_data *closure_data = (cmsg_server_closure_data *) _closure_data;

    recv_counters_s *counters = get_subscriber_counters (closure_data->server);
    counters->recv_test_3++;

    cmsg_test_server_simple_pub_queue_test_3Send (service);
}

/**
 * Publisher processing function that should be run in a new thread.
 * Simply polls the server for any subscription requests.
 */
static void *
publisher_thread_process (void *unused)
{
    int fd = cmsg_pub_get_server_socket (publisher);
    int fd_max = fd + 1;

    fd_set readfds;
    FD_ZERO (&readfds);
    FD_SET (fd, &readfds);

    publisher_ready = true;

    while (threads_run)
    {
        cmsg_publisher_receive_poll (publisher, 1000, &readfds, &fd_max);
    }

    for (fd = 0; fd <= fd_max; fd++)
    {
        if (FD_ISSET (fd, &readfds))
        {
            close (fd);
        }
    }

    return 0;
}

/**
 * Create a new thread to run the publisher in.
 */
static void
create_publisher_thread (void)
{
    int ret = 0;
    ret = pthread_create (&publisher_thread, NULL, &publisher_thread_process, NULL);
    NP_ASSERT_EQUAL (ret, 0);
}

/**
 * Signal that a given subscriber is ready to run the tests.
 */
static void
set_subscriber_ready (cmsg_sub *subscriber)
{
    if (subscriber == subscriber_1)
    {
        subscriber1_ready = true;
    }
    else if (subscriber == subscriber_2)
    {
        subscriber2_ready = true;
    }
    else if (subscriber == subscriber_3)
    {
        subscriber3_ready = true;
    }
}

/**
 * Subscriber processing function that should be run in a new thread.
 * Simply polls the server for any notifications sent from the publisher.
 */
static void *
subscriber_thread_process (void *arg)
{
    cmsg_sub *subscriber = (cmsg_sub *) arg;
    int fd = cmsg_sub_get_server_socket (subscriber);
    int fd_max = fd + 1;

    fd_set readfds;
    FD_ZERO (&readfds);
    FD_SET (fd, &readfds);

    set_subscriber_ready (subscriber);

    while (threads_run)
    {
        cmsg_sub_server_receive_poll (subscriber, 1000, &readfds, &fd_max);
    }

    for (fd = 0; fd <= fd_max; fd++)
    {
        if (FD_ISSET (fd, &readfds))
        {
            close (fd);
        }
    }

    return 0;
}

/**
 * Create a new thread to run the given subscriber in.
 */
static void
create_subscriber_thread (pthread_t *subscriber_thread, cmsg_sub *subscriber)
{
    int ret = 0;
    ret = pthread_create (subscriber_thread, NULL, &subscriber_thread_process,
                          (void *) subscriber);
    NP_ASSERT_EQUAL (ret, 0);
}

/**
 * Create the publisher used for this test.
 */
void
create_publisher (void)
{
    publisher_transport = cmsg_create_transport_unix (CMSG_DESCRIPTOR (cmsg, test),
                                                      CMSG_TRANSPORT_RPC_UNIX);
    publisher = cmsg_pub_new (publisher_transport, CMSG_DESCRIPTOR (cmsg, test));
    NP_ASSERT_NOT_NULL (publisher);
}

/**
 * Clean up the memory and associated structures of a given publisher.
 */
void
cleanup_publisher (cmsg_pub *publisher)
{
    cmsg_destroy_publisher_and_transport (publisher);
}

/**
 * Create a given subscriber used for this test.
 */
void
create_subscriber (cmsg_transport **pub_transport, cmsg_transport **sub_transport,
                   cmsg_sub **subscriber, const char *unix_path)
{
    *pub_transport = cmsg_create_transport_unix (CMSG_DESCRIPTOR (cmsg, test),
                                                 CMSG_TRANSPORT_RPC_UNIX);
    NP_ASSERT_NOT_NULL (*pub_transport);

    *sub_transport = cmsg_transport_new (CMSG_TRANSPORT_ONEWAY_UNIX);
    NP_ASSERT_NOT_NULL (*sub_transport);
    (*sub_transport)->config.socket.family = AF_UNIX;
    (*sub_transport)->config.socket.sockaddr.un.sun_family = AF_UNIX;
    strncpy ((*sub_transport)->config.socket.sockaddr.un.sun_path, unix_path,
             sizeof ((*sub_transport)->config.socket.sockaddr.un.sun_path) - 1);

    *subscriber = cmsg_sub_new (*sub_transport, CMSG_SERVICE (cmsg, test));
    NP_ASSERT_NOT_NULL (*subscriber);
}

/**
 * Clean up the memory and associated structures of a given subscriber.
 */
void
cleanup_subscriber (cmsg_transport *pub_transport, cmsg_sub *subscriber)
{
    cmsg_destroy_subscriber_and_transport (subscriber);
    close (pub_transport->socket);
    cmsg_transport_destroy (pub_transport);
}

/**
 * Wait for the publisher and subscriber threads to be ready
 * to run the tests.
 */
void
wait_for_pub_and_sub_threads_ready (void)
{
    while (!publisher_ready || !subscriber1_ready || !subscriber2_ready ||
           !subscriber3_ready)
    {
        usleep (100000);
    }
}

/**
 * Wait for the publisher and subscriber threads to exit.
 */
static void
wait_for_threads_to_exit (void)
{
    pthread_join (publisher_thread, NULL);
    pthread_join (subscriber1_thread, NULL);
    pthread_join (subscriber2_thread, NULL);
    pthread_join (subscriber3_thread, NULL);
}

static void
queuing_tests_init (void)
{
    create_publisher ();
    create_subscriber (&pub_transport_1, &sub_transport_1, &subscriber_1,
                       "/tmp/unix_test_path1");
    create_subscriber (&pub_transport_2, &sub_transport_2, &subscriber_2,
                       "/tmp/unix_test_path2");
    create_subscriber (&pub_transport_3, &sub_transport_3, &subscriber_3,
                       "/tmp/unix_test_path3");

    create_publisher_thread ();
    create_subscriber_thread (&subscriber1_thread, subscriber_1);
    create_subscriber_thread (&subscriber2_thread, subscriber_2);
    create_subscriber_thread (&subscriber3_thread, subscriber_3);

    wait_for_pub_and_sub_threads_ready ();
}

static void
queuing_tests_deinit (void)
{
    threads_run = false;
    wait_for_threads_to_exit ();

    cleanup_publisher (publisher);
    cleanup_subscriber (pub_transport_1, subscriber_1);
    cleanup_subscriber (pub_transport_2, subscriber_2);
    cleanup_subscriber (pub_transport_3, subscriber_3);
}

static void
run_no_queuing_test (void)
{
    int ret;
    cmsg_uint32_msg send_msg = CMSG_UINT32_MSG_INIT;

    ret = cmsg_sub_subscribe (subscriber_1, pub_transport_1, "simple_pub_queue_test_1");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    ret = cmsg_sub_subscribe (subscriber_1, pub_transport_1, "simple_pub_queue_test_2");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    ret = cmsg_sub_subscribe (subscriber_1, pub_transport_1, "simple_pub_queue_test_3");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    ret = cmsg_sub_subscribe (subscriber_2, pub_transport_2, "simple_pub_queue_test_1");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    ret = cmsg_sub_subscribe (subscriber_2, pub_transport_2, "simple_pub_queue_test_2");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    ret = cmsg_sub_subscribe (subscriber_2, pub_transport_2, "simple_pub_queue_test_3");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    ret = cmsg_sub_subscribe (subscriber_3, pub_transport_3, "simple_pub_queue_test_1");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    ret = cmsg_sub_subscribe (subscriber_3, pub_transport_3, "simple_pub_queue_test_2");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    ret = cmsg_sub_subscribe (subscriber_3, pub_transport_3, "simple_pub_queue_test_3");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    sleep (1);
    NP_ASSERT_EQUAL (publisher->subscriber_count, 9);

    ret = cmsg_test_api_simple_pub_queue_test_1 ((cmsg_client *) publisher, &send_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    ret = cmsg_test_api_simple_pub_queue_test_2 ((cmsg_client *) publisher, &send_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    ret = cmsg_test_api_simple_pub_queue_test_3 ((cmsg_client *) publisher, &send_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    sleep (1);

    NP_ASSERT_EQUAL (counters[0].recv_test_1, 1);
    NP_ASSERT_EQUAL (counters[0].recv_test_2, 1);
    NP_ASSERT_EQUAL (counters[0].recv_test_3, 1);
    NP_ASSERT_EQUAL (counters[1].recv_test_1, 1);
    NP_ASSERT_EQUAL (counters[1].recv_test_2, 1);
    NP_ASSERT_EQUAL (counters[1].recv_test_3, 1);
    NP_ASSERT_EQUAL (counters[2].recv_test_1, 1);
    NP_ASSERT_EQUAL (counters[2].recv_test_2, 1);
    NP_ASSERT_EQUAL (counters[2].recv_test_3, 1);
}

/**
 * Test that a publisher with no queuing functions as expected.
 */
void
test_publisher_subscriber_queuing__no_queuing (void)
{
    queuing_tests_init ();
    run_no_queuing_test ();
    queuing_tests_deinit ();
}

static void
run_drop_all_test (void)
{
    int ret;
    cmsg_uint32_msg send_msg = CMSG_UINT32_MSG_INIT;

    ret = cmsg_sub_subscribe (subscriber_1, pub_transport_1, "simple_pub_queue_test_1");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    ret = cmsg_sub_subscribe (subscriber_1, pub_transport_1, "simple_pub_queue_test_2");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    ret = cmsg_sub_subscribe (subscriber_1, pub_transport_1, "simple_pub_queue_test_3");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    ret = cmsg_sub_subscribe (subscriber_2, pub_transport_2, "simple_pub_queue_test_1");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    ret = cmsg_sub_subscribe (subscriber_2, pub_transport_2, "simple_pub_queue_test_2");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    ret = cmsg_sub_subscribe (subscriber_2, pub_transport_2, "simple_pub_queue_test_3");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    ret = cmsg_sub_subscribe (subscriber_3, pub_transport_3, "simple_pub_queue_test_1");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    ret = cmsg_sub_subscribe (subscriber_3, pub_transport_3, "simple_pub_queue_test_2");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    ret = cmsg_sub_subscribe (subscriber_3, pub_transport_3, "simple_pub_queue_test_3");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    sleep (1);
    NP_ASSERT_EQUAL (publisher->subscriber_count, 9);

    cmsg_pub_queue_filter_set_all (publisher, CMSG_QUEUE_FILTER_DROP);

    ret = cmsg_test_api_simple_pub_queue_test_1 ((cmsg_client *) publisher, &send_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    ret = cmsg_test_api_simple_pub_queue_test_2 ((cmsg_client *) publisher, &send_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    ret = cmsg_test_api_simple_pub_queue_test_3 ((cmsg_client *) publisher, &send_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    sleep (1);

    NP_ASSERT_EQUAL (counters[0].recv_test_1, 0);
    NP_ASSERT_EQUAL (counters[0].recv_test_2, 0);
    NP_ASSERT_EQUAL (counters[0].recv_test_3, 0);
    NP_ASSERT_EQUAL (counters[1].recv_test_1, 0);
    NP_ASSERT_EQUAL (counters[1].recv_test_2, 0);
    NP_ASSERT_EQUAL (counters[1].recv_test_3, 0);
    NP_ASSERT_EQUAL (counters[2].recv_test_1, 0);
    NP_ASSERT_EQUAL (counters[2].recv_test_2, 0);
    NP_ASSERT_EQUAL (counters[2].recv_test_3, 0);
}


/**
 * Test that a publisher with a filter to drop all messages functions as expected.
 */
void
test_publisher_subscriber_queuing__drop_all (void)
{
    queuing_tests_init ();
    run_drop_all_test ();
    queuing_tests_deinit ();
}

static void
run_queue_all_test (void)
{
    int ret;
    cmsg_uint32_msg send_msg = CMSG_UINT32_MSG_INIT;

    ret = cmsg_sub_subscribe (subscriber_1, pub_transport_1, "simple_pub_queue_test_1");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    ret = cmsg_sub_subscribe (subscriber_1, pub_transport_1, "simple_pub_queue_test_2");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    ret = cmsg_sub_subscribe (subscriber_1, pub_transport_1, "simple_pub_queue_test_3");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    ret = cmsg_sub_subscribe (subscriber_2, pub_transport_2, "simple_pub_queue_test_1");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    ret = cmsg_sub_subscribe (subscriber_2, pub_transport_2, "simple_pub_queue_test_2");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    ret = cmsg_sub_subscribe (subscriber_2, pub_transport_2, "simple_pub_queue_test_3");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    ret = cmsg_sub_subscribe (subscriber_3, pub_transport_3, "simple_pub_queue_test_1");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    ret = cmsg_sub_subscribe (subscriber_3, pub_transport_3, "simple_pub_queue_test_2");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    ret = cmsg_sub_subscribe (subscriber_3, pub_transport_3, "simple_pub_queue_test_3");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    sleep (1);
    NP_ASSERT_EQUAL (publisher->subscriber_count, 9);

    cmsg_pub_queue_filter_set_all (publisher, CMSG_QUEUE_FILTER_QUEUE);

    ret = cmsg_test_api_simple_pub_queue_test_1 ((cmsg_client *) publisher, &send_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    ret = cmsg_test_api_simple_pub_queue_test_2 ((cmsg_client *) publisher, &send_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    ret = cmsg_test_api_simple_pub_queue_test_3 ((cmsg_client *) publisher, &send_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    sleep (1);

    NP_ASSERT_EQUAL (counters[0].recv_test_1, 0);
    NP_ASSERT_EQUAL (counters[0].recv_test_2, 0);
    NP_ASSERT_EQUAL (counters[0].recv_test_3, 0);
    NP_ASSERT_EQUAL (counters[1].recv_test_1, 0);
    NP_ASSERT_EQUAL (counters[1].recv_test_2, 0);
    NP_ASSERT_EQUAL (counters[1].recv_test_3, 0);
    NP_ASSERT_EQUAL (counters[2].recv_test_1, 0);
    NP_ASSERT_EQUAL (counters[2].recv_test_2, 0);
    NP_ASSERT_EQUAL (counters[2].recv_test_3, 0);

    NP_ASSERT_EQUAL (g_queue_get_length (publisher->queue), 9);

    cmsg_pub_queue_process_all (publisher);

    sleep (1);

    NP_ASSERT_EQUAL (counters[0].recv_test_1, 1);
    NP_ASSERT_EQUAL (counters[0].recv_test_2, 1);
    NP_ASSERT_EQUAL (counters[0].recv_test_3, 1);
    NP_ASSERT_EQUAL (counters[1].recv_test_1, 1);
    NP_ASSERT_EQUAL (counters[1].recv_test_2, 1);
    NP_ASSERT_EQUAL (counters[1].recv_test_3, 1);
    NP_ASSERT_EQUAL (counters[2].recv_test_1, 1);
    NP_ASSERT_EQUAL (counters[2].recv_test_2, 1);
    NP_ASSERT_EQUAL (counters[2].recv_test_3, 1);

    NP_ASSERT_EQUAL (g_queue_get_length (publisher->queue), 0);
}


/**
 * Test that a publisher with a filter to queue all messages functions as expected.
 */
void
test_publisher_subscriber_queuing__queue_all (void)
{
    queuing_tests_init ();
    run_queue_all_test ();
    queuing_tests_deinit ();
}

static void
run_queue_all_and_unsubscribe_test (void)
{
    int ret;
    cmsg_uint32_msg send_msg = CMSG_UINT32_MSG_INIT;

    ret = cmsg_sub_subscribe (subscriber_1, pub_transport_1, "simple_pub_queue_test_1");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    ret = cmsg_sub_subscribe (subscriber_1, pub_transport_1, "simple_pub_queue_test_2");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    ret = cmsg_sub_subscribe (subscriber_1, pub_transport_1, "simple_pub_queue_test_3");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    ret = cmsg_sub_subscribe (subscriber_2, pub_transport_2, "simple_pub_queue_test_1");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    ret = cmsg_sub_subscribe (subscriber_2, pub_transport_2, "simple_pub_queue_test_2");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    ret = cmsg_sub_subscribe (subscriber_2, pub_transport_2, "simple_pub_queue_test_3");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    ret = cmsg_sub_subscribe (subscriber_3, pub_transport_3, "simple_pub_queue_test_1");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    ret = cmsg_sub_subscribe (subscriber_3, pub_transport_3, "simple_pub_queue_test_2");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    ret = cmsg_sub_subscribe (subscriber_3, pub_transport_3, "simple_pub_queue_test_3");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    sleep (1);
    NP_ASSERT_EQUAL (publisher->subscriber_count, 9);

    cmsg_pub_queue_filter_set_all (publisher, CMSG_QUEUE_FILTER_QUEUE);

    ret = cmsg_test_api_simple_pub_queue_test_1 ((cmsg_client *) publisher, &send_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    ret = cmsg_test_api_simple_pub_queue_test_2 ((cmsg_client *) publisher, &send_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    ret = cmsg_test_api_simple_pub_queue_test_3 ((cmsg_client *) publisher, &send_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    sleep (1);

    NP_ASSERT_EQUAL (counters[0].recv_test_1, 0);
    NP_ASSERT_EQUAL (counters[0].recv_test_2, 0);
    NP_ASSERT_EQUAL (counters[0].recv_test_3, 0);
    NP_ASSERT_EQUAL (counters[1].recv_test_1, 0);
    NP_ASSERT_EQUAL (counters[1].recv_test_2, 0);
    NP_ASSERT_EQUAL (counters[1].recv_test_3, 0);
    NP_ASSERT_EQUAL (counters[2].recv_test_1, 0);
    NP_ASSERT_EQUAL (counters[2].recv_test_2, 0);
    NP_ASSERT_EQUAL (counters[2].recv_test_3, 0);

    NP_ASSERT_EQUAL (g_queue_get_length (publisher->queue), 9);

    ret = cmsg_sub_unsubscribe (subscriber_3, pub_transport_3, "simple_pub_queue_test_2");
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    sleep (1);

    NP_ASSERT_EQUAL (g_queue_get_length (publisher->queue), 8);
    cmsg_pub_queue_process_all (publisher);

    sleep (1);

    NP_ASSERT_EQUAL (counters[0].recv_test_1, 1);
    NP_ASSERT_EQUAL (counters[0].recv_test_2, 1);
    NP_ASSERT_EQUAL (counters[0].recv_test_3, 1);
    NP_ASSERT_EQUAL (counters[1].recv_test_1, 1);
    NP_ASSERT_EQUAL (counters[1].recv_test_2, 1);
    NP_ASSERT_EQUAL (counters[1].recv_test_3, 1);
    NP_ASSERT_EQUAL (counters[2].recv_test_1, 1);
    NP_ASSERT_EQUAL (counters[2].recv_test_2, 0);
    NP_ASSERT_EQUAL (counters[2].recv_test_3, 1);

    NP_ASSERT_EQUAL (g_queue_get_length (publisher->queue), 0);
}

/**
 * Test that a publisher with a filter to queue all messages functions as expected
 * when a subscriber unsubscribes from a message while it is queued.
 */
void
test_publisher_subscriber_queuing__queue_all_and_unsubscribe (void)
{
    queuing_tests_init ();
    run_queue_all_and_unsubscribe_test ();
    queuing_tests_deinit ();
}
