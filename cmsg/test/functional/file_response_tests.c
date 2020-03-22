/*
 * Functional tests for the file response option.
 *
 * Copyright 2020, Allied Telesis Labs New Zealand, Ltd
 */

#include <np.h>
#include "cmsg_functional_tests_api_auto.h"

/**
 * This informs the compiler that the function is, in fact, being used even though it
 * doesn't look like it. This is useful for static functions that get found by NovaProva
 * using debug symbols.
 */
#define USED __attribute__ ((used))

#define RESPONSE_FILE "/tmp/test_file_response"

static cmsg_client *test_client = NULL;

/**
 * Common functionality to run before each test case.
 */
static int USED
set_up (void)
{
    test_client = cmsg_create_client_unix (CMSG_DESCRIPTOR (cmsg, test));
    return 0;
}

/**
 * Common functionality to run at the end of each test case.
 */
static int USED
tear_down (void)
{
    cmsg_destroy_client_and_transport (test_client);
    test_client = NULL;
    unlink (RESPONSE_FILE);
    return 0;
}

void
test_file_response_test_no_file (void)
{
    cmsg_file_response_message *recv_msg = NULL;
    int ret;

    ret = cmsg_test_api_file_response_test (test_client, &recv_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    NP_ASSERT_EQUAL (recv_msg->_error_info->code, CMSG_RET_OK);
    NP_ASSERT_FALSE (CMSG_IS_FIELD_PRESENT (recv_msg, bool_val));
    NP_ASSERT_FALSE (CMSG_IS_PTR_PRESENT (recv_msg, string_val));
    CMSG_FREE_RECV_MSG (recv_msg);
}

void
test_file_response_test_file_exists (void)
{
    cmsg_file_response_message *recv_msg = NULL;
    int ret;
    cmsg_file_response_message msg = CMSG_FILE_RESPONSE_MESSAGE_INIT;
    ant_result _error_info = ANT_RESULT_INIT;

    CMSG_SET_FIELD_VALUE (&_error_info, code, ANT_CODE_OK);
    CMSG_SET_FIELD_PTR (&msg, _error_info, &_error_info);
    CMSG_SET_FIELD_VALUE (&msg, bool_val, false);
    CMSG_SET_FIELD_PTR (&msg, string_val, "blah");

    ret = cmsg_dump_msg_to_file ((struct ProtobufCMessage *) &msg, RESPONSE_FILE);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    ret = cmsg_test_api_file_response_test (test_client, &recv_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    NP_ASSERT_EQUAL (recv_msg->_error_info->code, CMSG_RET_OK);
    NP_ASSERT_TRUE (CMSG_IS_FIELD_PRESENT (recv_msg, bool_val));
    NP_ASSERT_EQUAL (recv_msg->bool_val, false);
    NP_ASSERT_TRUE (CMSG_IS_PTR_PRESENT (recv_msg, string_val));
    NP_ASSERT_STR_EQUAL (recv_msg->string_val, "blah");
    CMSG_FREE_RECV_MSG (recv_msg);
}

void
test_file_response_test_wrong_message_serialised (void)
{
    cmsg_file_response_message *recv_msg = NULL;
    int ret;
    cmsg_uint32_msg msg = CMSG_UINT32_MSG_INIT;

    CMSG_SET_FIELD_VALUE (&msg, value, 1);

    ret = cmsg_dump_msg_to_file ((struct ProtobufCMessage *) &msg, RESPONSE_FILE);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    np_syslog_ignore (".*");
    ret = cmsg_test_api_file_response_test (test_client, &recv_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_ERR);
    CMSG_FREE_RECV_MSG (recv_msg);
}
