/*
 * Functional tests for the supported service option.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
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

static cmsg_client *test_client = NULL;
static cmsg_server *server = NULL;
static bool server_thread_run = true;
static bool server_ready = false;
static pthread_t server_thread;


/**
 * Server processing function that should be run in a new thread.
 * Creates a server of given type and then begins polling the server
 * for any received messages. Once the main thread signals the polling
 * to stop the server is destroyed and the thread exits.
 */
static void *
server_thread_process (void *unused)
{
    server = cmsg_create_server_unix_rpc (CMSG_SERVICE (cmsg, supported_service_test));

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
create_server_and_wait (void)
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

    create_server_and_wait ();
    test_client = cmsg_create_client_unix (CMSG_DESCRIPTOR (cmsg, supported_service_test));

    return 0;
}

/**
 * Common functionality to run at the end of each test case.
 */
static int USED
tear_down (void)
{
    stop_server_and_wait ();
    cmsg_destroy_client_and_transport (test_client);

    NP_ASSERT_NULL (server);

    return 0;
}

void
cmsg_supported_service_test_impl_ss_test_direct (const void *service,
                                                 const cmsg_bool_msg *recv_msg)
{
    ant_result send_msg = ANT_RESULT_INIT;

    CMSG_SET_FIELD_VALUE (&send_msg, code, ANT_CODE_OK);

    cmsg_supported_service_test_server_ss_test_directSend (service, &send_msg);
}

void
cmsg_supported_service_test_impl_ss_test_nested (const void *service,
                                                 const cmsg_bool_msg *recv_msg)
{
    cmsg_message_with_ant_result send_msg = CMSG_MESSAGE_WITH_ANT_RESULT_INIT;
    ant_result ant_result_msg = ANT_RESULT_INIT;

    CMSG_SET_FIELD_VALUE (&ant_result_msg, code, ANT_CODE_OK);
    CMSG_SET_FIELD_PTR (&send_msg, _error_info, &ant_result_msg);

    cmsg_supported_service_test_server_ss_test_nestedSend (service, &send_msg);
}

void
test_supported_service_functionality_direct (void)
{
    cmsg_bool_msg send_msg = CMSG_BOOL_MSG_INIT;
    ant_result *recv_msg = NULL;
    int ret;

    ret = cmsg_supported_service_test_api_ss_test_direct (test_client, &send_msg,
                                                          &recv_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    NP_ASSERT_EQUAL (recv_msg->code, ANT_CODE_UNIMPLEMENTED);
    NP_ASSERT_STR_EQUAL (recv_msg->message, "This service is not supported.");
    CMSG_FREE_RECV_MSG (recv_msg);

    system ("touch /tmp/test");

    ret = cmsg_supported_service_test_api_ss_test_direct (test_client, &send_msg,
                                                          &recv_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    NP_ASSERT_EQUAL (recv_msg->code, ANT_CODE_OK);
    CMSG_FREE_RECV_MSG (recv_msg);

    unlink ("/tmp/test");

    ret = cmsg_supported_service_test_api_ss_test_direct (test_client, &send_msg,
                                                          &recv_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    NP_ASSERT_EQUAL (recv_msg->code, ANT_CODE_UNIMPLEMENTED);
    NP_ASSERT_STR_EQUAL (recv_msg->message, "This service is not supported.");
    CMSG_FREE_RECV_MSG (recv_msg);
}

void
test_supported_service_functionality_nested (void)
{
    cmsg_bool_msg send_msg = CMSG_BOOL_MSG_INIT;
    cmsg_message_with_ant_result *recv_msg = NULL;
    int ret;

    ret = cmsg_supported_service_test_api_ss_test_nested (test_client, &send_msg,
                                                          &recv_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    NP_ASSERT_EQUAL (recv_msg->_error_info->code, ANT_CODE_UNIMPLEMENTED);
    NP_ASSERT_STR_EQUAL (recv_msg->_error_info->message, "This service is not supported.");
    CMSG_FREE_RECV_MSG (recv_msg);
}
