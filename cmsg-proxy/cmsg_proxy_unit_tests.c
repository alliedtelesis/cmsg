/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */

#include <np.h>
#include "cmsg_proxy_mem.c"
#include "cmsg_proxy.c"
#include "cmsg_proxy_unit_tests_proxy_def.h"
#include "cmsg_proxy_unit_tests_api_auto.h"
#include "cmsg_proxy_unit_tests_impl_auto.h"

/**
 * Novaprova setup function
 */
int
setup (void)
{
    return 0;
}

/**
 * Function Tested: _cmsg_proxy_service_info_init()
 *
 * Tests that the proxy tree is the correct length after _cmsg_proxy_service_info_init()
 * is called. Given the following set of RPCs:
 * PUT: /v1/test
 * GET: /v1/test
 * GET: /v1/test/test1
 * GET: /v1/test/query_param/{key_a}/{key_c}
 *
 * the resulting proxy tree will have a total of 10 nodes:
 *                  ---PUT
 *                 /
 *                /----GET
 *               /
 * "v1"---"test"-------"test1"---GET
 *               \
 *                -----"query_param"---"{key_a}"---"{key_c}"---GET
 */
void
test_cmsg_proxy_service_info_init__list_length (void)
{
    proxy_entries_tree = g_node_new (g_strdup ("CMSG_API"));

    _cmsg_proxy_service_info_init (cmsg_proxy_array_get (), cmsg_proxy_array_size ());
    NP_ASSERT_EQUAL (g_node_n_nodes (proxy_entries_tree, G_TRAVERSE_ALL), 18);

    _cmsg_proxy_service_info_init (cmsg_proxy_array_get (), cmsg_proxy_array_size ());
    NP_ASSERT_EQUAL (g_node_n_nodes (proxy_entries_tree, G_TRAVERSE_ALL), 18);

    _cmsg_proxy_service_info_init (cmsg_proxy_array_get (), cmsg_proxy_array_size ());
    NP_ASSERT_EQUAL (g_node_n_nodes (proxy_entries_tree, G_TRAVERSE_ALL), 18);
}

/**
 * Function Tested: _cmsg_proxy_service_info_init()
 *
 * Tests that the first proxy list entry points at the expected autogenerated data.
 */
void
test_cmsg_proxy_service_info_init__list_entries (void)
{
    proxy_entries_tree = g_node_new (g_strdup ("CMSG_API"));

    _cmsg_proxy_service_info_init (cmsg_proxy_array_get (), cmsg_proxy_array_size ());
    cmsg_service_info *entry =
        _cmsg_proxy_find_service_from_url_and_verb ("/v1/test", CMSG_HTTP_PUT, NULL);

    NP_ASSERT_PTR_EQUAL (entry, cmsg_proxy_array_get ());
    NP_ASSERT_PTR_EQUAL (entry->service_descriptor,
                         &cmsg_proxy_unit_tests_interface_descriptor);
    NP_ASSERT_EQUAL (entry->http_verb, CMSG_HTTP_PUT);
}

/**
 * Function Tested: _cmsg_proxy_find_service_from_url_and_verb()
 *
 * Tests that the function finds the correct service entry when passed
 * a known URL and verb or returns NULL for an unknown URL and verb.
 */
void
test_cmsg_proxy_find_service_from_url_and_verb__finds_correct_service_entry (void)
{
    const cmsg_service_info *entry;
    GList *url_parameters = NULL;

    proxy_entries_tree = g_node_new (g_strdup ("CMSG_API"));

    _cmsg_proxy_service_info_init (cmsg_proxy_array_get (), cmsg_proxy_array_size ());

    entry = _cmsg_proxy_find_service_from_url_and_verb ("/v1/test", CMSG_HTTP_PUT,
                                                        &url_parameters);
    NP_ASSERT_PTR_NOT_EQUAL (entry, NULL);
    NP_ASSERT_EQUAL (entry->http_verb, CMSG_HTTP_PUT);

    entry = _cmsg_proxy_find_service_from_url_and_verb ("BAD URL", CMSG_HTTP_PUT,
                                                        &url_parameters);
    NP_ASSERT_PTR_EQUAL (entry, NULL);

    entry = _cmsg_proxy_find_service_from_url_and_verb ("/v1/test", CMSG_HTTP_GET,
                                                        &url_parameters);
    NP_ASSERT_PTR_NOT_EQUAL (entry, NULL);
    NP_ASSERT_EQUAL (entry->http_verb, CMSG_HTTP_GET);

    entry = _cmsg_proxy_find_service_from_url_and_verb ("/v1/test", CMSG_HTTP_PATCH,
                                                        &url_parameters);
    NP_ASSERT_PTR_EQUAL (entry, NULL);

    g_list_free_full (url_parameters, _cmsg_proxy_free_url_parameter);
}

