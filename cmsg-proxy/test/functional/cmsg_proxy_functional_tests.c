/*
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 *
 * Functional tests for the CMSG proxy library.
 */

#define _GNU_SOURCE
#include <np.h>
#include "cmsg_proxy_mem.c"
#include "cmsg_proxy_tree.c"
#include "cmsg_proxy.c"
#include "cmsg_proxy_functional_tests_proxy_def.h"
#include "cmsg_proxy_functional_tests_api_auto.h"
#include "cmsg_proxy_functional_tests_impl_auto.h"

/**
 * Mock the '_cmsg_proxy_library_handles_load' function to simply call
 * the autogenerated 'cmsg_proxy_array_get' and 'cmsg_proxy_array_size'
 * functions that are linked with the executable. Usually the CMSG proxy
 * will dynamically load every '_proxy_def' library that is on the device
 * however to simplify the functional tests we don't do this.
 */
void
sm_mock_cmsg_proxy_library_handles_load (void)
{
    _cmsg_proxy_service_info_init (cmsg_proxy_array_get (), cmsg_proxy_array_size ());
}

/**
 * Mock the 'cmsg_create_client_unix' used by the cmsg-proxy to instead be the
 * 'cmsg_create_client_loopback' function. This allows the proxy to execute the
 * CMSG IMPL functions locally which removes the need to run a separate thread/process
 * to implement the CMSG Unix socket server that the proxy usually expects. This makes
 * implementing functional tests much simpler.
 */
cmsg_client *
sm_mock_cmsg_create_client_unix (const ProtobufCServiceDescriptor *descriptor)
{
    return cmsg_create_client_loopback (CMSG_SERVICE_NOPACKAGE (functional_tests));
}

void
functional_tests_impl_test_single_bool_get (const void *service)
{
    ant_result error_info = ANT_RESULT_INIT;
    ant_result_plus_bool send_msg = ANT_RESULT_PLUS_BOOL_INIT;

    CMSG_SET_FIELD_VALUE (&error_info, code, ANT_CODE_OK);
    CMSG_SET_FIELD_PTR (&send_msg, _error_info, &error_info);
    CMSG_SET_FIELD_VALUE (&send_msg, _data, true);

    functional_tests_server_test_single_bool_getSend (service, &send_msg);
}

void
functional_tests_impl_test_single_string_get (const void *service)
{
    ant_result error_info = ANT_RESULT_INIT;
    ant_result_plus_string send_msg = ANT_RESULT_PLUS_STRING_INIT;

    CMSG_SET_FIELD_VALUE (&error_info, code, ANT_CODE_OK);
    CMSG_SET_FIELD_PTR (&send_msg, _error_info, &error_info);
    CMSG_SET_FIELD_PTR (&send_msg, _data, "single string");

    functional_tests_server_test_single_string_getSend (service, &send_msg);
}

void
functional_tests_impl_test_single_uint32_get (const void *service)
{
    ant_result error_info = ANT_RESULT_INIT;
    ant_result_plus_uint32 send_msg = ANT_RESULT_PLUS_UINT32_INIT;

    CMSG_SET_FIELD_VALUE (&error_info, code, ANT_CODE_OK);
    CMSG_SET_FIELD_PTR (&send_msg, _error_info, &error_info);
    CMSG_SET_FIELD_VALUE (&send_msg, _data, 123);

    functional_tests_server_test_single_uint32_getSend (service, &send_msg);
}

void
functional_tests_impl_test_single_message_get (const void *service)
{
    ant_result error_info = ANT_RESULT_INIT;
    cmsg_bool inner_msg = CMSG_BOOL_INIT;
    test_single_message_get_msg send_msg = TEST_SINGLE_MESSAGE_GET_MSG_INIT;

    CMSG_SET_FIELD_VALUE (&error_info, code, ANT_CODE_OK);
    CMSG_SET_FIELD_PTR (&send_msg, _error_info, &error_info);
    CMSG_SET_FIELD_VALUE (&inner_msg, value, false);
    CMSG_SET_FIELD_PTR (&send_msg, inner_message, &inner_msg);

    functional_tests_server_test_single_message_getSend (service, &send_msg);
}

void
functional_tests_impl_test_repeated_string_get (const void *service)
{
    char *repeated_strings[3] = { "string1", "string2", "string3", };
    ant_result error_info = ANT_RESULT_INIT;
    ant_result_plus_repeated_string send_msg = ANT_RESULT_PLUS_REPEATED_STRING_INIT;

    CMSG_SET_FIELD_VALUE (&error_info, code, ANT_CODE_OK);
    CMSG_SET_FIELD_PTR (&send_msg, _error_info, &error_info);
    CMSG_SET_FIELD_REPEATED (&send_msg, _data, repeated_strings, 3);

    functional_tests_server_test_repeated_string_getSend (service, &send_msg);
}

