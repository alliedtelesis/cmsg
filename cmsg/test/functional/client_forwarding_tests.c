/*
 * Functional tests for the forwarding client.
 *
 * Copyright 2021, Allied Telesis Labs New Zealand, Ltd
 */

#include <np.h>
#include <stdint.h>
#include "cmsg_functional_tests_api_auto.h"
#include "cmsg_functional_tests_impl_auto.h"
#include "setup.h"

static void *test_ptr;
static bool func_called = false;

/**
 * Common functionality to run before each test case.
 */
static int USED
set_up (void)
{
    /* Ignore SIGPIPE signal if it occurs */
    signal (SIGPIPE, SIG_IGN);

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

void
cmsg_test_impl_simple_forwarding_test (const void *service, const cmsg_bool_msg *recv_msg)
{
    /* Should not be hit as client is forwarding to a user defined function */
    NP_FAIL;
}

static int
send_func (void *user_data, void *buff, int length)
{
    NP_ASSERT_PTR_EQUAL (user_data, test_ptr);
    func_called = true;
    return 0;
}

void
test_forwarding_client (void)
{
    int ret = 0;
    cmsg_client *client;
    cmsg_bool_msg send_msg = CMSG_BOOL_MSG_INIT;

    CMSG_SET_FIELD_VALUE (&send_msg, value, true);

    test_ptr = (void *) 0x123;
    client = cmsg_create_client_forwarding (CMSG_DESCRIPTOR (cmsg, test), test_ptr,
                                            send_func);

    func_called = false;
    ret = cmsg_test_api_simple_forwarding_test (client, &send_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    NP_ASSERT_TRUE (func_called);

    test_ptr = (void *) 0x456;
    cmsg_client_forwarding_data_set (client, test_ptr);

    func_called = false;
    ret = cmsg_test_api_simple_forwarding_test (client, &send_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    NP_ASSERT_TRUE (func_called);

    cmsg_destroy_client_and_transport (client);
}