/**
 * Function Tested: _cmsg_proxy_convert_json_to_protobuf()
 *
 * Tests that valid input is correctly converted into a protobuf message
 */
void
test_cmsg_proxy_convert_json_to_protobuf__valid_input (void)
{
    ProtobufCMessage *output = NULL;
    bool ret;
    json_error_t error;
    char *error_message = NULL;
    json_t *json_obj = json_loads ("{\n    \"value\":true\n}", 0, &error);

    ret = _cmsg_proxy_convert_json_to_protobuf (json_obj,
                                                &cmsg_bool_descriptor,
                                                &output, &error_message);

    json_decref (json_obj);
    free (output);
    if (error_message)
    {
        free (error_message);
        error_message = NULL;
    }
    NP_ASSERT_TRUE (ret == 0);
}

/**
 * Function Tested: _cmsg_proxy_convert_json_to_protobuf()
 *
 * Tests that invalid input fails to be converted into a protobuf message
 */
void
test_cmsg_proxy_convert_json_to_protobuf__invalid_input (void)
{
    ProtobufCMessage *output = NULL;
    int ret;
    json_error_t error;
    json_t *json_obj;
    char *error_message = NULL;

    /* value is not quoted correctly */
    json_obj = json_loads ("{\n    value\":true\n}", 0, &error);

    ret = _cmsg_proxy_convert_json_to_protobuf (json_obj,
                                                &cmsg_bool_descriptor,
                                                &output, &error_message);

    if (error_message)
    {
        free (error_message);
        error_message = NULL;
    }

    NP_ASSERT_FALSE (ret == 0);
    json_decref (json_obj);

    /* json string is missing closing bracket */
    json_obj = json_loads ("{\n    \"value\":true\n", 0, &error);

    ret = _cmsg_proxy_convert_json_to_protobuf (json_obj,
                                                &cmsg_bool_descriptor,
                                                &output, &error_message);

    if (error_message)
    {
        free (error_message);
        error_message = NULL;
    }

    json_decref (json_obj);
    NP_ASSERT_FALSE (ret == 0);
}

/**
 * Function Tested: _cmsg_proxy_create_client()
 *
 * Tests that the function correctly creates a CMSG client
 * from a valid descriptor
 */
void
test_cmsg_proxy_create_client (void)
{
    _cmsg_proxy_create_client (&cmsg_proxy_unit_tests_interface_descriptor);

    NP_ASSERT_EQUAL (g_list_length (proxy_clients_list), 1);
}

struct cmsg_client *
sm_mock_cmsg_create_client_unix__returns_null (const ProtobufCServiceDescriptor *descriptor)
{
    return NULL;
}

/**
 * Function Tested: _cmsg_proxy_create_client()
 *
 * Tests that no memory is leaked if the internal cmsg_create_client_unix()
 * function fails
 */
void
test_cmsg_proxy_create_client__memory_leaks (void)
{
    np_mock (cmsg_create_client_unix, sm_mock_cmsg_create_client_unix__returns_null);

    np_syslog_ignore ("Failed to create client for service:");

    _cmsg_proxy_create_client (&cmsg_proxy_unit_tests_interface_descriptor);
    _cmsg_proxy_create_client (&cmsg_proxy_unit_tests_interface_descriptor);

    NP_ASSERT_EQUAL (g_list_length (proxy_clients_list), 0);
}

/**
 * Function Tested: _cmsg_proxy_clients_init()
 *
 * Tests that the function correctly creates the required CMSG clients
 */