void
functional_tests_impl_test_repeated_uint32_get (const void *service)
{
    uint32_t repeated_uint32[3] = { 1, 2, 3 };
    ant_result error_info = ANT_RESULT_INIT;
    ant_result_plus_uint32_array send_msg = ANT_RESULT_PLUS_UINT32_ARRAY_INIT;

    CMSG_SET_FIELD_VALUE (&error_info, code, ANT_CODE_OK);
    CMSG_SET_FIELD_PTR (&send_msg, _error_info, &error_info);
    CMSG_SET_FIELD_REPEATED (&send_msg, _data, repeated_uint32, 3);

    functional_tests_server_test_repeated_uint32_getSend (service, &send_msg);
}

void
functional_tests_impl_test_repeated_message_get (const void *service)
{
    ant_result error_info = ANT_RESULT_INIT;
    cmsg_bool **inner_msgs = CMSG_MSG_ARRAY_ALLOC (cmsg_bool, 3);
    test_repeated_message_get_msg send_msg = TEST_REPEATED_MESSAGE_GET_MSG_INIT;

    for (int i = 0; i < 3; i++)
    {
        cmsg_bool_init (inner_msgs[i]);
        CMSG_SET_FIELD_VALUE (inner_msgs[i], value, false);
    }

    CMSG_SET_FIELD_VALUE (&error_info, code, ANT_CODE_OK);
    CMSG_SET_FIELD_PTR (&send_msg, _error_info, &error_info);
    CMSG_SET_FIELD_REPEATED (&send_msg, inner_messages, inner_msgs, 3);

    functional_tests_server_test_repeated_message_getSend (service, &send_msg);

    CMSG_MSG_ARRAY_FREE (inner_msgs);
}

void
functional_tests_impl_test_multiple_fields_message_get (const void *service)
{
    ant_result error_info = ANT_RESULT_INIT;
    cmsg_bool inner_msg = CMSG_BOOL_INIT;
    test_multiple_fields_message_get_msg send_msg =
        TEST_MULTIPLE_FIELDS_MESSAGE_GET_MSG_INIT;

    CMSG_SET_FIELD_VALUE (&error_info, code, ANT_CODE_OK);
    CMSG_SET_FIELD_PTR (&send_msg, _error_info, &error_info);
    CMSG_SET_FIELD_VALUE (&inner_msg, value, true);
    CMSG_SET_FIELD_PTR (&send_msg, inner_bool_msg, &inner_msg);
    CMSG_SET_FIELD_PTR (&send_msg, inner_string, "test_string");
    CMSG_SET_FIELD_VALUE (&send_msg, inner_uint32, 123);

    functional_tests_server_test_multiple_fields_message_getSend (service, &send_msg);
}

void
functional_tests_impl_test_ant_result_get_ok (const void *service)
{
    ant_result send_msg = ANT_RESULT_INIT;

    CMSG_SET_FIELD_VALUE (&send_msg, code, ANT_CODE_OK);
    CMSG_SET_FIELD_PTR (&send_msg, message, "test message");

    functional_tests_server_test_ant_result_get_okSend (service, &send_msg);
}

void
functional_tests_impl_test_ant_result_get_error (const void *service)
{
    ant_result send_msg = ANT_RESULT_INIT;

    CMSG_SET_FIELD_VALUE (&send_msg, code, ANT_CODE_NOT_FOUND);
    CMSG_SET_FIELD_PTR (&send_msg, message, "ERROR: Not found");

    functional_tests_server_test_ant_result_get_errorSend (service, &send_msg);
}

void
functional_tests_impl_test_get_error_with_single_data (const void *service)
{
    ant_result error_info = ANT_RESULT_INIT;
    ant_result_plus_bool send_msg = ANT_RESULT_PLUS_BOOL_INIT;

    CMSG_SET_FIELD_VALUE (&error_info, code, ANT_CODE_NOT_FOUND);
    CMSG_SET_FIELD_PTR (&error_info, message, "ERROR: Not found");
    CMSG_SET_FIELD_PTR (&send_msg, _error_info, &error_info);
    CMSG_SET_FIELD_VALUE (&send_msg, _data, true);

    functional_tests_server_test_get_error_with_single_dataSend (service, &send_msg);
}

void
functional_tests_impl_test_get_error_with_multiple_data (const void *service)
{
    ant_result error_info = ANT_RESULT_INIT;
    cmsg_bool inner_msg = CMSG_BOOL_INIT;
    test_multiple_fields_message_get_msg send_msg =
        TEST_MULTIPLE_FIELDS_MESSAGE_GET_MSG_INIT;

    CMSG_SET_FIELD_VALUE (&error_info, code, ANT_CODE_NOT_FOUND);
    CMSG_SET_FIELD_PTR (&error_info, message, "ERROR: Not found");
    CMSG_SET_FIELD_PTR (&send_msg, _error_info, &error_info);
    CMSG_SET_FIELD_VALUE (&inner_msg, value, true);
    CMSG_SET_FIELD_PTR (&send_msg, inner_bool_msg, &inner_msg);
    CMSG_SET_FIELD_PTR (&send_msg, inner_string, "test_string");
    CMSG_SET_FIELD_VALUE (&send_msg, inner_uint32, 123);

    functional_tests_server_test_get_error_with_multiple_dataSend (service, &send_msg);
}

