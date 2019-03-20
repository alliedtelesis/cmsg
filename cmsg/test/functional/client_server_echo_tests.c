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

/**
 * This informs the compiler that the function is, in fact, being used even though it
 * doesn't look like it. This is useful for static functions that get found by NovaProva
 * using debug symbols.
 */
#define USED __attribute__ ((used))

static cmsg_server *server = NULL;
static bool server_thread_run = true;
static bool server_ready = false;
static pthread_t server_thread;


/**
 * Common functionality to run before each test case.
 */
static int USED
set_up (void)
{
    server_ready = false;
    server_thread_run = true;

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

/**
 * Server processing function that should be run in a new thread.
 * Creates a server of given type and then begins polling the server
 * for any received messages. Once the main thread signals the polling
 * to stop the server is destroyed and the thread exits.
 */
static void *
server_thread_process (void *unused)
{
    server = cmsg_create_server_unix_rpc (CMSG_SERVICE (cmsg, test));

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
 */
static void
create_server_and_wait ()
{
    int ret = 0;

    ret = pthread_create (&server_thread, NULL, &server_thread_process, NULL);

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

    create_server_and_wait ();

    client = cmsg_create_client_unix (CMSG_DESCRIPTOR (cmsg, test));

    _run_client_server_echo_test (client);

    stop_server_and_wait ();

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
