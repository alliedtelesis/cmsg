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
static bool data_add_server_called = false;
static bool data_remove_server_called = false;

static void
sm_mock_cmsg_server_app_owns_current_msg_set (cmsg_server *server)
{
    cmsg_server_app_owns_current_msg_set_called = true;
}

static void
sm_mock_data_add_server (const cmsg_service_info *server_info, bool local)
{
    data_add_server_called = true;
}

static void
sm_mock_data_remove_server (const cmsg_service_info *server_info, bool local)
{
    data_remove_server_called = true;
}

static void
sm_mock_cmsg_server_send_response (const ProtobufCMessage *send_msg, const void *service)
{
}

static int USED
set_up (void)
{
    cmsg_server_app_owns_current_msg_set_called = false;
    data_add_server_called = false;
    data_remove_server_called = false;

    np_mock (cmsg_server_app_owns_current_msg_set,
             sm_mock_cmsg_server_app_owns_current_msg_set);
    np_mock (data_remove_server, sm_mock_data_remove_server);
    np_mock (data_add_server, sm_mock_data_add_server);
    np_mock (cmsg_server_send_response, sm_mock_cmsg_server_send_response);

    return 0;
}

void
test_cmsg_sld_configuration_impl_add_server_tcp (void)
{
    cmsg_service_info service_info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;

    CMSG_SET_FIELD_VALUE (&transport_info, type, CMSG_TRANSPORT_INFO_TYPE_TCP);
    CMSG_SET_FIELD_PTR (&service_info, server_info, &transport_info);

    cmsg_sld_configuration_impl_add_server (NULL, &service_info);

    NP_ASSERT_TRUE (cmsg_server_app_owns_current_msg_set_called);
    NP_ASSERT_TRUE (data_add_server_called);
    NP_ASSERT_FALSE (data_remove_server_called);
}

void
test_cmsg_sld_configuration_impl_add_server_unix (void)
{
    cmsg_service_info service_info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;

    CMSG_SET_FIELD_VALUE (&transport_info, type, CMSG_TRANSPORT_INFO_TYPE_UNIX);
    CMSG_SET_FIELD_PTR (&service_info, server_info, &transport_info);

    cmsg_sld_configuration_impl_add_server (NULL, &service_info);

    NP_ASSERT_TRUE (cmsg_server_app_owns_current_msg_set_called);
    NP_ASSERT_TRUE (data_add_server_called);
    NP_ASSERT_FALSE (data_remove_server_called);
}

void
test_cmsg_sld_configuration_impl_remove_server_tcp (void)
{
    cmsg_service_info service_info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;

    CMSG_SET_FIELD_VALUE (&transport_info, type, CMSG_TRANSPORT_INFO_TYPE_TCP);
    CMSG_SET_FIELD_PTR (&service_info, server_info, &transport_info);

    cmsg_sld_configuration_impl_remove_server (NULL, &service_info);

    NP_ASSERT_FALSE (cmsg_server_app_owns_current_msg_set_called);
    NP_ASSERT_TRUE (data_remove_server_called);
    NP_ASSERT_FALSE (data_add_server_called);
}

void
test_cmsg_sld_configuration_impl_remove_server_unix (void)
{
    cmsg_service_info service_info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;

    CMSG_SET_FIELD_VALUE (&transport_info, type, CMSG_TRANSPORT_INFO_TYPE_UNIX);
    CMSG_SET_FIELD_PTR (&service_info, server_info, &transport_info);

    cmsg_sld_configuration_impl_remove_server (NULL, &service_info);

    NP_ASSERT_FALSE (cmsg_server_app_owns_current_msg_set_called);
    NP_ASSERT_TRUE (data_remove_server_called);
    NP_ASSERT_FALSE (data_add_server_called);
}