void
functional_tests_impl_test_single_bool_put (const void *service, const cmsg_bool *recv_msg)
{
    ant_result send_msg = ANT_RESULT_INIT;

    NP_ASSERT_TRUE (CMSG_IS_FIELD_PRESENT (recv_msg, value));
    NP_ASSERT_FALSE (recv_msg->value);

    CMSG_SET_FIELD_VALUE (&send_msg, code, ANT_CODE_OK);

    functional_tests_server_test_single_bool_putSend (service, &send_msg);
}

void
functional_tests_impl_test_single_string_put (const void *service,
                                              const cmsg_string *recv_msg)
{
    ant_result send_msg = ANT_RESULT_INIT;

    NP_ASSERT_TRUE (CMSG_IS_PTR_PRESENT (recv_msg, value));
    NP_ASSERT_STR_EQUAL (recv_msg->value, "Test String");

    CMSG_SET_FIELD_VALUE (&send_msg, code, ANT_CODE_OK);

    functional_tests_server_test_single_string_putSend (service, &send_msg);
}

void
functional_tests_impl_test_single_uint32_put (const void *service,
                                              const cmsg_uint32 *recv_msg)
{
    ant_result send_msg = ANT_RESULT_INIT;

    NP_ASSERT_TRUE (CMSG_IS_FIELD_PRESENT (recv_msg, value));
    NP_ASSERT_EQUAL (recv_msg->value, 987);

    CMSG_SET_FIELD_VALUE (&send_msg, code, ANT_CODE_OK);

    functional_tests_server_test_single_uint32_putSend (service, &send_msg);
}

void
functional_tests_impl_test_single_repeated_uint32_put (const void *service,
                                                       const cmsg_uint32_array *recv_msg)
{
    ant_result send_msg = ANT_RESULT_INIT;

    NP_ASSERT_TRUE (CMSG_IS_REPEATED_PRESENT (recv_msg, values));
    NP_ASSERT_EQUAL (recv_msg->n_values, 3);
    NP_ASSERT_EQUAL (recv_msg->values[0], 9);
    NP_ASSERT_EQUAL (recv_msg->values[1], 8);
    NP_ASSERT_EQUAL (recv_msg->values[2], 7);

    CMSG_SET_FIELD_VALUE (&send_msg, code, ANT_CODE_OK);

    functional_tests_server_test_single_repeated_uint32_putSend (service, &send_msg);
}

void
functional_tests_impl_test_body_mapped_to_sub_message (const void *service,
                                                       const test_body_msg *recv_msg)
{
    ant_result send_msg = ANT_RESULT_INIT;

    NP_ASSERT_TRUE (CMSG_IS_PTR_PRESENT (recv_msg, field_a));
    NP_ASSERT_STR_EQUAL (recv_msg->field_a, "Bar");

    NP_ASSERT_NULL (recv_msg->field_b);

    NP_ASSERT_TRUE (CMSG_IS_PTR_PRESENT (recv_msg, field_c));

    NP_ASSERT_TRUE (CMSG_IS_PTR_PRESENT (recv_msg->field_c, field_x));
    NP_ASSERT_STR_EQUAL (recv_msg->field_c->field_x, "Hi");

    NP_ASSERT_TRUE (CMSG_IS_PTR_PRESENT (recv_msg->field_c, field_y));
    NP_ASSERT_EQUAL (recv_msg->field_c->field_y, 123);

    CMSG_SET_FIELD_VALUE (&send_msg, code, ANT_CODE_OK);
    CMSG_SET_FIELD_PTR (&send_msg, message, NULL);

    functional_tests_server_test_body_mapped_to_sub_messageSend (service, &send_msg);
}

void
functional_tests_impl_test_body_mapped_to_primitive (const void *service,
                                                     const test_body_msg *recv_msg)
{
    ant_result send_msg = ANT_RESULT_INIT;

    NP_ASSERT_TRUE (CMSG_IS_PTR_PRESENT (recv_msg, field_a));
    NP_ASSERT_STR_EQUAL (recv_msg->field_a, "Bar");

    NP_ASSERT_TRUE (CMSG_IS_PTR_PRESENT (recv_msg, field_b));
    NP_ASSERT_STR_EQUAL (recv_msg->field_b, "Foo");

    NP_ASSERT_NULL (recv_msg->field_c);

    CMSG_SET_FIELD_VALUE (&send_msg, code, ANT_CODE_OK);
    CMSG_SET_FIELD_PTR (&send_msg, message, NULL);

    functional_tests_server_test_body_mapped_to_primitiveSend (service, &send_msg);
}

void
functional_tests_impl_test_body_mapped_to_remaining_multiple_fields (const void *service,
                                                                     const test_body_msg
                                                                     *recv_msg)
{
    ant_result send_msg = ANT_RESULT_INIT;

    NP_ASSERT_TRUE (CMSG_IS_PTR_PRESENT (recv_msg, field_a));
    NP_ASSERT_STR_EQUAL (recv_msg->field_a, "Bar");

    NP_ASSERT_TRUE (CMSG_IS_PTR_PRESENT (recv_msg, field_b));
    NP_ASSERT_STR_EQUAL (recv_msg->field_b, "Foo");

    NP_ASSERT_TRUE (CMSG_IS_PTR_PRESENT (recv_msg, field_c));

    NP_ASSERT_TRUE (CMSG_IS_PTR_PRESENT (recv_msg->field_c, field_x));
    NP_ASSERT_STR_EQUAL (recv_msg->field_c->field_x, "Hi");

    NP_ASSERT_TRUE (CMSG_IS_PTR_PRESENT (recv_msg->field_c, field_y));
    NP_ASSERT_EQUAL (recv_msg->field_c->field_y, 123);

    CMSG_SET_FIELD_VALUE (&send_msg, code, ANT_CODE_OK);
    CMSG_SET_FIELD_PTR (&send_msg, message, NULL);

    functional_tests_server_test_body_mapped_to_remaining_multiple_fieldsSend (service,
                                                                               &send_msg);
}

