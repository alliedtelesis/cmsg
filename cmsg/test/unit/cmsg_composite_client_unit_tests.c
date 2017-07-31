/*
 * Unit tests for cmsg_composite_client.c
 *
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */

#include <np.h>
#include <cmsg_client.h>
#include <cmsg_composite_client.h>

/**
 * This informs the compiler that the function is, in fact, being used even though it
 * doesn't look like it. This is useful for static functions that get found by NovaProva
 * using debug symbols.
 */
#define USED __attribute__ ((used))


extern int32_t cmsg_composite_client_invoke (ProtobufCService *service,
                                             uint32_t method_index,
                                             const ProtobufCMessage *input,
                                             ProtobufCClosure closure, void *closure_data);

ProtobufCServiceDescriptor dummy_service_descriptor = {
    .name = "dummy",
};

ProtobufCService dummy_service = {
    .descriptor = &dummy_service_descriptor,
};

static const uint16_t tipc_port = 18888;

void
test_cmsg_composite_client_new__success (void)
{
    cmsg_client *comp_client = cmsg_composite_client_new (&dummy_service_descriptor);

    NP_ASSERT_NOT_NULL (comp_client);

    cmsg_client_destroy (comp_client);
}

static int
sm_mock_pthread_mutex_init_fail (pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    return -1;
}

void
test_cmsg_composite_client_new__pthread_mutex_init_failure (void)
{
    cmsg_client *comp_client = NULL;

    np_syslog_ignore (".*");
    np_mock (pthread_mutex_init, sm_mock_pthread_mutex_init_fail);

    comp_client = cmsg_composite_client_new (&dummy_service_descriptor);

    NP_ASSERT_NULL (comp_client);
}

static int
sm_mock_cmsg_service_port_get (const char *name, const char *proto)
{
    if ((strcmp (name, "test") == 0) && (strcmp (proto, "tipc") == 0))
    {
        return tipc_port;
    }

    NP_FAIL;

    return 0;
}

static int USED
set_up (void)
{
    np_mock (cmsg_service_port_get, sm_mock_cmsg_service_port_get);

    return 0;
}

void
test_cmsg_composite_client_child_add (void)
{
    int ret;
    cmsg_client *comp_client = cmsg_composite_client_new (&dummy_service_descriptor);
    cmsg_client *child_1 = cmsg_create_client_tipc_rpc ("test", 1, 1,
                                                        &dummy_service_descriptor);
    cmsg_client *child_2 = cmsg_create_client_tipc_rpc ("test", 2, 2,
                                                        &dummy_service_descriptor);
    cmsg_client *child_3 = cmsg_create_client_tipc_rpc ("test", 3, 3,
                                                        &dummy_service_descriptor);

    ret = cmsg_composite_client_add_child (comp_client, child_1);
    NP_ASSERT_EQUAL (ret, 0);
    NP_ASSERT_EQUAL (g_list_length (comp_client->child_clients), 1);

    ret = cmsg_composite_client_add_child (comp_client, child_2);
    NP_ASSERT_EQUAL (ret, 0);
    NP_ASSERT_EQUAL (g_list_length (comp_client->child_clients), 2);

    ret = cmsg_composite_client_add_child (comp_client, child_3);
    NP_ASSERT_EQUAL (ret, 0);
    NP_ASSERT_EQUAL (g_list_length (comp_client->child_clients), 3);

    cmsg_client_destroy (comp_client);
    cmsg_destroy_client_and_transport (child_1);
    cmsg_destroy_client_and_transport (child_2);
    cmsg_destroy_client_and_transport (child_3);
}