void
test_cmsg_proxy_clients_init (void)
{
    proxy_entries_tree = g_node_new (g_strdup ("CMSG_API"));

    _cmsg_proxy_service_info_init (cmsg_proxy_array_get (), cmsg_proxy_array_size ());

    _cmsg_proxy_clients_init ();
    NP_ASSERT_EQUAL (g_list_length (proxy_clients_list), 1);

    _cmsg_proxy_clients_init ();
    NP_ASSERT_EQUAL (g_list_length (proxy_clients_list), 1);
}

/**
 * Function Tested: _cmsg_proxy_protobuf2json_string()
 *
 * Tests that valid input is correctly converted into a json string
 */
void
test_cmsg_proxy_protobuf2json_string (void)
{
    cmsg_bool proto_msg = CMSG_BOOL_INIT;
    char *json_str = NULL;
    bool ret = false;

    CMSG_SET_FIELD_VALUE (&proto_msg, value, true);

    ret = _cmsg_proxy_protobuf2json_string ((ProtobufCMessage *) &proto_msg, &json_str);

    NP_ASSERT_TRUE (ret);
    NP_ASSERT_STR_EQUAL (json_str, "{\n    \"value\": true\n}");

    free (json_str);
}

/**
 * Function Tested: _cmsg_proxy_service_info_add()
 *
 * Tests that input URL string is correctly tokenized into the tree
 */
void
test_cmsg_proxy_service_info_add (void)
{
    cmsg_service_info *array = cmsg_proxy_array_get ();

    proxy_entries_tree = g_node_new (g_strdup ("CMSG_API"));

    _cmsg_proxy_service_info_add (&array[2]);
    NP_ASSERT_EQUAL (g_node_n_nodes (proxy_entries_tree, G_TRAVERSE_ALL), 5);

    _cmsg_proxy_service_info_add (&array[0]);
    NP_ASSERT_EQUAL (g_node_n_nodes (proxy_entries_tree, G_TRAVERSE_ALL), 6);

    /* Add the same service info again. */
    _cmsg_proxy_service_info_add (&array[0]);
    NP_ASSERT_EQUAL (g_node_n_nodes (proxy_entries_tree, G_TRAVERSE_ALL), 6);

    /* Add the different service info with same URL. */
    _cmsg_proxy_service_info_add (&array[1]);
    NP_ASSERT_EQUAL (g_node_n_nodes (proxy_entries_tree, G_TRAVERSE_ALL), 6);
}

/**
 * Function Tested: _cmsg_proxy_deinit()
 *
 * Tests that dynamically allocated memory is freed properly
 */
void
test_cmsg_proxy_deinit (void)
{
    proxy_entries_tree = g_node_new (g_strdup ("CMSG_API"));

    _cmsg_proxy_service_info_init (cmsg_proxy_array_get (), cmsg_proxy_array_size ());

    _cmsg_proxy_clients_init ();
    NP_ASSERT_EQUAL (g_list_length (proxy_clients_list), 1);

    cmsg_proxy_deinit ();
    NP_ASSERT_PTR_EQUAL (proxy_clients_list, NULL);
    NP_ASSERT_PTR_EQUAL (proxy_entries_tree, NULL);
    NP_ASSERT_PTR_EQUAL (library_handles_list, NULL);
}

/**
 * Function Tested: cmsg_proxy()
 *
 * Tests that invalid JSON input is appropriately handled.
 */
void
test_cmsg_proxy__invalid_json_input (void)
{
    bool request_handled;
    char *output_json;
    int http_status;

    /* *INDENT-OFF* */
    char *expected_output_json =
        "{\n"
        "    \"code\": \"ANT_CODE_INVALID_ARGUMENT\",\n"
        "    \"message\": \"Invalid JSON: string or '}' expected near end of file\"\n"
        "}";
    /* *INDENT-ON* */

    proxy_entries_tree = g_node_new (g_strdup ("CMSG_API"));
    _cmsg_proxy_service_info_init (cmsg_proxy_array_get (), cmsg_proxy_array_size ());

    request_handled =
        cmsg_proxy ("/v1/test", NULL, CMSG_HTTP_PUT, "{", &output_json, &http_status);

    NP_ASSERT_TRUE (request_handled);
    NP_ASSERT_STR_EQUAL (output_json, expected_output_json);
    NP_ASSERT_EQUAL (http_status, 400);

    free (output_json);
}

/**
 * Function Tested: _cmsg_proxy_parse_query_parameters()
 *
 * Tests that a query string is correctly parsed and that any parsed query
 * parameters do not overwrite parameters with identical keys already in the list.
 */