void
functional_tests_impl_test_body_mapped_to_remaining_single_field (const void *service,
                                                                  const test_body_msg
                                                                  *recv_msg)
{
    ant_result send_msg = ANT_RESULT_INIT;

    NP_ASSERT_TRUE (CMSG_IS_PTR_PRESENT (recv_msg, field_a));
    NP_ASSERT_STR_EQUAL (recv_msg->field_a, "Bar");

    NP_ASSERT_TRUE (CMSG_IS_PTR_PRESENT (recv_msg, field_b));
    NP_ASSERT_STR_EQUAL (recv_msg->field_b, "Foo");

    NP_ASSERT_TRUE (CMSG_IS_PTR_PRESENT (recv_msg, field_c));

    NP_ASSERT_TRUE (CMSG_IS_PTR_PRESENT (recv_msg->field_c, field_x));
    NP_ASSERT_STR_EQUAL (recv_msg->field_c->field_x, "Hi");

    NP_ASSERT_TRUE (CMSG_IS_PTR_PRESENT (recv_msg->field_c, field_y));
    NP_ASSERT_EQUAL (recv_msg->field_c->field_y, 123);

    CMSG_SET_FIELD_VALUE (&send_msg, code, ANT_CODE_OK);
    CMSG_SET_FIELD_PTR (&send_msg, message, NULL);

    functional_tests_server_test_body_mapped_to_remaining_single_fieldSend (service,
                                                                            &send_msg);
}

void
functional_tests_impl_test_body_mapped_to_nothing (const void *service,
                                                   const test_body_msg *recv_msg)
{
    ant_result send_msg = ANT_RESULT_INIT;

    NP_ASSERT_TRUE (CMSG_IS_PTR_PRESENT (recv_msg, field_a));
    NP_ASSERT_STR_EQUAL (recv_msg->field_a, "Bar");

    NP_ASSERT_NULL (recv_msg->field_b);

    NP_ASSERT_NULL (recv_msg->field_c);

    CMSG_SET_FIELD_VALUE (&send_msg, code, ANT_CODE_OK);
    CMSG_SET_FIELD_PTR (&send_msg, message, NULL);

    functional_tests_server_test_body_mapped_to_nothingSend (service, &send_msg);
}

void
functional_tests_impl_test_internal_web_api_info_set (const void *service,
                                                      const internal_api_info_test
                                                      *recv_msg)
{
    ant_result send_msg = ANT_RESULT_INIT;

    NP_ASSERT_NOT_NULL (recv_msg->_api_request_ip_address);
    NP_ASSERT_STR_EQUAL (recv_msg->_api_request_ip_address, "1.2.3.4");

    NP_ASSERT_NOT_NULL (recv_msg->_api_request_username);
    NP_ASSERT_STR_EQUAL (recv_msg->_api_request_username, "user123");

    CMSG_SET_FIELD_VALUE (&send_msg, code, ANT_CODE_OK);

    functional_tests_server_test_internal_web_api_info_setSend (service, &send_msg);
}

void
functional_tests_impl_test_single_data_plus_internal_set (const void *service,
                                                          const single_data_and_internal
                                                          *recv_msg)
{
    ant_result send_msg = ANT_RESULT_INIT;

    NP_ASSERT_NOT_NULL (recv_msg->_api_request_ip_address);
    NP_ASSERT_STR_EQUAL (recv_msg->_api_request_ip_address, "1.2.3.4");

    NP_ASSERT_NOT_NULL (recv_msg->_api_request_username);
    NP_ASSERT_STR_EQUAL (recv_msg->_api_request_username, "user123");

    NP_ASSERT_EQUAL (recv_msg->field_abc, 987);

    CMSG_SET_FIELD_VALUE (&send_msg, code, ANT_CODE_OK);

    functional_tests_server_test_single_data_plus_internal_setSend (service, &send_msg);
}

void
functional_tests_impl_test_multiple_data_plus_internal_set (const void *service,
                                                            const multiple_data_and_internal
                                                            *recv_msg)
{
    ant_result send_msg = ANT_RESULT_INIT;

    NP_ASSERT_NOT_NULL (recv_msg->_api_request_ip_address);
    NP_ASSERT_STR_EQUAL (recv_msg->_api_request_ip_address, "1.2.3.4");

    NP_ASSERT_NOT_NULL (recv_msg->_api_request_username);
    NP_ASSERT_STR_EQUAL (recv_msg->_api_request_username, "user123");

    NP_ASSERT_EQUAL (recv_msg->field_abc, 987);

    CMSG_SET_FIELD_VALUE (&send_msg, code, ANT_CODE_OK);

    functional_tests_server_test_multiple_data_plus_internal_setSend (service, &send_msg);
}

