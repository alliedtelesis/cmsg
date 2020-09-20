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

static cmsg_client *test_client = NULL;
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

    server = cmsg_create_server_unix_rpc (CMSG_SERVICE (cmsg, supported_service_test));
    NP_ASSERT_TRUE (cmsg_pthread_server_init (&server_thread, server));
    test_client = cmsg_create_client_unix (CMSG_DESCRIPTOR (cmsg, supported_service_test));

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
