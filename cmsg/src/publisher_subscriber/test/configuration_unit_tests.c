/*
 * Unit tests for the configuration functionality.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include <np.h>
#include "../configuration_impl_auto.h"
#include "../data.h"

/**
 * This informs the compiler that the function is, in fact, being used even though it
 * doesn't look like it. This is useful for static functions that get found by NovaProva
 * using debug symbols.
 */
#define USED __attribute__ ((used))

static bool cmsg_server_app_owns_current_msg_set_called = false;

static void
sm_mock_cmsg_server_app_owns_current_msg_set (cmsg_server *server)
{
    cmsg_server_app_owns_current_msg_set_called = true;
}

static void
sm_mock_cmsg_server_send_response (const ProtobufCMessage *send_msg, const void *service)
{
    /* Do nothing. */
}

static bool
sm_mock_data_add_subscription_return_true (const cmsg_subscription_info *info)
{
    return true;
}

static bool
sm_mock_data_add_subscription_return_false (const cmsg_subscription_info *info)
{
    return false;
}

static int USED
set_up (void)
{
    cmsg_server_app_owns_current_msg_set_called = false;

    np_mock (cmsg_server_app_owns_current_msg_set,
             sm_mock_cmsg_server_app_owns_current_msg_set);
    np_mock (cmsg_server_send_response, sm_mock_cmsg_server_send_response);

    return 0;
}

void
test_cmsg_psd_configuration_impl_add_subscription_remote (void)
{
    np_mock (data_add_subscription, sm_mock_data_add_subscription_return_true);
    cmsg_psd_configuration_impl_add_subscription (NULL, NULL);
    NP_ASSERT_TRUE (cmsg_server_app_owns_current_msg_set_called);
}

void
test_cmsg_psd_configuration_impl_add_subscription_local (void)
{
    np_mock (data_add_subscription, sm_mock_data_add_subscription_return_false);
    cmsg_psd_configuration_impl_add_subscription (NULL, NULL);
    NP_ASSERT_FALSE (cmsg_server_app_owns_current_msg_set_called);
}