int
set_up (void)
{
    np_mock (_cmsg_proxy_library_handles_load, sm_mock_cmsg_proxy_library_handles_load);
    np_mock (cmsg_create_client_unix, sm_mock_cmsg_create_client_unix);

    cmsg_proxy_init ();
    return 0;
}

int
tear_down (void)
{
    cmsg_proxy_deinit ();
    return 0;
}

void
test_single_bool_get (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    input.url = "/test_single_bool_get";
    input.http_verb = CMSG_HTTP_GET;

    cmsg_proxy (&input, &output);

    NP_ASSERT_STR_EQUAL (output.response_body, "true");
    NP_ASSERT_EQUAL (output.http_status, HTTP_CODE_OK);

    cmsg_proxy_free_output_contents (&output);
}

void
test_single_string_get (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    input.url = "/test_single_string_get";
    input.http_verb = CMSG_HTTP_GET;

    cmsg_proxy (&input, &output);

    NP_ASSERT_STR_EQUAL (output.response_body, "\"single string\"");
    NP_ASSERT_EQUAL (output.http_status, HTTP_CODE_OK);

    cmsg_proxy_free_output_contents (&output);
}

void
test_single_uint32_get (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    input.url = "/test_single_uint32_get";
    input.http_verb = CMSG_HTTP_GET;

    cmsg_proxy (&input, &output);

    NP_ASSERT_STR_EQUAL (output.response_body, "123");
    NP_ASSERT_EQUAL (output.http_status, HTTP_CODE_OK);

    cmsg_proxy_free_output_contents (&output);
}

void
test_single_message_get (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    /* *INDENT-OFF* */
    char *expected_output_json =
        "{"
        "\"value\":false"
        "}";
    /* *INDENT-ON* */

    input.url = "/test_single_message_get";
    input.http_verb = CMSG_HTTP_GET;

    cmsg_proxy (&input, &output);

    NP_ASSERT_STR_EQUAL (output.response_body, expected_output_json);
    NP_ASSERT_EQUAL (output.http_status, HTTP_CODE_OK);

    cmsg_proxy_free_output_contents (&output);
}

void
test_repeated_string_get (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    /* *INDENT-OFF* */
    char *expected_output_json =
        "["
        "\"string1\","
        "\"string2\","
        "\"string3\""
        "]";
    /* *INDENT-ON* */

    input.url = "/test_repeated_string_get";
    input.http_verb = CMSG_HTTP_GET;

    cmsg_proxy (&input, &output);

    NP_ASSERT_STR_EQUAL (output.response_body, expected_output_json);
    NP_ASSERT_EQUAL (output.http_status, HTTP_CODE_OK);

    cmsg_proxy_free_output_contents (&output);
}

void
test_repeated_uint32_get (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    /* *INDENT-OFF* */
    char *expected_output_json =
        "["
        "1,"
        "2,"
        "3"
        "]";
    /* *INDENT-ON* */

    input.url = "/test_repeated_uint32_get";
    input.http_verb = CMSG_HTTP_GET;

    cmsg_proxy (&input, &output);

    NP_ASSERT_STR_EQUAL (output.response_body, expected_output_json);
    NP_ASSERT_EQUAL (output.http_status, HTTP_CODE_OK);

    cmsg_proxy_free_output_contents (&output);
}

void
test_repeated_message_get (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    /* *INDENT-OFF* */
    char *expected_output_json =
        "["
        "{"
        "\"value\":false"
        "},"
        "{"
        "\"value\":false"
        "},"
        "{"
        "\"value\":false"
        "}"
        "]";
    /* *INDENT-ON* */

    input.url = "/test_repeated_message_get";
    input.http_verb = CMSG_HTTP_GET;

    cmsg_proxy (&input, &output);

    NP_ASSERT_STR_EQUAL (output.response_body, expected_output_json);
    NP_ASSERT_EQUAL (output.http_status, HTTP_CODE_OK);

    cmsg_proxy_free_output_contents (&output);
}

void
test_multiple_fields_message_get (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    /* *INDENT-OFF* */
    char *expected_output_json =
        "{"
        "\"inner_bool_msg\":{"
        "\"value\":true"
        "},"
        "\"inner_string\":\"test_string\","
        "\"inner_uint32\":123"
        "}";
    /* *INDENT-ON* */

    input.url = "/test_multiple_fields_message_get";
    input.http_verb = CMSG_HTTP_GET;

    cmsg_proxy (&input, &output);

    NP_ASSERT_STR_EQUAL (output.response_body, expected_output_json);
    NP_ASSERT_EQUAL (output.http_status, HTTP_CODE_OK);

    cmsg_proxy_free_output_contents (&output);
}

void
test_ant_result_get_ok (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    input.url = "/test_ant_result_get_ok";
    input.http_verb = CMSG_HTTP_GET;

    cmsg_proxy (&input, &output);

    NP_ASSERT_NULL (output.response_body);
    NP_ASSERT_EQUAL (output.http_status, HTTP_CODE_OK);

    cmsg_proxy_free_output_contents (&output);
}

