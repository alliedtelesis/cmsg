/*
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 *
 * Functional tests for the CMSG proxy library.
 */

#include <np.h>
#include "cmsg_proxy_passthrough.c"
#include "cmsg_proxy_passthrough_functional_tests_proxy_def.h"
#include "cmsg_proxy_passthrough_functional_tests_api_auto.h"
#include "cmsg_proxy_passthrough_functional_tests_impl_auto.h"

/* *INDENT-OFF* */
char *test_input_json =
    "["
    "\"string1\","
    "\"string2\","
    "\"string3\""
    "]";
/* *INDENT-ON* */

char *test_output_string = "Test is OK";
int test_output_status = 204;

/**
 * Mock the 'cmsg_proxy_library_handles_load' function to simply call
 * the autogenerated 'cmsg_proxy_array_get' and 'cmsg_proxy_array_size'
 * functions that are linked with the executable. Usually the CMSG proxy
 * will dynamically load every '_proxy_def' library that is on the device
 * however to simplify the functional tests we don't do this.
 */
void
sm_mock_cmsg_proxy_passthrough_library_handle_load (const char *library_name)
{
    _load_library_info (cmsg_proxy_array_get, cmsg_proxy_array_size);
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
functional_tests_impl_passthrough (const void *service, const passthrough_request *recv_msg)
{
    passthrough_response send_msg = PASSTHROUGH_RESPONSE_INIT;

    if (strstr (recv_msg->path, "_get"))
    {
        NP_ASSERT_STR_EQUAL (recv_msg->method, "GET");
    }
    else if (strstr (recv_msg->path, "_put"))
    {
        NP_ASSERT_STR_EQUAL (recv_msg->method, "PUT");
    }
    else if (strstr (recv_msg->path, "_post"))
    {
        NP_ASSERT_STR_EQUAL (recv_msg->method, "POST");
    }
    else if (strstr (recv_msg->path, "_patch"))
    {
        NP_ASSERT_STR_EQUAL (recv_msg->method, "PATCH");
    }
    else
    {
        NP_ASSERT (strstr (recv_msg->path, "_delete"));
        NP_ASSERT_STR_EQUAL (recv_msg->method, "DELETE");
    }

    CMSG_SET_FIELD_PTR (&send_msg, response_body, test_output_string);
    CMSG_SET_FIELD_VALUE (&send_msg, status_code, test_output_status);

    functional_tests_server_passthroughSend (service, &send_msg);
}

int
set_up (void)
{
    np_mock (cmsg_proxy_passthrough_library_handle_load,
             sm_mock_cmsg_proxy_passthrough_library_handle_load);
    np_mock (cmsg_create_client_unix, sm_mock_cmsg_create_client_unix);

    cmsg_proxy_passthrough_init ("passthrough");
    return 0;
}

int
tear_down (void)
{
    cmsg_proxy_passthrough_deinit ();
    return 0;
}

void
test_simple_passthrough_get (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    input.url = "/test_passthrough_get";
    input.http_verb = CMSG_HTTP_GET;
    input.data = test_input_json;
    input.data_length = strlen (test_input_json);

    cmsg_proxy_passthrough (&input, &output);

    NP_ASSERT_STR_EQUAL (output.response_body, test_output_string);
    NP_ASSERT_EQUAL (output.http_status, test_output_status);

    cmsg_proxy_passthrough_free_output_contents (&output);
}

void
test_simple_passthrough_put (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    input.url = "/test_passthrough_put";
    input.http_verb = CMSG_HTTP_PUT;
    input.data = test_input_json;
    input.data_length = strlen (test_input_json);

    cmsg_proxy_passthrough (&input, &output);

    NP_ASSERT_STR_EQUAL (output.response_body, test_output_string);
    NP_ASSERT_EQUAL (output.http_status, test_output_status);

    cmsg_proxy_passthrough_free_output_contents (&output);
}

void
test_simple_passthrough_post (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    input.url = "/test_passthrough_post";
    input.http_verb = CMSG_HTTP_POST;
    input.data = test_input_json;
    input.data_length = strlen (test_input_json);

    cmsg_proxy_passthrough (&input, &output);

    NP_ASSERT_STR_EQUAL (output.response_body, test_output_string);
    NP_ASSERT_EQUAL (output.http_status, test_output_status);

    cmsg_proxy_passthrough_free_output_contents (&output);
}

void
test_simple_passthrough_patch (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    input.url = "/test_passthrough_patch";
    input.http_verb = CMSG_HTTP_PATCH;
    input.data = test_input_json;
    input.data_length = strlen (test_input_json);

    cmsg_proxy_passthrough (&input, &output);

    NP_ASSERT_STR_EQUAL (output.response_body, test_output_string);
    NP_ASSERT_EQUAL (output.http_status, test_output_status);

    cmsg_proxy_passthrough_free_output_contents (&output);
}

void
test_simple_passthrough_delete (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    input.url = "/test_passthrough_delete";
    input.http_verb = CMSG_HTTP_DELETE;
    input.data = test_input_json;
    input.data_length = strlen (test_input_json);

    cmsg_proxy_passthrough (&input, &output);

    NP_ASSERT_STR_EQUAL (output.response_body, test_output_string);
    NP_ASSERT_EQUAL (output.http_status, test_output_status);

    cmsg_proxy_passthrough_free_output_contents (&output);
}

/**
 * Mock the cmsg_api_invoke function to simulate it returning CMSG_RET_ERR.
 */
int
sm_mock_cmsg_api_invoke_err (cmsg_client *client, const cmsg_api_descriptor *cmsg_desc,
                             int method_index, const ProtobufCMessage *send_msg,
                             ProtobufCMessage **recv_msg)
{
    return CMSG_RET_ERR;
}

/*
 * Test that cmsg_proxy correctly handles an error happening within
 * cmsg_proxy_convert_json_to_protobuf, producing ANT_CODE_INTERNAL.
 */
void
test_cmsg_proxy_passthrough__error (void)
{
    cmsg_proxy_input input = { 0 };
    cmsg_proxy_output output = { 0 };

    /* *INDENT-OFF* */
    char *expected_output_response_body =
        "{"
        "\"code\":\"ANT_CODE_INTERNAL\","
        "\"message\":\"Error calling passthrough API\""
        "}";
    /* *INDENT-ON* */

    input.url = "/test_passthrough_put";
    input.http_verb = CMSG_HTTP_PUT;
    input.data = test_input_json;
    input.data_length = strlen (test_input_json);

    np_mock (cmsg_api_invoke, sm_mock_cmsg_api_invoke_err);
    cmsg_proxy_passthrough (&input, &output);

    NP_ASSERT_STR_EQUAL (output.response_body, expected_output_response_body);
    NP_ASSERT_EQUAL (output.http_status, HTTP_CODE_INTERNAL_SERVER_ERROR);

    cmsg_proxy_passthrough_free_output_contents (&output);
}
