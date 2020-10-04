/*
 * Functional tests for the glib helper functionality.
 *
 * Copyright 2020, Allied Telesis Labs New Zealand, Ltd
 */

#include <np.h>
#include <stdint.h>
#include "cmsg_functional_tests_api_auto.h"
#include "cmsg_functional_tests_impl_auto.h"
#include "cmsg_glib_helpers.h"
#include "setup.h"

static GMainContext *context = NULL;
static GMainLoop *loop = NULL;

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
server_thread (void *server)
{
    _cmsg_glib_server_processing_start (server, context);

    g_main_loop_run (loop);

    cmsg_glib_server_destroy (server);

    g_main_loop_unref (loop);

    return NULL;
}

void
cmsg_test_impl_glib_helper_test (const void *service, const cmsg_bool_msg *recv_msg)
{
    cmsg_bool_msg send_msg = CMSG_BOOL_MSG_INIT;

    cmsg_test_server_simple_rpc_testSend (service, &send_msg);
}

/**
 * Run the simple client <-> server test case with a UNIX transport.
 */
void
test_glib_helper (void)
{
    int ret;
    cmsg_client *client = NULL;
    cmsg_server *server = NULL;
    cmsg_bool_msg send_msg = CMSG_BOOL_MSG_INIT;
    cmsg_bool_msg *recv_msg = NULL;
    pthread_t thread;

    context = g_main_context_new ();
    loop = g_main_loop_new (context, FALSE);

    server = cmsg_create_server_unix_rpc (CMSG_SERVICE (cmsg, test));

    ret = cmsg_server_accept_thread_init (server);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    pthread_create (&thread, NULL, server_thread, server);

    client = cmsg_create_client_unix (CMSG_DESCRIPTOR (cmsg, test));
    cmsg_test_api_glib_helper_test (client, &send_msg, &recv_msg);
    CMSG_FREE_RECV_MSG (recv_msg);
    cmsg_destroy_client_and_transport (client);

    client = cmsg_create_client_unix (CMSG_DESCRIPTOR (cmsg, test));
    cmsg_test_api_glib_helper_test (client, &send_msg, &recv_msg);
    CMSG_FREE_RECV_MSG (recv_msg);
    cmsg_destroy_client_and_transport (client);

    client = cmsg_create_client_unix (CMSG_DESCRIPTOR (cmsg, test));
    cmsg_test_api_glib_helper_test (client, &send_msg, &recv_msg);
    CMSG_FREE_RECV_MSG (recv_msg);
    /* Client destroyed at end of test so that the tear down code of
     * the glib helper is fully exercised as there are open sockets
     * remaining on the server. */

    g_main_loop_quit (loop);
    pthread_join (thread, NULL);
    g_main_context_unref (context);
    cmsg_destroy_client_and_transport (client);
}