void
test_ant_result_get_error (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    /* *INDENT-OFF* */
    char *expected_output_json =
        "{"
        "\"code\":\"ANT_CODE_NOT_FOUND\","
        "\"message\":\"ERROR: Not found\""
        "}";
    /* *INDENT-ON* */

    input.url = "/test_ant_result_get_error";
    input.http_verb = CMSG_HTTP_GET;

    cmsg_proxy (&input, &output);

    NP_ASSERT_STR_EQUAL (output.response_body, expected_output_json);
    NP_ASSERT_EQUAL (output.http_status, HTTP_CODE_NOT_FOUND);

    cmsg_proxy_free_output_contents (&output);
}

void
test_get_error_with_single_data (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    /* *INDENT-OFF* */
    char *expected_output_json =
        "{"
        "\"code\":\"ANT_CODE_NOT_FOUND\","
        "\"message\":\"ERROR: Not found\""
        "}";
    /* *INDENT-ON* */

    input.url = "/test_get_error_with_single_data";
    input.http_verb = CMSG_HTTP_GET;

    cmsg_proxy (&input, &output);

    NP_ASSERT_STR_EQUAL (output.response_body, expected_output_json);
    NP_ASSERT_EQUAL (output.http_status, HTTP_CODE_NOT_FOUND);

    cmsg_proxy_free_output_contents (&output);
}

void
test_get_error_with_multiple_data (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    /* *INDENT-OFF* */
    char *expected_output_json =
        "{"
        "\"code\":\"ANT_CODE_NOT_FOUND\","
        "\"message\":\"ERROR: Not found\""
        "}";
    /* *INDENT-ON* */

    input.url = "/test_get_error_with_multiple_data";
    input.http_verb = CMSG_HTTP_GET;

    cmsg_proxy (&input, &output);

    NP_ASSERT_STR_EQUAL (output.response_body, expected_output_json);
    NP_ASSERT_EQUAL (output.http_status, HTTP_CODE_NOT_FOUND);

    cmsg_proxy_free_output_contents (&output);
}

void
test_single_bool_put (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    /* *INDENT-OFF* */
    char *expected_output_json =
        "{"
        "\"code\":\"ANT_CODE_OK\""
        "}";
    /* *INDENT-ON* */

    input.url = "/test_single_bool_put";
    input.http_verb = CMSG_HTTP_PUT;
    input.data = "false";
    input.data_length = strlen ("false");

    cmsg_proxy (&input, &output);

    NP_ASSERT_STR_EQUAL (output.response_body, expected_output_json);
    NP_ASSERT_EQUAL (output.http_status, HTTP_CODE_OK);

    cmsg_proxy_free_output_contents (&output);
}

void
test_single_bool_put__invalid (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    /* *INDENT-OFF* */
    char *expected_output_json =
        "{"
        "\"code\":\"ANT_CODE_INVALID_ARGUMENT\","
        "\"message\":\"Invalid JSON: invalid token near 'blah'\""
        "}";
    /* *INDENT-ON* */

    input.url = "/test_single_bool_put";
    input.http_verb = CMSG_HTTP_PUT;
    input.data = "blah";
    input.data_length = strlen ("blah");

    cmsg_proxy (&input, &output);

    NP_ASSERT_STR_EQUAL (output.response_body, expected_output_json);
    NP_ASSERT_EQUAL (output.http_status, HTTP_CODE_BAD_REQUEST);

    cmsg_proxy_free_output_contents (&output);
}

void
test_single_string_put (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    /* *INDENT-OFF* */
    char *expected_output_json =
        "{"
        "\"code\":\"ANT_CODE_OK\""
        "}";
    /* *INDENT-ON* */

    input.url = "/test_single_string_put";
    input.http_verb = CMSG_HTTP_PUT;
    input.data = "\"Test String\"";
    input.data_length = strlen ("\"Test String\"");

    cmsg_proxy (&input, &output);

    NP_ASSERT_STR_EQUAL (output.response_body, expected_output_json);
    NP_ASSERT_EQUAL (output.http_status, HTTP_CODE_OK);

    cmsg_proxy_free_output_contents (&output);
}

void
test_single_uint32_put (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    /* *INDENT-OFF* */
    char *expected_output_json =
        "{"
        "\"code\":\"ANT_CODE_OK\""
        "}";
    /* *INDENT-ON* */

    input.url = "/test_single_uint32_put";
    input.http_verb = CMSG_HTTP_PUT;
    input.data = "987";
    input.data_length = strlen ("987");

    cmsg_proxy (&input, &output);

    NP_ASSERT_STR_EQUAL (output.response_body, expected_output_json);
    NP_ASSERT_EQUAL (output.http_status, HTTP_CODE_OK);

    cmsg_proxy_free_output_contents (&output);
}

