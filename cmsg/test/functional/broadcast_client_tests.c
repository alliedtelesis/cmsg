/*
 * Functional tests for the broadcast client functionality.
 *
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */

#include <np.h>
#include <stdint.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "cmsg_broadcast_client.h"
#include "cmsg_server.h"
#include "cmsg_functional_tests_api_auto.h"
#include "cmsg_functional_tests_impl_auto.h"
#include "cmsg_composite_client.h"
#include "setup.h"

#define LOOPBACK_ADDR_PREFIX    0x7f000000  /* 127.0.0.0 */

static cmsg_server *server1;
static pthread_t server_thread1;
static cmsg_server *server2;
static pthread_t server_thread2;

/**
 * Common functionality to run before each test case.
 */
static int USED
set_up (void)
{
    /* Ignore SIGPIPE signal if it occurs */
    signal (SIGPIPE, SIG_IGN);

    np_mock (cmsg_service_port_get, sm_mock_cmsg_service_port_get);

    cmsg_service_listener_daemon_start ();

    return 0;
}

/**
 * Common functionality to run at the end of each test case.
 */
static int USED
tear_down (void)
{
    cmsg_service_listener_daemon_stop ();

    return 0;
}

static void
create_servers (void)
{
    struct in_addr addr1 = inet_makeaddr (LOOPBACK_ADDR_PREFIX, 1);
    struct in_addr addr2 = inet_makeaddr (LOOPBACK_ADDR_PREFIX, 2);

    server1 = cmsg_create_server_tcp_ipv4_rpc ("cmsg-test", &addr1, NULL,
                                               CMSG_SERVICE (cmsg, test));
    cmsg_pthread_server_init (&server_thread1, server1);
    server2 = cmsg_create_server_tcp_ipv4_rpc ("cmsg-test", &addr2, NULL,
                                               CMSG_SERVICE (cmsg, test));
    cmsg_pthread_server_init (&server_thread2, server2);
}

static void
stop_servers_and_wait (void)
{
    pthread_cancel (server_thread1);
    pthread_join (server_thread1, NULL);
    cmsg_destroy_server_and_transport (server1);
    server1 = NULL;
    pthread_cancel (server_thread2);
    pthread_join (server_thread2, NULL);
    cmsg_destroy_server_and_transport (server2);
    server2 = NULL;

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
    struct in_addr addr5 = inet_makeaddr (LOOPBACK_ADDR_PREFIX, 5);

    broadcast_client = cmsg_broadcast_client_new (CMSG_DESCRIPTOR (cmsg, test), "cmsg-test",
                                                  addr5, false, true, NULL);

    NP_ASSERT_NOT_NULL (broadcast_client);

    NP_ASSERT_EQUAL (cmsg_composite_client_num_children (broadcast_client), 0);

    create_servers ();
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
    struct in_addr addr5 = inet_makeaddr (LOOPBACK_ADDR_PREFIX, 5);

    create_servers ();
    sleep (2);

    broadcast_client = cmsg_broadcast_client_new (CMSG_DESCRIPTOR (cmsg, test), "cmsg-test",
                                                  addr5, false, true, NULL);
    NP_ASSERT_NOT_NULL (broadcast_client);

    sleep (2);

    NP_ASSERT_EQUAL (cmsg_composite_client_num_children (broadcast_client), 2);

    cmsg_broadcast_client_destroy (broadcast_client);

    stop_servers_and_wait ();
}