void
test_cmsg_composite_client_child_remove (void)
{
    int ret;
    cmsg_client *comp_client = cmsg_composite_client_new (&dummy_service_descriptor);
    cmsg_client *child_1 = cmsg_create_client_tipc_rpc ("test", 1, 1,
                                                        &dummy_service_descriptor);
    cmsg_client *child_2 = cmsg_create_client_tipc_rpc ("test", 2, 2,
                                                        &dummy_service_descriptor);
    cmsg_client *child_3 = cmsg_create_client_tipc_rpc ("test", 3, 3,
                                                        &dummy_service_descriptor);

    cmsg_composite_client_add_child (comp_client, child_1);
    cmsg_composite_client_add_child (comp_client, child_2);
    cmsg_composite_client_add_child (comp_client, child_3);

    ret = cmsg_composite_client_delete_child (comp_client, child_3);
    NP_ASSERT_EQUAL (ret, 0);
    NP_ASSERT_EQUAL (g_list_length (comp_client->child_clients), 2);

    ret = cmsg_composite_client_delete_child (comp_client, child_2);
    NP_ASSERT_EQUAL (ret, 0);
    NP_ASSERT_EQUAL (g_list_length (comp_client->child_clients), 1);

    ret = cmsg_composite_client_delete_child (comp_client, child_1);
    NP_ASSERT_EQUAL (ret, 0);
    NP_ASSERT_EQUAL (g_list_length (comp_client->child_clients), 0);

    cmsg_client_destroy (comp_client);
    cmsg_destroy_client_and_transport (child_1);
    cmsg_destroy_client_and_transport (child_2);
    cmsg_destroy_client_and_transport (child_3);
}

void
test_cmsg_composite_client_child_remove__already_removed (void)
{
    int ret;
    cmsg_client *comp_client = cmsg_composite_client_new (&dummy_service_descriptor);
    cmsg_client *child_1 = cmsg_create_client_tipc_rpc ("test", 1, 1,
                                                        &dummy_service_descriptor);
    cmsg_client *child_2 = cmsg_create_client_tipc_rpc ("test", 2, 2,
                                                        &dummy_service_descriptor);
    cmsg_client *child_3 = cmsg_create_client_tipc_rpc ("test", 3, 3,
                                                        &dummy_service_descriptor);

    cmsg_composite_client_add_child (comp_client, child_1);
    cmsg_composite_client_add_child (comp_client, child_2);
    cmsg_composite_client_add_child (comp_client, child_3);

    ret = cmsg_composite_client_delete_child (comp_client, child_3);
    NP_ASSERT_EQUAL (ret, 0);
    NP_ASSERT_EQUAL (g_list_length (comp_client->child_clients), 2);

    ret = cmsg_composite_client_delete_child (comp_client, child_3);
    NP_ASSERT_EQUAL (ret, 0);
    NP_ASSERT_EQUAL (g_list_length (comp_client->child_clients), 2);

    cmsg_client_destroy (comp_client);
    cmsg_destroy_client_and_transport (child_1);
    cmsg_destroy_client_and_transport (child_2);
    cmsg_destroy_client_and_transport (child_3);
}

void
test_cmsg_composite_client__sanity_checks (void)
{
    int ret;
    cmsg_client *comp_client = cmsg_composite_client_new (&dummy_service_descriptor);
    cmsg_client *child_1 = cmsg_create_client_tipc_rpc ("test", 1, 1,
                                                        &dummy_service_descriptor);

    ret = cmsg_composite_client_add_child (NULL, child_1);
    NP_ASSERT_EQUAL (ret, -1);

    ret = cmsg_composite_client_add_child (comp_client, NULL);
    NP_ASSERT_EQUAL (ret, -1);

    ret = cmsg_composite_client_add_child (NULL, NULL);
    NP_ASSERT_EQUAL (ret, -1);

    ret = cmsg_composite_client_delete_child (NULL, child_1);
    NP_ASSERT_EQUAL (ret, -1);

    ret = cmsg_composite_client_delete_child (comp_client, NULL);
    NP_ASSERT_EQUAL (ret, -1);

    ret = cmsg_composite_client_delete_child (NULL, NULL);
    NP_ASSERT_EQUAL (ret, -1);

    cmsg_client_destroy (comp_client);
    cmsg_destroy_client_and_transport (child_1);
}

