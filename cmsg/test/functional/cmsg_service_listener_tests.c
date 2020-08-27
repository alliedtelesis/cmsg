/*
 * Functional tests for the service listener functionality.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include <np.h>
#include <cmsg/cmsg_sl.h>
#include <cmsg/cmsg_server.h>
#include "cmsg_functional_tests_api_auto.h"
#include "cmsg_functional_tests_impl_auto.h"
#include "setup.h"

/**
 * This informs the compiler that the function is, in fact, being used even though it
 * doesn't look like it. This is useful for static functions that get found by NovaProva
 * using debug symbols.
 */
#define USED __attribute__ ((used))

static cmsg_transport *test_transport = NULL;
static bool expected_added = false;

static int USED
set_up (void)
{
    /* Ignore SIGPIPE signal if it occurs */
    signal (SIGPIPE, SIG_IGN);

    test_transport = NULL;
    expected_added = false;

    cmsg_service_listener_daemon_start ();

    return 0;
}

static int USED
tear_down (void)
{
    cmsg_service_listener_daemon_stop ();

    return 0;
}

static bool
sl_event_handler (const cmsg_transport *transport, bool added, void *user_data)
{
    NP_ASSERT_TRUE (cmsg_transport_compare (transport, test_transport));
    NP_ASSERT_EQUAL (added, expected_added);

    return false;
}

void
test_cmsg_service_listener_listen_first (void)
{
    const cmsg_sl_info *info = NULL;
    const char *service_name = NULL;
    cmsg_server *test_server = NULL;

    service_name = cmsg_service_name_get (CMSG_DESCRIPTOR (cmsg, test));

    info = cmsg_service_listener_listen (service_name, sl_event_handler, NULL);

    test_server = cmsg_create_server_unix_rpc (CMSG_SERVICE (cmsg, test));
    test_transport = cmsg_transport_copy (test_server->_transport);

    expected_added = true;
    while (cmsg_service_listener_event_queue_process (info))
    {
    }

    cmsg_destroy_server_and_transport (test_server);

    expected_added = false;
    while (cmsg_service_listener_event_queue_process (info))
    {
    }

    cmsg_service_listener_unlisten (info);
    cmsg_transport_destroy (test_transport);
}

void
test_cmsg_service_listener_listen_last (void)
{
    const cmsg_sl_info *info = NULL;
    const char *service_name = NULL;
    cmsg_server *test_server = NULL;

    service_name = cmsg_service_name_get (CMSG_DESCRIPTOR (cmsg, test));

    test_server = cmsg_create_server_unix_rpc (CMSG_SERVICE (cmsg, test));
    test_transport = cmsg_transport_copy (test_server->_transport);

    info = cmsg_service_listener_listen (service_name, sl_event_handler, NULL);

    expected_added = true;
    while (cmsg_service_listener_event_queue_process (info))
    {
    }

    cmsg_destroy_server_and_transport (test_server);

    expected_added = false;
    while (cmsg_service_listener_event_queue_process (info))
    {
    }

    cmsg_service_listener_unlisten (info);
    cmsg_transport_destroy (test_transport);
}

void
test_cmsg_service_listener_create_server_after_crash (void)
{
    const cmsg_sl_info *info = NULL;
    const char *service_name = NULL;
    cmsg_server *test_server1 = NULL;
    cmsg_server *test_server2 = NULL;

    service_name = cmsg_service_name_get (CMSG_DESCRIPTOR (cmsg, test));

    test_server1 = cmsg_create_server_unix_rpc (CMSG_SERVICE (cmsg, test));
    test_transport = cmsg_transport_copy (test_server1->_transport);

    info = cmsg_service_listener_listen (service_name, sl_event_handler, NULL);

    expected_added = true;
    while (cmsg_service_listener_event_queue_process (info))
    {
    }

    test_server2 = cmsg_create_server_unix_rpc (CMSG_SERVICE (cmsg, test));

    expected_added = false;
    while (cmsg_service_listener_event_queue_process (info))
    {
    }

    expected_added = true;
    while (cmsg_service_listener_event_queue_process (info))
    {
    }

    cmsg_destroy_server_and_transport (test_server1);
    cmsg_destroy_server_and_transport (test_server2);

    expected_added = false;
    while (cmsg_service_listener_event_queue_process (info))
    {
    }

    cmsg_service_listener_unlisten (info);
    cmsg_transport_destroy (test_transport);
}