void
test_cmsg_proxy_parse_query_parameters (void)
{
    GList *url_parameters = NULL;
    GList *matching_param = NULL;

    proxy_entries_tree = g_node_new (g_strdup ("CMSG_API"));
    _cmsg_proxy_service_info_init (cmsg_proxy_array_get (), cmsg_proxy_array_size ());

    /* URL: /v1/test/query_param/{key_a}/{key_c} */
    _cmsg_proxy_get_service_and_parameters ("/v1/test/query_param/AA/CC",
                                            "key_a=WW&key_b=XX&key_c=YY&key_d=ZZ",
                                            CMSG_HTTP_GET, &url_parameters);

    /* There should be 4 parameters in the list (skip duplicate keys) */
    NP_ASSERT_EQUAL (g_list_length (url_parameters), 4);

    /* The parameter values for "key_a" and "key_c" should be "AA" and "CC" as
     * set in the URL. */
    matching_param = g_list_find_custom (url_parameters, "key_a",
                                         _cmsg_proxy_param_name_matches);
    NP_ASSERT_STR_EQUAL (((cmsg_url_parameter *) matching_param->data)->value, "AA");
    matching_param = g_list_find_custom (url_parameters, "key_c",
                                         _cmsg_proxy_param_name_matches);
    NP_ASSERT_STR_EQUAL (((cmsg_url_parameter *) matching_param->data)->value, "CC");

    /* The parameter values for "key_b" and "key_d" should match the values
     * provided in the query string, "XX" and "ZZ" */
    matching_param = g_list_find_custom (url_parameters, "key_b",
                                         _cmsg_proxy_param_name_matches);
    NP_ASSERT_STR_EQUAL (((cmsg_url_parameter *) matching_param->data)->value, "XX");
    matching_param = g_list_find_custom (url_parameters, "key_d",
                                         _cmsg_proxy_param_name_matches);
    NP_ASSERT_STR_EQUAL (((cmsg_url_parameter *) matching_param->data)->value, "ZZ");

    g_list_free_full (url_parameters, _cmsg_proxy_free_url_parameter);
}

/**
 * Function Tested: _cmsg_proxy_find_service_from_url_and_verb()
 *
 * Tests that the function returns a service info entry for an RPC's
 * URL and each of its additional bindings, and that they all point to
 * the same API function.
 */
void
test_cmsg_proxy_find_service_from_url_and_verb__additional_bindings_use_same_api (void)
{
    GList *url_parameters = NULL;
    const cmsg_service_info *entry1;
    const cmsg_service_info *entry2;
    const cmsg_service_info *entry3;
    const char *url1 = "/v1/test/additional_bindings/test_get/value_a/value_b";
    const char *url2 = "/v1/test/additional_bindings/test_post";
    const char *url3 = "/v1/test/additional_bindings/test_get/value_a";

    proxy_entries_tree = g_node_new (g_strdup ("CMSG_API"));
    _cmsg_proxy_service_info_init (cmsg_proxy_array_get (), cmsg_proxy_array_size ());

    entry1 = _cmsg_proxy_find_service_from_url_and_verb (url1, CMSG_HTTP_GET,
                                                         &url_parameters);
    NP_ASSERT_PTR_NOT_EQUAL (entry1, NULL);
    NP_ASSERT_PTR_EQUAL (entry1->api_ptr,
                         &cmsg_proxy_unit_tests_interface_api_test_additional_bindings);
    NP_ASSERT_EQUAL (entry1->http_verb, CMSG_HTTP_GET);

    entry2 = _cmsg_proxy_find_service_from_url_and_verb (url2, CMSG_HTTP_POST,
                                                         &url_parameters);
    NP_ASSERT_PTR_NOT_EQUAL (entry2, NULL);
    NP_ASSERT_PTR_EQUAL (entry2->api_ptr,
                         &cmsg_proxy_unit_tests_interface_api_test_additional_bindings);
    NP_ASSERT_EQUAL (entry2->http_verb, CMSG_HTTP_POST);

    entry3 = _cmsg_proxy_find_service_from_url_and_verb (url3, CMSG_HTTP_GET,
                                                         &url_parameters);
    NP_ASSERT_PTR_NOT_EQUAL (entry3, NULL);
    NP_ASSERT_PTR_EQUAL (entry3->api_ptr,
                         &cmsg_proxy_unit_tests_interface_api_test_additional_bindings);
    NP_ASSERT_EQUAL (entry3->http_verb, CMSG_HTTP_GET);

    g_list_free_full (url_parameters, _cmsg_proxy_free_url_parameter);
}