void
test_cmsg_composite_client_add_client__loopback_is_last (void)
{
    cmsg_client *comp_client = cmsg_composite_client_new (&dummy_service_descriptor);
    cmsg_client *child_1 = cmsg_create_client_tipc_rpc ("test", 1, 1,
                                                        &dummy_service_descriptor);
    cmsg_client *child_2 = cmsg_create_client_tipc_rpc ("test", 2, 2,
                                                        &dummy_service_descriptor);
    cmsg_client *child_3 = cmsg_create_client_tipc_rpc ("test", 3, 3,
                                                        &dummy_service_descriptor);
    cmsg_client *child_4 = cmsg_create_client_loopback (&dummy_service);
    cmsg_client *child_5 = cmsg_create_client_loopback (&dummy_service);

    cmsg_composite_client_add_child (comp_client, child_1);
    cmsg_composite_client_add_child (comp_client, child_2);
    cmsg_composite_client_add_child (comp_client, child_4);
    NP_ASSERT_PTR_EQUAL ((g_list_last (comp_client->child_clients))->data, child_4);

    cmsg_composite_client_add_child (comp_client, child_3);
    NP_ASSERT_PTR_EQUAL ((g_list_last (comp_client->child_clients))->data, child_4);

    cmsg_composite_client_add_child (comp_client, child_5);
    NP_ASSERT_PTR_EQUAL ((g_list_last (comp_client->child_clients))->data, child_5);

    cmsg_client_destroy (comp_client);
    cmsg_destroy_client_and_transport (child_1);
    cmsg_destroy_client_and_transport (child_2);
    cmsg_destroy_client_and_transport (child_3);
    cmsg_destroy_client_and_transport (child_4);
    cmsg_destroy_client_and_transport (child_5);
}

void
test_cmsg_composite_client_child_add__unsupported_transport (void)
{
    int ret;
    cmsg_client *comp_client = cmsg_composite_client_new (&dummy_service_descriptor);
    cmsg_client *child_1 =
        cmsg_create_client_tipc_rpc ("test", 1, 1, &dummy_service_descriptor);
    cmsg_client *child_2 =
        cmsg_create_client_tipc_oneway ("test", 2, 2, &dummy_service_descriptor);

    np_syslog_ignore (".*");

    ret = cmsg_composite_client_add_child (comp_client, child_1);
    NP_ASSERT_EQUAL (ret, 0);
    NP_ASSERT_EQUAL (g_list_length (comp_client->child_clients), 1);

    ret = cmsg_composite_client_add_child (comp_client, child_2);
    NP_ASSERT_EQUAL (ret, -1);
    NP_ASSERT_EQUAL (g_list_length (comp_client->child_clients), 1);

    cmsg_client_destroy (comp_client);
    cmsg_destroy_client_and_transport (child_1);
    cmsg_destroy_client_and_transport (child_2);
}

void
test_cmsg_composite_client_lookup_by_tipc_id (void)
{
    cmsg_client *comp_client = cmsg_composite_client_new (&dummy_service_descriptor);
    cmsg_client *child_1 = cmsg_create_client_tipc_rpc ("test", 1, 1,
                                                        &dummy_service_descriptor);
    cmsg_client *child_2 = cmsg_create_client_tipc_rpc ("test", 2, 2,
                                                        &dummy_service_descriptor);
    cmsg_client *child_3 = cmsg_create_client_tipc_rpc ("test", 3, 3,
                                                        &dummy_service_descriptor);
    cmsg_client *lookup_client = NULL;

    cmsg_composite_client_add_child (comp_client, child_1);
    cmsg_composite_client_add_child (comp_client, child_2);

    lookup_client = cmsg_composite_client_lookup_by_tipc_id (comp_client, 1);
    NP_ASSERT_PTR_EQUAL (lookup_client, child_1);

    lookup_client = cmsg_composite_client_lookup_by_tipc_id (comp_client, 3);
    NP_ASSERT_NULL (lookup_client);

    cmsg_client_destroy (comp_client);
    cmsg_destroy_client_and_transport (child_1);
    cmsg_destroy_client_and_transport (child_2);
    cmsg_destroy_client_and_transport (child_3);
}