void
test_single_repeated_uint32_put (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    /* *INDENT-OFF* */
    char *expected_output_json =
        "{"
        "\"code\":\"ANT_CODE_OK\""
        "}";
    /* *INDENT-ON* */

    input.url = "/test_single_repeated_uint32_put";
    input.http_verb = CMSG_HTTP_PUT;
    input.data = "[9, 8, 7]";
    input.data_length = strlen ("[9, 8, 7]");

    cmsg_proxy (&input, &output);

    NP_ASSERT_STR_EQUAL (output.response_body, expected_output_json);
    NP_ASSERT_EQUAL (output.http_status, HTTP_CODE_OK);

    cmsg_proxy_free_output_contents (&output);
}

void
test_body_mapped_to_sub_message (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    /* *INDENT-OFF* */
    const char *input_json =
        "{"
        "\"field_x\":\"Hi\","
        "\"field_y\":123"
        "}";

    const char *expected_output_json =
        "{"
        "\"code\":\"ANT_CODE_OK\""
        "}";
    /* *INDENT-ON* */

    input.url = "/test_body_mapped_to_sub_message/Bar";
    input.http_verb = CMSG_HTTP_POST;
    input.data = input_json;
    input.data_length = strlen (input_json);

    cmsg_proxy (&input, &output);

    NP_ASSERT_EQUAL (output.http_status, HTTP_CODE_OK);
    NP_ASSERT_STR_EQUAL (output.response_body, expected_output_json);

    cmsg_proxy_free_output_contents (&output);
}

void
test_body_mapped_to_primitive (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    const char *input_json = "\"Bar\"";

    /* *INDENT-OFF* */
    const char *expected_output_json =
        "{"
        "\"code\":\"ANT_CODE_OK\""
        "}";
    /* *INDENT-ON* */

    input.url = "/test_body_mapped_to_primitive/Foo";
    input.http_verb = CMSG_HTTP_POST;
    input.data = input_json;
    input.data_length = strlen (input_json);

    cmsg_proxy (&input, &output);

    NP_ASSERT_EQUAL (output.http_status, HTTP_CODE_OK);
    NP_ASSERT_STR_EQUAL (output.response_body, expected_output_json);

    cmsg_proxy_free_output_contents (&output);
}

void
test_body_mapped_to_remaining_multiple_fields (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    /* *INDENT-OFF* */
    const char *input_json =
        "{"
        "\"field_b\":\"Foo\","
        "\"field_c\":{ \"field_x\":\"Hi\",\"field_y\":123}"
        "}";

    const char *expected_output_json =
        "{"
        "\"code\":\"ANT_CODE_OK\""
        "}";
    /* *INDENT-ON* */

    input.url = "/test_body_mapped_to_remaining_multiple_fields/Bar";
    input.http_verb = CMSG_HTTP_POST;
    input.data = input_json;
    input.data_length = strlen (input_json);

    cmsg_proxy (&input, &output);

    NP_ASSERT_EQUAL (output.http_status, HTTP_CODE_OK);
    NP_ASSERT_STR_EQUAL (output.response_body, expected_output_json);

    cmsg_proxy_free_output_contents (&output);
}

void
test_body_mapped_to_remaining_single_field (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    /* *INDENT-OFF* */
    const char *input_json =
        "{"
        "\"field_x\":\"Hi\","
        "\"field_y\": 123"
        "}";

    const char *expected_output_json =
        "{"
        "\"code\":\"ANT_CODE_OK\""
        "}";
    /* *INDENT-ON* */

    input.url = "/test_body_mapped_to_remaining_single_field/Bar/Foo";
    input.http_verb = CMSG_HTTP_POST;
    input.data = input_json;
    input.data_length = strlen (input_json);

    cmsg_proxy (&input, &output);

    NP_ASSERT_EQUAL (output.http_status, HTTP_CODE_OK);
    NP_ASSERT_STR_EQUAL (output.response_body, expected_output_json);

    cmsg_proxy_free_output_contents (&output);
}

void
test_body_mapped_to_nothing (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    /* *INDENT-OFF* */
    const char *expected_error_output =
        "{"
        "\"code\":\"ANT_CODE_INVALID_ARGUMENT\","
        "\"message\":\"Invalid JSON: No JSON data expected for API, but JSON data input\""
        "}";

    const char *expected_ok_output =
        "{"
        "\"code\":\"ANT_CODE_OK\""
        "}";
    /* *INDENT-ON* */

    input.url = "/test_body_mapped_to_nothing/Bar";
    input.http_verb = CMSG_HTTP_POST;

    /* Test with no input JSON */

    cmsg_proxy (&input, &output);

    NP_ASSERT_EQUAL (output.http_status, HTTP_CODE_OK);
    NP_ASSERT_STR_EQUAL (output.response_body, expected_ok_output);

    cmsg_proxy_free_output_contents (&output);
    output.response_body = NULL;

    /* Test with input JSON */
    input.data = "Test Input";
    input.data_length = strlen ("Test Input");

    cmsg_proxy (&input, &output);

    NP_ASSERT_EQUAL (output.http_status, HTTP_CODE_BAD_REQUEST);
    NP_ASSERT_STR_EQUAL (output.response_body, expected_error_output);

    cmsg_proxy_free_output_contents (&output);
}