/**
 * Function Tested: cmsg_proxy_mem_init()
 *
 * Tests that the function initialises the 'cmsg_proxy_mtype' variable
 * to the correct value.
 */
void
test_cmsg_proxy_mem_init (void)
{
    cmsg_proxy_mem_init (1);

    NP_ASSERT_EQUAL (cmsg_proxy_mtype, 1);
}

void
sm_mock_g_mem_record_alloc__do_nothing (void *ptr, int type, const char *filename, int line)
{
    return;
}

void
sm_mock_g_mem_record_free__do_nothing (void *ptr, int type, const char *filename, int line)
{
    return;
}

/**
 * Function Tested: cmsg_proxy_mem_calloc()
 *
 * Tests that the function returns a pointer to dynamically allocated memory
 */
void
test_cmsg_proxy_mem_calloc (void)
{
    int *ptr = NULL;

    np_mock (g_mem_record_alloc, sm_mock_g_mem_record_alloc__do_nothing);
    np_mock (g_mem_record_free, sm_mock_g_mem_record_free__do_nothing);
    cmsg_proxy_mem_init (1);

    ptr = CMSG_PROXY_CALLOC (1, sizeof (int));

    NP_ASSERT_PTR_NOT_EQUAL (ptr, NULL);

    CMSG_PROXY_FREE (ptr);
}

/**
 * Function Tested: cmsg_proxy_mem_asprintf()
 *
 * Tests that the function returns a printed string in
 * dynamically allocated memory
 */
void
test_cmsg_proxy_mem_asprintf (void)
{
    char *str = NULL;

    np_mock (g_mem_record_alloc, sm_mock_g_mem_record_alloc__do_nothing);
    np_mock (g_mem_record_free, sm_mock_g_mem_record_free__do_nothing);
    cmsg_proxy_mem_init (1);

    CMSG_PROXY_ASPRINTF (&str, "%s", "TEST");

    NP_ASSERT_STR_EQUAL (str, "TEST");

    CMSG_PROXY_FREE (str);
}

/**
 * Function Tested: cmsg_proxy_mem_strdup()
 *
 * Tests that the function returns a printed string in
 * dynamically allocated memory
 */
void
test_cmsg_proxy_mem_strdup (void)
{
    char *str = NULL;

    np_mock (g_mem_record_alloc, sm_mock_g_mem_record_alloc__do_nothing);
    np_mock (g_mem_record_free, sm_mock_g_mem_record_free__do_nothing);
    cmsg_proxy_mem_init (1);

    str = CMSG_PROXY_STRDUP ("TEST");

    NP_ASSERT_STR_EQUAL (str, "TEST");

    CMSG_PROXY_FREE (str);
}

/**
 * Function Tested: cmsg_proxy_mem_strndup()
 *
 * Tests that the function returns a printed string of up
 * to X characters in dynamically allocated memory
 */
void
test_cmsg_proxy_mem_strndup (void)
{
    char *str = NULL;

    np_mock (g_mem_record_alloc, sm_mock_g_mem_record_alloc__do_nothing);
    np_mock (g_mem_record_free, sm_mock_g_mem_record_free__do_nothing);
    cmsg_proxy_mem_init (1);

    str = CMSG_PROXY_STRNDUP ("TEST1234", 6);

    NP_ASSERT_STR_EQUAL (str, "TEST12");

    CMSG_PROXY_FREE (str);
}

/**
 * Function Tested: test_cmsg_proxy_mem_free()
 *
 * The above tests have already tested that the function correctly
 * frees memory however test that the function handles a NULL input.
 */
void
test_cmsg_proxy_mem_free__handles_NULL (void)
{
    np_mock (g_mem_record_free, sm_mock_g_mem_record_free__do_nothing);
    cmsg_proxy_mem_init (1);

    CMSG_PROXY_FREE (NULL);
}

#include "cmsg_proxy_unit_tests_functional.c"
