/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */

#include <np.h>
#include "cmsg_proxy.c"
#include "cmsg_proxy_unit_tests_proxy_def.h"
#include "cmsg_proxy_unit_tests_api_auto.h"

/**
 * Novaprova setup function
 */
static int
setup (void)
{
    /* Create GNode cmsg proxy entry tree */
    proxy_entries_tree = g_node_new (g_strdup ("CMSG_API"));

    return 0;
}

/**
 * Function Tested: _cmsg_proxy_service_info_init()
 *
 * Tests that the proxy tree is the correct length after _cmsg_proxy_service_info_init()
 * is called.
 */
void
test_cmsg_proxy_service_info_init__list_length (void)
{
    _cmsg_proxy_service_info_init (cmsg_proxy_array_get (), cmsg_proxy_array_size ());
    NP_ASSERT_EQUAL (g_node_n_nodes (proxy_entries_tree, G_TRAVERSE_ALL), 6);

    _cmsg_proxy_service_info_init (cmsg_proxy_array_get (), cmsg_proxy_array_size ());
    NP_ASSERT_EQUAL (g_node_n_nodes (proxy_entries_tree, G_TRAVERSE_ALL), 6);

    _cmsg_proxy_service_info_init (cmsg_proxy_array_get (), cmsg_proxy_array_size ());
    NP_ASSERT_EQUAL (g_node_n_nodes (proxy_entries_tree, G_TRAVERSE_ALL), 6);
}

/**
 * Function Tested: _cmsg_proxy_service_info_init()
 *
 * Tests that the first proxy list entry points at the expected autogenerated data.
 */
void
test_cmsg_proxy_service_info_init__list_entries (void)
{
    _cmsg_proxy_service_info_init (cmsg_proxy_array_get (), cmsg_proxy_array_size ());
    cmsg_service_info *entry =
        _cmsg_proxy_find_service_from_url_and_verb ("/v1/test", CMSG_HTTP_PUT,
                                                    NULL);

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
test_cmsg_proxy_find_service_from_url_and_verb (void)
{
    const cmsg_service_info *entry;
    json_t *json_object;

    _cmsg_proxy_service_info_init (cmsg_proxy_array_get (), cmsg_proxy_array_size ());

    entry = _cmsg_proxy_find_service_from_url_and_verb ("/v1/test",
                                                        CMSG_HTTP_PUT, &json_object);
    NP_ASSERT_PTR_NOT_EQUAL (entry, NULL);
    NP_ASSERT_EQUAL (entry->http_verb, CMSG_HTTP_PUT);

    entry = _cmsg_proxy_find_service_from_url_and_verb ("BAD URL", CMSG_HTTP_PUT,
                                                        &json_object);
    NP_ASSERT_PTR_EQUAL (entry, NULL);

    entry = _cmsg_proxy_find_service_from_url_and_verb ("/v1/test",
                                                        CMSG_HTTP_GET, &json_object);
    NP_ASSERT_PTR_NOT_EQUAL (entry, NULL);
    NP_ASSERT_EQUAL (entry->http_verb, CMSG_HTTP_GET);

    entry = _cmsg_proxy_find_service_from_url_and_verb ("/v1/test",
                                                        CMSG_HTTP_PATCH, &json_object);
    NP_ASSERT_PTR_EQUAL (entry, NULL);
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
    json_t *json_obj = json_loads ("{\n    \"value\":true\n}", 0, &error);

    ret = _cmsg_proxy_convert_json_to_protobuf (json_obj,
                                                &cmsg_proxy_unit_tests_cmsg_bool_descriptor,
                                                &output);

    json_decref (json_obj);
    free (output);
    NP_ASSERT_TRUE (ret);
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
    bool ret;
    json_error_t error;
    json_t *json_obj;

    /* value is not quoted correctly */
    json_obj = json_loads ("{\n    value\":true\n}", 0, &error);

    ret = _cmsg_proxy_convert_json_to_protobuf (json_obj,
                                                &cmsg_proxy_unit_tests_cmsg_bool_descriptor,
                                                &output);

    NP_ASSERT_FALSE (ret);
    json_decref (json_obj);

    /* json string is missing closing bracket */
    json_obj = json_loads ("{\n    \"value\":true\n", 0, &error);

    ret = _cmsg_proxy_convert_json_to_protobuf (json_obj,
                                                &cmsg_proxy_unit_tests_cmsg_bool_descriptor,
                                                &output);
    json_decref (json_obj);
    NP_ASSERT_FALSE (ret);
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
    _cmsg_proxy_service_info_init (cmsg_proxy_array_get (), cmsg_proxy_array_size ());

    _cmsg_proxy_clients_init ();
    NP_ASSERT_EQUAL (g_list_length (proxy_clients_list), 1);

    _cmsg_proxy_clients_init ();
    NP_ASSERT_EQUAL (g_list_length (proxy_clients_list), 1);
}

/**
 * Function Tested: _cmsg_proxy_convert_protobuf_to_json()
 *
 * Tests that valid input is correctly converted into a json string
 */
void
test_cmsg_proxy_convert_protobuf_to_json (void)
{
    cmsg_proxy_unit_tests_cmsg_bool proto_msg = CMSG_PROXY_UNIT_TESTS_CMSG_BOOL_INIT;
    char *json_str = NULL;
    bool ret = false;

    CMSG_SET_FIELD_VALUE (&proto_msg, value, true);

    ret = _cmsg_proxy_convert_protobuf_to_json ((ProtobufCMessage *) &proto_msg, &json_str);

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
    _cmsg_proxy_service_info_init (cmsg_proxy_array_get (), cmsg_proxy_array_size ());

    _cmsg_proxy_clients_init ();
    NP_ASSERT_EQUAL (g_list_length (proxy_clients_list), 1);

    cmsg_proxy_deinit ();
    NP_ASSERT_PTR_EQUAL (proxy_clients_list, NULL);
    NP_ASSERT_PTR_EQUAL (proxy_entries_tree, NULL);
    NP_ASSERT_PTR_EQUAL (library_handles_list, NULL);
}
