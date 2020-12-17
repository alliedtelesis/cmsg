/*
 * Unit tests for cmsg_composite_client.c
 *
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */

#include <np.h>
#include <cmsg_client.h>
#include <cmsg_composite_client.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

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

const ProtobufCServiceDescriptor dummy_service_descriptor = {
    .name = "dummy",
};

ProtobufCService dummy_service = {
    .descriptor = &dummy_service_descriptor,
};

#define LOOPBACK_ADDR_PREFIX    0x7f000000  /* 127.0.0.0 */
static const uint16_t tcp_port = 18888;

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
    if ((strcmp (name, "test") == 0) && (strcmp (proto, "tcp") == 0))
    {
        return tcp_port;
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
    struct in_addr addr1 = inet_makeaddr (LOOPBACK_ADDR_PREFIX, 1);
    struct in_addr addr2 = inet_makeaddr (LOOPBACK_ADDR_PREFIX, 2);
    struct in_addr addr3 = inet_makeaddr (LOOPBACK_ADDR_PREFIX, 3);

    cmsg_client *comp_client = cmsg_composite_client_new (&dummy_service_descriptor);
    cmsg_client *child_1 = cmsg_create_client_tcp_ipv4_rpc ("test", &addr1, NULL,
                                                            &dummy_service_descriptor);
    cmsg_client *child_2 = cmsg_create_client_tcp_ipv4_rpc ("test", &addr2, NULL,
                                                            &dummy_service_descriptor);
    cmsg_client *child_3 = cmsg_create_client_tcp_ipv4_rpc ("test", &addr3, NULL,
                                                            &dummy_service_descriptor);

    ret = cmsg_composite_client_add_child (comp_client, child_1);
    NP_ASSERT_EQUAL (ret, 0);
    NP_ASSERT_EQUAL (cmsg_composite_client_num_children (comp_client), 1);

    ret = cmsg_composite_client_add_child (comp_client, child_2);
    NP_ASSERT_EQUAL (ret, 0);
    NP_ASSERT_EQUAL (cmsg_composite_client_num_children (comp_client), 2);

    ret = cmsg_composite_client_add_child (comp_client, child_3);
    NP_ASSERT_EQUAL (ret, 0);
    NP_ASSERT_EQUAL (cmsg_composite_client_num_children (comp_client), 3);

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
    struct in_addr addr1 = inet_makeaddr (LOOPBACK_ADDR_PREFIX, 1);
    struct in_addr addr2 = inet_makeaddr (LOOPBACK_ADDR_PREFIX, 2);
    struct in_addr addr3 = inet_makeaddr (LOOPBACK_ADDR_PREFIX, 3);

    cmsg_client *child_1 = cmsg_create_client_tcp_ipv4_rpc ("test", &addr1, NULL,
                                                            &dummy_service_descriptor);
    cmsg_client *child_2 = cmsg_create_client_tcp_ipv4_rpc ("test", &addr2, NULL,
                                                            &dummy_service_descriptor);
    cmsg_client *child_3 = cmsg_create_client_tcp_ipv4_rpc ("test", &addr3, NULL,
                                                            &dummy_service_descriptor);

    cmsg_composite_client_add_child (comp_client, child_1);
    cmsg_composite_client_add_child (comp_client, child_2);
    cmsg_composite_client_add_child (comp_client, child_3);

    ret = cmsg_composite_client_delete_child (comp_client, child_3);
    NP_ASSERT_EQUAL (ret, 0);
    NP_ASSERT_EQUAL (cmsg_composite_client_num_children (comp_client), 2);

    ret = cmsg_composite_client_delete_child (comp_client, child_2);
    NP_ASSERT_EQUAL (ret, 0);
    NP_ASSERT_EQUAL (cmsg_composite_client_num_children (comp_client), 1);

    ret = cmsg_composite_client_delete_child (comp_client, child_1);
    NP_ASSERT_EQUAL (ret, 0);
    NP_ASSERT_EQUAL (cmsg_composite_client_num_children (comp_client), 0);

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
    struct in_addr addr1 = inet_makeaddr (LOOPBACK_ADDR_PREFIX, 1);
    struct in_addr addr2 = inet_makeaddr (LOOPBACK_ADDR_PREFIX, 2);
    struct in_addr addr3 = inet_makeaddr (LOOPBACK_ADDR_PREFIX, 3);

    cmsg_client *child_1 = cmsg_create_client_tcp_ipv4_rpc ("test", &addr1, NULL,
                                                            &dummy_service_descriptor);
    cmsg_client *child_2 = cmsg_create_client_tcp_ipv4_rpc ("test", &addr2, NULL,
                                                            &dummy_service_descriptor);
    cmsg_client *child_3 = cmsg_create_client_tcp_ipv4_rpc ("test", &addr3, NULL,
                                                            &dummy_service_descriptor);

    cmsg_composite_client_add_child (comp_client, child_1);
    cmsg_composite_client_add_child (comp_client, child_2);
    cmsg_composite_client_add_child (comp_client, child_3);

    ret = cmsg_composite_client_delete_child (comp_client, child_3);
    NP_ASSERT_EQUAL (ret, 0);
    NP_ASSERT_EQUAL (cmsg_composite_client_num_children (comp_client), 2);

    ret = cmsg_composite_client_delete_child (comp_client, child_3);
    NP_ASSERT_EQUAL (ret, 0);
    NP_ASSERT_EQUAL (cmsg_composite_client_num_children (comp_client), 2);

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
    struct in_addr addr1 = inet_makeaddr (LOOPBACK_ADDR_PREFIX, 1);

    cmsg_client *child_1 = cmsg_create_client_tcp_ipv4_rpc ("test", &addr1, NULL,
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
    struct in_addr addr1 = inet_makeaddr (LOOPBACK_ADDR_PREFIX, 1);
    struct in_addr addr2 = inet_makeaddr (LOOPBACK_ADDR_PREFIX, 2);
    struct in_addr addr3 = inet_makeaddr (LOOPBACK_ADDR_PREFIX, 3);

    cmsg_client *child_1 = cmsg_create_client_tcp_ipv4_rpc ("test", &addr1, NULL,
                                                            &dummy_service_descriptor);
    cmsg_client *child_2 = cmsg_create_client_tcp_ipv4_rpc ("test", &addr2, NULL,
                                                            &dummy_service_descriptor);
    cmsg_client *child_3 = cmsg_create_client_tcp_ipv4_rpc ("test", &addr3, NULL,
                                                            &dummy_service_descriptor);
    cmsg_client *child_4 = cmsg_create_client_loopback (&dummy_service);
    cmsg_client *child_5 = cmsg_create_client_loopback (&dummy_service);
    GList *child_clients = NULL;
    cmsg_client *list_child = NULL;

    cmsg_composite_client_add_child (comp_client, child_1);
    cmsg_composite_client_add_child (comp_client, child_2);
    cmsg_composite_client_add_child (comp_client, child_4);
    child_clients = cmsg_composite_client_get_children (comp_client);
    list_child = (cmsg_client *) (g_list_last (child_clients))->data;
    NP_ASSERT_PTR_EQUAL (list_child, child_4);

    cmsg_composite_client_add_child (comp_client, child_3);
    child_clients = cmsg_composite_client_get_children (comp_client);
    list_child = (cmsg_client *) (g_list_last (child_clients))->data;
    NP_ASSERT_PTR_EQUAL (list_child, child_4);

    cmsg_composite_client_add_child (comp_client, child_5);
    child_clients = cmsg_composite_client_get_children (comp_client);
    list_child = (cmsg_client *) (g_list_last (child_clients))->data;
    NP_ASSERT_PTR_EQUAL (list_child, child_5);

    cmsg_client_destroy (comp_client);
    cmsg_destroy_client_and_transport (child_1);
    cmsg_destroy_client_and_transport (child_2);
    cmsg_destroy_client_and_transport (child_3);
    cmsg_destroy_client_and_transport (child_4);
    cmsg_destroy_client_and_transport (child_5);
}

void
test_wrong_client_type (void)
{
    cmsg_client *comp_client = cmsg_composite_client_new (&dummy_service_descriptor);
    struct in_addr addr1 = inet_makeaddr (LOOPBACK_ADDR_PREFIX, 1);
    struct in_addr addr2 = inet_makeaddr (LOOPBACK_ADDR_PREFIX, 2);

    cmsg_client *std_client = cmsg_create_client_tcp_ipv4_rpc ("test", &addr1, NULL,
                                                               &dummy_service_descriptor);
    cmsg_client *child_client = cmsg_create_client_tcp_ipv4_rpc ("test", &addr2, NULL,
                                                                 &dummy_service_descriptor);
    int ret;

    np_syslog_ignore (".*");

    ret = cmsg_composite_client_add_child (comp_client, child_client);
    NP_ASSERT_EQUAL (ret, 0);

    ret = cmsg_composite_client_add_child (std_client, child_client);
    NP_ASSERT_EQUAL (ret, -1);

    ret = cmsg_composite_client_delete_child (comp_client, child_client);
    NP_ASSERT_EQUAL (ret, 0);

    ret = cmsg_composite_client_delete_child (std_client, child_client);
    NP_ASSERT_EQUAL (ret, -1);

    cmsg_client_destroy (comp_client);
    cmsg_destroy_client_and_transport (std_client);
    cmsg_destroy_client_and_transport (child_client);
}
