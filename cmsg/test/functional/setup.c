/*
 * Common setup functionality for the functional tests.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include <np.h>
#include "service_listener/cmsg_sl_api_private.h"
#include "publisher_subscriber/cmsg_ps_api_private.h"

#define CMSG_SLD_WAIT_TIME (500 * 1000)

void
cmsg_service_listener_daemon_start (void)
{
    system ("cmsg_sld &");
    usleep (CMSG_SLD_WAIT_TIME);
}

void
cmsg_service_listener_daemon_stop (void)
{
    system ("pkill cmsg_sld");
    usleep (CMSG_SLD_WAIT_TIME);
}

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

static int32_t
sm_mock_cmsg_ps_subscription_add_local (cmsg_server *sub_server, const char *method_name)
{
    /* Do nothing. */
    return CMSG_RET_OK;
}

static int32_t
sm_mock_cmsg_ps_subscription_add_remote (cmsg_server *sub_server, const char *method_name,
                                         struct in_addr remote_addr)
{
    /* Do nothing. */
    return CMSG_RET_OK;
}

static int32_t
sm_mock_cmsg_ps_subscription_remove_local (cmsg_server *sub_server, const char *method_name)
{
    /* Do nothing. */
    return CMSG_RET_OK;
}

static int32_t
sm_mock_cmsg_ps_subscription_remove_remote (cmsg_server *sub_server,
                                            const char *method_name,
                                            struct in_addr remote_addr)
{
    /* Do nothing. */
    return CMSG_RET_OK;
}

static int32_t
sm_mock_cmsg_ps_remove_subscriber (cmsg_server *sub_server)
{
    /* Do nothing. */
    return CMSG_RET_OK;
}

/**
 * The CMSG publisher subscriber storage daemon will not be running unless it
 * is explicitly started by a test. Ensure we mock any API calls to it to ensure
 * the tests don't fail on syslog messages.
 */
void
cmsg_ps_mock_functions (void)
{
    np_mock (cmsg_ps_subscription_add_local, sm_mock_cmsg_ps_subscription_add_local);
    np_mock (cmsg_ps_subscription_add_remote, sm_mock_cmsg_ps_subscription_add_remote);
    np_mock (cmsg_ps_subscription_remove_local, sm_mock_cmsg_ps_subscription_remove_local);
    np_mock (cmsg_ps_subscription_remove_remote,
             sm_mock_cmsg_ps_subscription_remove_remote);
    np_mock (cmsg_ps_remove_subscriber, sm_mock_cmsg_ps_remove_subscriber);
}
