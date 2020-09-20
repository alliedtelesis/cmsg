/*
 * Functional tests for client <-> server echo functionality.
 *
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */

#include <np.h>
#include <stdint.h>
#include <cmsg_server.h>
#include "cmsg_functional_tests_api_auto.h"
#include "cmsg_functional_tests_impl_auto.h"
#include "setup.h"

static cmsg_server *server = NULL;
static pthread_t server_thread;


/**
 * Common functionality to run before each test case.
 */
static int USED
set_up (void)
{
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

static void
_run_client_server_echo_test (cmsg_client *client)
{
    int sock = -1;
    int select_ret;
    cmsg_status_code ret;
    struct timeval timeout = { 1, 0 };
    fd_set read_fds;
    int maxfd;

    FD_ZERO (&read_fds);

    sock = cmsg_client_send_echo_request (client);
    NP_ASSERT (sock >= 0);

    FD_SET (sock, &read_fds);
    maxfd = sock;

    select_ret = select (maxfd + 1, &read_fds, NULL, NULL, &timeout);
    NP_ASSERT (select_ret == 1);

    ret = cmsg_client_recv_echo_reply (client);
    NP_ASSERT (ret == CMSG_STATUS_CODE_SUCCESS);
}

static void
run_client_server_echo_test (void)
{
    cmsg_client *client = NULL;

    server = create_server (CMSG_TRANSPORT_RPC_UNIX, AF_UNSPEC, &server_thread);

    client = create_client (CMSG_TRANSPORT_RPC_UNIX, AF_UNSPEC);

    _run_client_server_echo_test (client);

    pthread_cancel (server_thread);
    pthread_join (server_thread, NULL);
    cmsg_destroy_server_and_transport (server);
    server = NULL;

    cmsg_destroy_client_and_transport (client);
}


/**
 * Run the client <-> server echo test case.
 */
void
test_client_server_echo (void)
{
    run_client_server_echo_test ();
}
