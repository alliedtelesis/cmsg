/*
 * Functional tests for the broadcast client functionality.
 *
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */

#include <np.h>
#include <stdint.h>
#include <sys/eventfd.h>
#include "cmsg_broadcast_client.h"
#include "cmsg_server.h"
#include "cmsg_functional_tests_api_auto.h"
#include "cmsg_functional_tests_impl_auto.h"
#include "cmsg_composite_client.h"

/**
 * This informs the compiler that the function is, in fact, being used even though it
 * doesn't look like it. This is useful for static functions that get found by NovaProva
 * using debug symbols.
 */
#define USED __attribute__ ((used))

bool server_threads_run = true;

#define TEST_CLIENT_TIPC_ID 5
#define MIN_TIPC_ID 1
#define MAX_TIPC_ID 8

static const uint16_t tipc_port = 18888;
static const uint16_t tipc_scope = TIPC_CLUSTER_SCOPE;

static pthread_t server_thread1;
static pthread_t server_thread2;

static bool impl_function_hit = false;

static int
sm_mock_cmsg_service_port_get (const char *name, const char *proto)
{
    if ((strcmp (name, "cmsg-test") == 0) && (strcmp (proto, "tipc") == 0))
    {
        return tipc_port;
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
    /* Ignore SIGPIPE signal if it occurs */
    signal (SIGPIPE, SIG_IGN);

    np_mock (cmsg_service_port_get, sm_mock_cmsg_service_port_get);

    return 0;
}

/**
 * Common functionality to run at the end of each test case.
 */
static int USED
tear_down (void)
{
    return 0;
}

static void *
server_test_thread_run (void *_tipc_instance)
{
    cmsg_server *server = NULL;
    uint32_t tipc_instance = (uintptr_t) _tipc_instance;

    server = cmsg_create_server_tipc_rpc ("cmsg-test", tipc_instance, tipc_scope,
                                          CMSG_SERVICE (cmsg, test));

    int fd = cmsg_server_get_socket (server);
    int fd_max = fd + 1;

    fd_set readfds;
    FD_ZERO (&readfds);
    FD_SET (fd, &readfds);

    while (server_threads_run)
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

    return 0;
}

static void
stop_servers_and_wait (void)
{
    server_threads_run = false;
    pthread_join (server_thread1, NULL);
    pthread_join (server_thread2, NULL);
}

/**
 * First initialise a broadcast client. Then start a couple of servers
 * and confirm that the broadcast client has automatically connected to
 * them.
 */
void
test_broadcast_client__servers_up_after_client_init (void)
{
    cmsg_client *broadcast_client = NULL;
    uintptr_t cast_id = 0;

    broadcast_client = cmsg_broadcast_client_new (CMSG_DESCRIPTOR (cmsg, test), "cmsg-test",
                                                  TEST_CLIENT_TIPC_ID, MIN_TIPC_ID,
                                                  MAX_TIPC_ID, false, true, NULL);

    NP_ASSERT_NOT_NULL (broadcast_client);

    NP_ASSERT_EQUAL (cmsg_composite_client_num_children (broadcast_client), 0);

    cast_id = (uintptr_t) 1;
    pthread_create (&server_thread1, NULL, &server_test_thread_run, (void *) cast_id);

    cast_id = (uintptr_t) 2;
    pthread_create (&server_thread2, NULL, &server_test_thread_run, (void *) cast_id);

    sleep (2);

    NP_ASSERT_EQUAL (cmsg_composite_client_num_children (broadcast_client), 2);

    cmsg_broadcast_client_destroy (broadcast_client);

    stop_servers_and_wait ();
}

/**
 * First start a couple of servers. Then initialise a broadcast client
 * and confirm that the broadcast client has automatically connected to
 * them.
 */
void
test_broadcast_client__servers_up_before_client_init (void)
{
    cmsg_client *broadcast_client = NULL;
    uintptr_t cast_id = 0;

    cast_id = (uintptr_t) 1;
    pthread_create (&server_thread1, NULL, &server_test_thread_run, (void *) cast_id);

    cast_id = (uintptr_t) 2;
    pthread_create (&server_thread2, NULL, &server_test_thread_run, (void *) cast_id);

    sleep (2);

    broadcast_client = cmsg_broadcast_client_new (CMSG_DESCRIPTOR (cmsg, test), "cmsg-test",
                                                  TEST_CLIENT_TIPC_ID, MIN_TIPC_ID,
                                                  MAX_TIPC_ID, false, true, NULL);

    NP_ASSERT_NOT_NULL (broadcast_client);

    sleep (2);

    NP_ASSERT_EQUAL (cmsg_composite_client_num_children (broadcast_client), 2);

    cmsg_broadcast_client_destroy (broadcast_client);

    stop_servers_and_wait ();
}

static void *
client_test_thread_run (void *unused)
{
    cmsg_client *client = NULL;
    int ret = 0;
    cmsg_uint32_msg send_msg = CMSG_UINT32_MSG_INIT;
    cmsg_uint32_msg *recv_msg = NULL;


    client = cmsg_create_client_tipc_rpc ("cmsg-test", TEST_CLIENT_TIPC_ID,
                                          TIPC_CLUSTER_SCOPE, CMSG_DESCRIPTOR (cmsg, test));

    CMSG_SET_FIELD_VALUE (&send_msg, value, 123);

    ret = cmsg_test_api_broadcast_client_test (client, &send_msg, &recv_msg);

    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    NP_ASSERT_NOT_NULL (recv_msg);
    NP_ASSERT_EQUAL (recv_msg->value, 456);

    CMSG_FREE_RECV_MSG (recv_msg);

    cmsg_destroy_client_and_transport (client);

    return 0;
}

void
cmsg_test_impl_broadcast_client_test (const void *service, const cmsg_uint32_msg *recv_msg)
{
    cmsg_uint32_msg send_msg = CMSG_UINT32_MSG_INIT;

    impl_function_hit = true;

    NP_ASSERT_EQUAL (recv_msg->value, 123);

    CMSG_SET_FIELD_VALUE (&send_msg, value, 456);

    cmsg_test_server_broadcast_client_testSend (service, &send_msg);
}

/**
 * First initialise a broadcast client. Then create a standard cmsg client
 * and confirm that the client can connect and send to the server of the
 * broadcast client.
 */
void
test_broadcast_client__client_can_send_to_broadcast_client (void)
{
    static pthread_t client_thread;
    cmsg_server *server = NULL;
    fd_set read_fds;
    struct timeval tv = { 5, 0 };
    int recv_fd = -1;
    int ret = -1;
    cmsg_server_accept_thread_info *info = NULL;
    eventfd_t value;
    int *newfd_ptr = NULL;

    server = cmsg_create_server_tipc_rpc ("cmsg-test", TEST_CLIENT_TIPC_ID,
                                          TIPC_CLUSTER_SCOPE, CMSG_SERVICE (cmsg, test));

    info = cmsg_server_accept_thread_init (server);
    NP_ASSERT_NOT_NULL (info);

    pthread_create (&client_thread, NULL, &client_test_thread_run, NULL);

    FD_ZERO (&read_fds);
    FD_SET (info->accept_sd_eventfd, &read_fds);

    select (info->accept_sd_eventfd + 1, &read_fds, NULL, NULL, &tv);
    NP_ASSERT_EQUAL (g_async_queue_length (info->accept_sd_queue), 1);

    /* clear notification */
    TEMP_FAILURE_RETRY (eventfd_read (info->accept_sd_eventfd, &value));

    newfd_ptr = g_async_queue_try_pop (info->accept_sd_queue);
    recv_fd = *newfd_ptr;
    CMSG_FREE (newfd_ptr);

    /* Since we are using TIPC the first message will be the
     * CMSG_MSG_TYPE_CONN_OPEN message */
    ret = cmsg_server_receive (server, recv_fd);
    NP_ASSERT_EQUAL (ret, 0);

    /* The next message should be the CMSG_MSG_TYPE_METHOD_REQ
     * message */
    ret = cmsg_server_receive (server, recv_fd);
    NP_ASSERT_EQUAL (ret, 0);

    close (recv_fd);

    NP_ASSERT_TRUE (impl_function_hit);

    pthread_join (client_thread, NULL);
    cmsg_server_accept_thread_deinit (info);
    cmsg_destroy_server_and_transport (server);
}