void
test_internal_web_api_info_set (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    input.url = "/test_internal_web_api_info_set";
    input.http_verb = CMSG_HTTP_GET;
    input.web_api_info.api_request_ip_address = "1.2.3.4";
    input.web_api_info.api_request_username = "user123";

    cmsg_proxy (&input, &output);

    NP_ASSERT_NULL (output.response_body);
    NP_ASSERT_EQUAL (output.http_status, HTTP_CODE_OK);
}

void
test_internal_web_api_info_not_set_by_user (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    /* *INDENT-OFF* */
    const char *input_json =
        "{"
        "\"_api_request_ip_address\":\"1.2.3.4\","
        "\"_api_request_username\":\"user123\""
        "}";

    const char *expected_output_json =
        "{"
        "\"code\":\"ANT_CODE_INVALID_ARGUMENT\","
        "\"message\":\"Invalid JSON: No JSON data expected for API, but JSON data input\""
        "}";
    /* *INDENT-ON* */

    input.url = "/test_internal_web_api_info_set";
    input.http_verb = CMSG_HTTP_GET;
    input.data = input_json;
    input.data_length = strlen (input_json);
    input.web_api_info.api_request_ip_address = "1.2.3.4";
    input.web_api_info.api_request_username = "user123";

    cmsg_proxy (&input, &output);

    NP_ASSERT_EQUAL (output.http_status, HTTP_CODE_BAD_REQUEST);
    NP_ASSERT_STR_EQUAL (output.response_body, expected_output_json);
    cmsg_proxy_free_output_contents (&output);
}

void
test_single_data_plus_internal_set (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    /* *INDENT-OFF* */
    char *expected_output_json =
        "{"
        "\"code\":\"ANT_CODE_OK\""
        "}";
    /* *INDENT-ON* */

    input.url = "/test_single_data_plus_internal_set";
    input.http_verb = CMSG_HTTP_PUT;
    input.data = "987";
    input.data_length = strlen ("987");
    input.web_api_info.api_request_ip_address = "1.2.3.4";
    input.web_api_info.api_request_username = "user123";

    cmsg_proxy (&input, &output);

    NP_ASSERT_STR_EQUAL (output.response_body, expected_output_json);
    NP_ASSERT_EQUAL (output.http_status, HTTP_CODE_OK);
    cmsg_proxy_free_output_contents (&output);
}

void
test_single_data_plus_internal_set_by_user (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    /* *INDENT-OFF* */
    const char *input_json =
        "{"
        "\"_api_request_ip_address\":\"1.2.3.4\","
        "\"field_abc\":987"
        "}";

    const char *expected_output_json =
        "{"
        "\"code\":\"ANT_CODE_INVALID_ARGUMENT\","
        "\"message\":\"Invalid JSON: JSON value or array expected but JSON object given\""
        "}";
    /* *INDENT-ON* */

    input.url = "/test_single_data_plus_internal_set";
    input.http_verb = CMSG_HTTP_PUT;
    input.data = input_json;
    input.data_length = strlen (input_json);
    input.web_api_info.api_request_ip_address = "1.2.3.4";
    input.web_api_info.api_request_username = "user123";

    cmsg_proxy (&input, &output);

    NP_ASSERT_STR_EQUAL (output.response_body, expected_output_json);
    NP_ASSERT_EQUAL (output.http_status, HTTP_CODE_BAD_REQUEST);
    cmsg_proxy_free_output_contents (&output);
}

void
test_multiple_data_plus_internal_set (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    /* *INDENT-OFF* */
    const char *input_json =
        "{"
        "\"field_abc\":987"
        "}";

    char *expected_output_json =
        "{"
        "\"code\":\"ANT_CODE_OK\""
        "}";
    /* *INDENT-ON* */

    input.url = "/test_multiple_data_plus_internal_set";
    input.http_verb = CMSG_HTTP_PUT;
    input.data = input_json;
    input.data_length = strlen (input_json);
    input.web_api_info.api_request_ip_address = "1.2.3.4";
    input.web_api_info.api_request_username = "user123";

    cmsg_proxy (&input, &output);

    NP_ASSERT_STR_EQUAL (output.response_body, expected_output_json);
    NP_ASSERT_EQUAL (output.http_status, HTTP_CODE_OK);
    cmsg_proxy_free_output_contents (&output);
}

void
test_multiple_data_plus_internal_set_by_user (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    /* *INDENT-OFF* */
    const char *input_json =
        "{"
        "\"_api_request_ip_address\":\"1.2.3.4\","
        "\"field_abc\":987"
        "}";

    const char *expected_output_json =
        "{"
        "\"code\":\"ANT_CODE_INVALID_ARGUMENT\","
        "\"message\":\"Invalid JSON: Invalid JSON\""
        "}";
    /* *INDENT-ON* */

    input.url = "/test_multiple_data_plus_internal_set";
    input.http_verb = CMSG_HTTP_PUT;
    input.data = input_json;
    input.data_length = strlen (input_json);
    input.web_api_info.api_request_ip_address = "1.2.3.4";
    input.web_api_info.api_request_username = "user123";

    cmsg_proxy (&input, &output);

    NP_ASSERT_STR_EQUAL (output.response_body, expected_output_json);
    NP_ASSERT_EQUAL (output.http_status, HTTP_CODE_BAD_REQUEST);
    cmsg_proxy_free_output_contents (&output);
}
