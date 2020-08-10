/*
 * Functional tests for server side queuing.
 *
 * Copyright 2020, Allied Telesis Labs New Zealand, Ltd
 */

#include <np.h>
#include <stdint.h>
#include <cmsg_server.h>
#include "cmsg_functional_tests_api_auto.h"
#include "cmsg_functional_tests_impl_auto.h"
#include "setup.h"

/**
 * This informs the compiler that the function is, in fact, being used even though it
 * doesn't look like it. This is useful for static functions that get found by NovaProva
 * using debug symbols.
 */
#define USED __attribute__ ((used))

static const uint16_t tcp_port = 18880;

static cmsg_server *server = NULL;
static bool server_thread_run = true;
static bool server_ready = false;
static pthread_t server_thread;

static uint32_t test_total = 0;

/**
 * Common functionality to run before each test case.
 */
static int USED
set_up (void)
{
    server_ready = false;
    server_thread_run = true;
    test_total = 0;

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

void
cmsg_test_impl_simple_server_queue_test_1 (const void *service,
                                           const cmsg_uint32_msg *recv_msg)
{
    cmsg_bool_msg send_msg = CMSG_BOOL_MSG_INIT;

    NP_ASSERT_EQUAL (recv_msg->value, 1);
    test_total += recv_msg->value;

    cmsg_test_server_simple_server_queue_test_1Send (service, &send_msg);
}

void
cmsg_test_impl_simple_server_queue_test_2 (const void *service,
                                           const cmsg_uint32_msg *recv_msg)
{
    cmsg_bool_msg send_msg = CMSG_BOOL_MSG_INIT;

    NP_ASSERT_EQUAL (recv_msg->value, 2);
    test_total += recv_msg->value;

    cmsg_test_server_simple_server_queue_test_2Send (service, &send_msg);
}

void
cmsg_test_impl_simple_server_queue_test_3 (const void *service,
                                           const cmsg_uint32_msg *recv_msg)
{
    cmsg_bool_msg send_msg = CMSG_BOOL_MSG_INIT;

    NP_ASSERT_EQUAL (recv_msg->value, 3);
    test_total += recv_msg->value;

    cmsg_test_server_simple_server_queue_test_3Send (service, &send_msg);
}

/**
 * Server processing function that should be run in a new thread.
 * Creates a server of given type and then begins polling the server
 * for any received messages. Once the main thread signals the polling
 * to stop the server is destroyed and the thread exits.
 *
 * @param arg - Enum value of the transport type of the server to
 *              create cast to a pointer
 */
static void *
server_thread_process (void *arg)
{
    cmsg_transport_type transport_type = (uintptr_t) arg;
    cmsg_transport *server_transport = NULL;

    switch (transport_type)
    {
    case CMSG_TRANSPORT_RPC_TCP:
        server_transport = cmsg_transport_new (CMSG_TRANSPORT_RPC_TCP);
        server_transport->config.socket.sockaddr.in.sin_addr.s_addr = htonl (INADDR_ANY);
        server_transport->config.socket.sockaddr.in.sin_port = htons (tcp_port);

        server = cmsg_server_new (server_transport, CMSG_SERVICE (cmsg, test));
        break;

    default:
        NP_FAIL;
    }

    int fd = cmsg_server_get_socket (server);
    int fd_max = fd + 1;

    fd_set readfds;
    FD_ZERO (&readfds);
    FD_SET (fd, &readfds);

    server_ready = true;

    while (server_thread_run)
    {
        cmsg_server_receive_poll (server, 1000, &readfds, &fd_max);
    }

    // Close accepted sockets before destroying server
    for (fd = 0; fd <= fd_max; fd++)
    {
        if (FD_ISSET (fd, &readfds))
        {
            close (fd);
        }
    }

    cmsg_destroy_server_and_transport (server);

    server = NULL;

    return 0;
}

/**
 * Create the server used to process the CMSG IMPL functions
 * in a new thread. Once the new thread is created the function
 * waits until the new thread signals that it is ready for processing.
 *
 * @param type - Transport type of the server to create
 */
static void
create_server_and_wait (cmsg_transport_type type)
{
    int ret = 0;
    uintptr_t cast_type = 0;

    cast_type = (uintptr_t) type;
    ret = pthread_create (&server_thread, NULL, &server_thread_process, (void *) cast_type);

    NP_ASSERT_EQUAL (ret, 0);

    while (!server_ready)
    {
        usleep (100000);
    }
}

/**
 * Signal the server in the different thread to stop processing
 * and then wait for the server to be destroyed and the thread
 * to exit.
 */
static void
stop_server_and_wait (void)
{
    server_thread_run = false;
    pthread_join (server_thread, NULL);
}

/**
 * Create the client that will be used to run a functional test.
 *
 * @param type - Transport type of the client to create
 */
static cmsg_client *
create_client (cmsg_transport_type type)
{
    cmsg_transport *transport = NULL;
    cmsg_client *client = NULL;

    switch (type)
    {
    case CMSG_TRANSPORT_RPC_TCP:
        transport = cmsg_transport_new (CMSG_TRANSPORT_RPC_TCP);
        transport->config.socket.sockaddr.in.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
        transport->config.socket.sockaddr.in.sin_port = htons (tcp_port);
        client = cmsg_client_new (transport, CMSG_DESCRIPTOR (cmsg, test));
        break;

    default:
        NP_FAIL;
    }

    return client;
}

/**
 * Run the simple test with a given CMSG client. Assumes the related
 * server has already been created and is ready to process any API
 * requests.
 *
 * @param client - CMSG client to run the simple test with
 */
static void
_run_server_queuing_drop_all_tests (cmsg_client *client)
{
    int ret = 0;
    cmsg_uint32_msg send_msg = CMSG_UINT32_MSG_INIT;
    cmsg_bool_msg *recv_msg = NULL;

    cmsg_server_queue_filter_set_all (server, CMSG_QUEUE_FILTER_DROP);

    CMSG_SET_FIELD_VALUE (&send_msg, value, 1);
    ret = cmsg_test_api_simple_server_queue_test_1 (client, &send_msg, &recv_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_DROPPED);
    CMSG_FREE_RECV_MSG (recv_msg);

    CMSG_SET_FIELD_VALUE (&send_msg, value, 2);
    ret = cmsg_test_api_simple_server_queue_test_2 (client, &send_msg, &recv_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_DROPPED);
    CMSG_FREE_RECV_MSG (recv_msg);

    CMSG_SET_FIELD_VALUE (&send_msg, value, 3);
    ret = cmsg_test_api_simple_server_queue_test_3 (client, &send_msg, &recv_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_DROPPED);
    CMSG_FREE_RECV_MSG (recv_msg);

    sleep (1);
    NP_ASSERT_EQUAL (test_total, 0);
}

static void
_run_server_queuing_drop_specific_tests (cmsg_client *client)
{
    int ret = 0;
    cmsg_uint32_msg send_msg = CMSG_UINT32_MSG_INIT;
    cmsg_bool_msg *recv_msg = NULL;

    cmsg_server_queue_filter_set (server, "simple_server_queue_test_2",
                                  CMSG_QUEUE_FILTER_DROP);

    CMSG_SET_FIELD_VALUE (&send_msg, value, 1);
    ret = cmsg_test_api_simple_server_queue_test_1 (client, &send_msg, &recv_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    CMSG_FREE_RECV_MSG (recv_msg);

    CMSG_SET_FIELD_VALUE (&send_msg, value, 2);
    ret = cmsg_test_api_simple_server_queue_test_2 (client, &send_msg, &recv_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_DROPPED);
    CMSG_FREE_RECV_MSG (recv_msg);

    CMSG_SET_FIELD_VALUE (&send_msg, value, 3);
    ret = cmsg_test_api_simple_server_queue_test_3 (client, &send_msg, &recv_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    CMSG_FREE_RECV_MSG (recv_msg);

    sleep (1);
    NP_ASSERT_EQUAL (test_total, 4);

    cmsg_server_queue_filter_clear (server, "simple_server_queue_test_2");
    CMSG_SET_FIELD_VALUE (&send_msg, value, 2);
    ret = cmsg_test_api_simple_server_queue_test_2 (client, &send_msg, &recv_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    CMSG_FREE_RECV_MSG (recv_msg);

    sleep (1);
    NP_ASSERT_EQUAL (test_total, 6);
}

static void
_run_server_queuing_queue_all_tests (cmsg_client *client)
{
    int ret = 0;
    cmsg_uint32_msg send_msg = CMSG_UINT32_MSG_INIT;
    cmsg_bool_msg *recv_msg = NULL;

    cmsg_server_queue_filter_set_all (server, CMSG_QUEUE_FILTER_QUEUE);

    CMSG_SET_FIELD_VALUE (&send_msg, value, 1);
    ret = cmsg_test_api_simple_server_queue_test_1 (client, &send_msg, &recv_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_QUEUED);
    CMSG_FREE_RECV_MSG (recv_msg);

    CMSG_SET_FIELD_VALUE (&send_msg, value, 2);
    ret = cmsg_test_api_simple_server_queue_test_2 (client, &send_msg, &recv_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_QUEUED);
    CMSG_FREE_RECV_MSG (recv_msg);

    CMSG_SET_FIELD_VALUE (&send_msg, value, 3);
    ret = cmsg_test_api_simple_server_queue_test_3 (client, &send_msg, &recv_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_QUEUED);
    CMSG_FREE_RECV_MSG (recv_msg);

    sleep (1);
    NP_ASSERT_EQUAL (test_total, 0);

    cmsg_server_queue_process_all (server);

    sleep (1);
    NP_ASSERT_EQUAL (test_total, 6);
}

static void
_run_server_queuing_queue_specific_tests (cmsg_client *client)
{
    int ret = 0;
    cmsg_uint32_msg send_msg = CMSG_UINT32_MSG_INIT;
    cmsg_bool_msg *recv_msg = NULL;

    cmsg_server_queue_filter_set (server, "simple_server_queue_test_2",
                                  CMSG_QUEUE_FILTER_QUEUE);

    CMSG_SET_FIELD_VALUE (&send_msg, value, 1);
    ret = cmsg_test_api_simple_server_queue_test_1 (client, &send_msg, &recv_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    CMSG_FREE_RECV_MSG (recv_msg);

    CMSG_SET_FIELD_VALUE (&send_msg, value, 2);
    ret = cmsg_test_api_simple_server_queue_test_2 (client, &send_msg, &recv_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_QUEUED);
    CMSG_FREE_RECV_MSG (recv_msg);

    CMSG_SET_FIELD_VALUE (&send_msg, value, 3);
    ret = cmsg_test_api_simple_server_queue_test_3 (client, &send_msg, &recv_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    CMSG_FREE_RECV_MSG (recv_msg);

    sleep (1);
    NP_ASSERT_EQUAL (test_total, 4);

    cmsg_server_queue_filter_clear (server, "simple_server_queue_test_2");
    CMSG_SET_FIELD_VALUE (&send_msg, value, 2);
    ret = cmsg_test_api_simple_server_queue_test_2 (client, &send_msg, &recv_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    CMSG_FREE_RECV_MSG (recv_msg);

    sleep (1);
    NP_ASSERT_EQUAL (test_total, 6);

    cmsg_server_queue_process_all (server);

    sleep (1);
    NP_ASSERT_EQUAL (test_total, 8);
}

static void
run_server_queuing_tests (cmsg_queue_filter_type queue_type, bool all)
{
    cmsg_client *client = NULL;

    create_server_and_wait (CMSG_TRANSPORT_RPC_TCP);

    client = create_client (CMSG_TRANSPORT_RPC_TCP);

    if (queue_type == CMSG_QUEUE_FILTER_DROP)
    {
        if (all)
        {
            _run_server_queuing_drop_all_tests (client);
        }
        else
        {
            _run_server_queuing_drop_specific_tests (client);
        }
    }
    else if (queue_type == CMSG_QUEUE_FILTER_QUEUE)
    {
        if (all)
        {
            _run_server_queuing_queue_all_tests (client);
        }
        else
        {
            _run_server_queuing_queue_specific_tests (client);
        }
    }
    else
    {
        NP_FAIL;
    }

    stop_server_and_wait ();
    cmsg_destroy_client_and_transport (client);
}

/**
 * Run the simple client <-> server test case with a TCP transport.
 */
void
test_server_queuing_all_drop (void)
{
    run_server_queuing_tests (CMSG_QUEUE_FILTER_DROP, true);
}

void
test_server_queuing_all_queue (void)
{
    run_server_queuing_tests (CMSG_QUEUE_FILTER_QUEUE, true);
}

void
test_server_queuing_specific_drop (void)
{
    run_server_queuing_tests (CMSG_QUEUE_FILTER_DROP, false);
}

void
test_server_queuing_specific_queue (void)
{
    run_server_queuing_tests (CMSG_QUEUE_FILTER_QUEUE, false);
}
