/*
 * Functional tests for the TCP connection cache functionality.
 *
 * Copyright 2020, Allied Telesis Labs New Zealand, Ltd
 */

#include <arpa/inet.h>
#include <np.h>
#include <stdint.h>
#include "setup.h"
#include "transport/cmsg_transport_private.h"

static cmsg_server *server = NULL;
static pthread_t server_thread;

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

    server = create_server (CMSG_TRANSPORT_RPC_TCP, AF_INET, &server_thread);

    return 0;
}

/**
 * Common functionality to run at the end of each test case.
 */
static int USED
tear_down (void)
{
    pthread_cancel (server_thread);
    pthread_join (server_thread, NULL);
    cmsg_destroy_server_and_transport (server);
    server = NULL;

    return 0;
}

void
test_tcp_connection_cache (void)
{
    int ret;
    cmsg_client *client = NULL;
    struct in_addr addr = { };

    client = create_client (CMSG_TRANSPORT_RPC_TCP, AF_INET);
    ret = cmsg_client_connect (client);
    cmsg_destroy_client_and_transport (client);
    NP_ASSERT_EQUAL (ret, 0);

    addr.s_addr = INADDR_ANY;
    cmsg_transport_tcp_cache_set (&addr, false);

    client = create_client (CMSG_TRANSPORT_RPC_TCP, AF_INET);
    np_syslog_ignore (".*");
    ret = cmsg_client_connect (client);
    np_syslog_fail (".*");
    cmsg_destroy_client_and_transport (client);
    NP_ASSERT_NOT_EQUAL (ret, 0);

    cmsg_transport_tcp_cache_set (&addr, true);

    client = create_client (CMSG_TRANSPORT_RPC_TCP, AF_INET);
    ret = cmsg_client_connect (client);
    cmsg_destroy_client_and_transport (client);
    NP_ASSERT_EQUAL (ret, 0);
}
