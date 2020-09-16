/*
 * Functional tests for client side queuing.
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

static uint32_t test_total = 0;

/**
 * Common functionality to run before each test case.
 */
static int USED
set_up (void)
{
    test_total = 0;

    /* Ignore SIGPIPE signal if it occurs */
    signal (SIGPIPE, SIG_IGN);

    np_mock (cmsg_service_port_get, sm_mock_cmsg_service_port_get);

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

void
cmsg_test_impl_simple_client_queue_test_1 (const void *service,
                                           const cmsg_uint32_msg *recv_msg)
{
    NP_ASSERT_EQUAL (recv_msg->value, 1);
    test_total += recv_msg->value;

    cmsg_test_server_simple_client_queue_test_1Send (service);
}

void
cmsg_test_impl_simple_client_queue_test_2 (const void *service,
                                           const cmsg_uint32_msg *recv_msg)
{
    NP_ASSERT_EQUAL (recv_msg->value, 2);
    test_total += recv_msg->value;

    cmsg_test_server_simple_client_queue_test_2Send (service);
}

void
cmsg_test_impl_simple_client_queue_test_3 (const void *service,
                                           const cmsg_uint32_msg *recv_msg)
{
    NP_ASSERT_EQUAL (recv_msg->value, 3);
    test_total += recv_msg->value;

    cmsg_test_server_simple_client_queue_test_3Send (service);
}

/**
 * Run the simple test with a given CMSG client. Assumes the related
 * server has already been created and is ready to process any API
 * requests.
 *
 * @param client - CMSG client to run the simple test with
 */
static void
_run_client_queuing_drop_all_tests (cmsg_client *client)
{
    int ret = 0;
    cmsg_uint32_msg send_msg = CMSG_UINT32_MSG_INIT;

    cmsg_client_queue_filter_set_all (client, CMSG_QUEUE_FILTER_DROP);

    CMSG_SET_FIELD_VALUE (&send_msg, value, 1);
    ret = cmsg_test_api_simple_client_queue_test_1 (client, &send_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_DROPPED);

    CMSG_SET_FIELD_VALUE (&send_msg, value, 2);
    ret = cmsg_test_api_simple_client_queue_test_2 (client, &send_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_DROPPED);

    CMSG_SET_FIELD_VALUE (&send_msg, value, 3);
    ret = cmsg_test_api_simple_client_queue_test_3 (client, &send_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_DROPPED);

    sleep (1);
    NP_ASSERT_EQUAL (test_total, 0);
}

static void
_run_client_queuing_drop_specific_tests (cmsg_client *client)
{
    int ret = 0;
    cmsg_uint32_msg send_msg = CMSG_UINT32_MSG_INIT;

    cmsg_client_queue_filter_set (client, "simple_client_queue_test_2",
                                  CMSG_QUEUE_FILTER_DROP);

    CMSG_SET_FIELD_VALUE (&send_msg, value, 1);
    ret = cmsg_test_api_simple_client_queue_test_1 (client, &send_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    CMSG_SET_FIELD_VALUE (&send_msg, value, 2);
    ret = cmsg_test_api_simple_client_queue_test_2 (client, &send_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_DROPPED);

    CMSG_SET_FIELD_VALUE (&send_msg, value, 3);
    ret = cmsg_test_api_simple_client_queue_test_3 (client, &send_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    sleep (1);
    NP_ASSERT_EQUAL (test_total, 4);

    cmsg_client_queue_filter_clear (client, "simple_client_queue_test_2");
    CMSG_SET_FIELD_VALUE (&send_msg, value, 2);
    ret = cmsg_test_api_simple_client_queue_test_2 (client, &send_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    sleep (1);
    NP_ASSERT_EQUAL (test_total, 6);
}

static void
_run_client_queuing_queue_all_tests (cmsg_client *client)
{
    int ret = 0;
    cmsg_uint32_msg send_msg = CMSG_UINT32_MSG_INIT;

    cmsg_client_queue_filter_set_all (client, CMSG_QUEUE_FILTER_QUEUE);

    CMSG_SET_FIELD_VALUE (&send_msg, value, 1);
    ret = cmsg_test_api_simple_client_queue_test_1 (client, &send_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_QUEUED);

    CMSG_SET_FIELD_VALUE (&send_msg, value, 2);
    ret = cmsg_test_api_simple_client_queue_test_2 (client, &send_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_QUEUED);

    CMSG_SET_FIELD_VALUE (&send_msg, value, 3);
    ret = cmsg_test_api_simple_client_queue_test_3 (client, &send_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_QUEUED);

    sleep (1);
    NP_ASSERT_EQUAL (test_total, 0);

    cmsg_client_queue_process_all (client);

    sleep (1);
    NP_ASSERT_EQUAL (test_total, 6);
}

static void
_run_client_queuing_queue_specific_tests (cmsg_client *client)
{
    int ret = 0;
    cmsg_uint32_msg send_msg = CMSG_UINT32_MSG_INIT;

    cmsg_client_queue_filter_set (client, "simple_client_queue_test_2",
                                  CMSG_QUEUE_FILTER_QUEUE);

    CMSG_SET_FIELD_VALUE (&send_msg, value, 1);
    ret = cmsg_test_api_simple_client_queue_test_1 (client, &send_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    CMSG_SET_FIELD_VALUE (&send_msg, value, 2);
    ret = cmsg_test_api_simple_client_queue_test_2 (client, &send_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_QUEUED);

    CMSG_SET_FIELD_VALUE (&send_msg, value, 3);
    ret = cmsg_test_api_simple_client_queue_test_3 (client, &send_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    sleep (1);
    NP_ASSERT_EQUAL (test_total, 4);

    cmsg_client_queue_filter_clear (client, "simple_client_queue_test_2");
    CMSG_SET_FIELD_VALUE (&send_msg, value, 2);
    ret = cmsg_test_api_simple_client_queue_test_2 (client, &send_msg);
    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);

    sleep (1);
    NP_ASSERT_EQUAL (test_total, 6);

    cmsg_client_queue_process_all (client);

    sleep (1);
    NP_ASSERT_EQUAL (test_total, 8);
}

static void
run_client_queuing_tests (cmsg_queue_filter_type queue_type, bool all)
{
    cmsg_client *client = NULL;

    server = create_server (CMSG_TRANSPORT_ONEWAY_TCP, AF_INET, &server_thread);

    client = create_client (CMSG_TRANSPORT_ONEWAY_TCP, AF_INET);

    if (queue_type == CMSG_QUEUE_FILTER_DROP)
    {
        if (all)
        {
            _run_client_queuing_drop_all_tests (client);
        }
        else
        {
            _run_client_queuing_drop_specific_tests (client);
        }
    }
    else if (queue_type == CMSG_QUEUE_FILTER_QUEUE)
    {
        if (all)
        {
            _run_client_queuing_queue_all_tests (client);
        }
        else
        {
            _run_client_queuing_queue_specific_tests (client);
        }
    }
    else
    {
        NP_FAIL;
    }

    pthread_cancel (server_thread);
    pthread_join (server_thread, NULL);
    cmsg_destroy_server_and_transport (server);
    server = NULL;
    cmsg_destroy_client_and_transport (client);
}

/**
 * Run the simple client <-> server test case with a TCP transport.
 */
void
test_client_queuing_all_drop (void)
{
    run_client_queuing_tests (CMSG_QUEUE_FILTER_DROP, true);
}

void
test_client_queuing_all_queue (void)
{
    run_client_queuing_tests (CMSG_QUEUE_FILTER_QUEUE, true);
}

void
test_client_queuing_specific_drop (void)
{
    run_client_queuing_tests (CMSG_QUEUE_FILTER_DROP, false);
}

void
test_client_queuing_specific_queue (void)
{
    run_client_queuing_tests (CMSG_QUEUE_FILTER_QUEUE, false);
}
