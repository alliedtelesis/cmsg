/*
 * Functional tests for the broadcast client functionality.
 *
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */

#include <np.h>
#include <stdint.h>
#include "cmsg_broadcast_client.h"
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

    broadcast_client = cmsg_broadcast_client_new (CMSG_SERVICE (cmsg, test), "cmsg-test",
                                                  TEST_CLIENT_TIPC_ID, MIN_TIPC_ID,
                                                  MAX_TIPC_ID, CMSG_BROADCAST_LOCAL_NONE,
                                                  true);

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

    broadcast_client = cmsg_broadcast_client_new (CMSG_SERVICE (cmsg, test), "cmsg-test",
                                                  TEST_CLIENT_TIPC_ID, MIN_TIPC_ID,
                                                  MAX_TIPC_ID, CMSG_BROADCAST_LOCAL_NONE,
                                                  true);

    NP_ASSERT_NOT_NULL (broadcast_client);

    sleep (2);

    NP_ASSERT_EQUAL (cmsg_composite_client_num_children (broadcast_client), 2);

    cmsg_broadcast_client_destroy (broadcast_client);

    stop_servers_and_wait ();
}
