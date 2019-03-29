/*
 * Common setup functionality for the functional tests.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include <np.h>
#include "service_listener/cmsg_sl_api_private.h"
#include "publisher_subscriber/cmsg_pss_api_private.h"

static void
sm_mock_cmsg_service_listener_add_server (cmsg_server *server)
{
    /* Do nothing. */
}

static void
sm_mock_cmsg_service_listener_remove_server (cmsg_server *server)
{
    /* Do nothing. */
}

/**
 * The CMSG service listener will not be running unless it is explicitly
 * started by a test. Ensure we mock any API calls to it to ensure the tests
 * don't fail on syslog messages.
 */
void
cmsg_service_listener_mock_functions (void)
{
    np_mock (cmsg_service_listener_add_server, sm_mock_cmsg_service_listener_add_server);
    np_mock (cmsg_service_listener_remove_server,
             sm_mock_cmsg_service_listener_remove_server);
}

static bool
sm_mock_cmsg_pss_subscription_add (cmsg_server *sub_server, cmsg_transport *pub_transport,
                                   const char *method_name)
{
    /* Do nothing. */
    return true;
}

static bool
sm_mock_cmsg_pss_subscription_remove (cmsg_server *sub_server,
                                      cmsg_transport *pub_transport,
                                      const char *method_name)
{
    /* Do nothing. */
    return true;
}

/**
 * The CMSG publisher subscriber storage daemon will not be running unless it
 * is explicitly started by a test. Ensure we mock any API calls to it to ensure
 * the tests don't fail on syslog messages.
 */
void
cmsg_pss_mock_functions (void)
{
    np_mock (cmsg_pss_subscription_add, sm_mock_cmsg_pss_subscription_add);
    np_mock (cmsg_pss_subscription_remove, sm_mock_cmsg_pss_subscription_remove);
}
