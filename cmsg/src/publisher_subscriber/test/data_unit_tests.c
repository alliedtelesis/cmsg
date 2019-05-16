/*
 * Unit tests for the data functionality.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include <np.h>
#include "../data.h"
#include "../remote_sync.h"
#include "transport/cmsg_transport_private.h"

/**
 * This informs the compiler that the function is, in fact, being used even though it
 * doesn't look like it. This is useful for static functions that get found by NovaProva
 * using debug symbols.
 */
#define USED __attribute__ ((used))

extern GHashTable *local_subscriptions_table;

static void
sm_mock_remote_sync_subscription_added (const cmsg_subscription_info *subscriber_info)
{
    /* Do nothing. */
}

static void
sm_mock_remote_sync_subscription_removed (const cmsg_subscription_info *subscriber_info)
{
    /* Do nothing. */
}

static int USED
set_up (void)
{
    np_mock (remote_sync_subscription_added, sm_mock_remote_sync_subscription_added);
    np_mock (remote_sync_subscription_removed, sm_mock_remote_sync_subscription_removed);
    data_init ();

    return 0;
}

static int USED
tear_down (void)
{
    data_deinit ();

    return 0;
}

static cmsg_transport_info *
create_unix_transport_info (void)
{
    cmsg_transport_info *transport_info = NULL;
    cmsg_transport *transport = NULL;
    const ProtobufCServiceDescriptor test_descriptor = {
        PROTOBUF_C__SERVICE_DESCRIPTOR_MAGIC,
        "test",
        "test",
        "test",
        "test",
        0,
        NULL,
        NULL,
    };

    transport = cmsg_create_transport_unix (&test_descriptor, CMSG_TRANSPORT_RPC_UNIX);
    transport_info = cmsg_transport_info_create (transport);
    NP_ASSERT_NOT_NULL (transport_info);
    cmsg_transport_destroy (transport);

    return transport_info;
}

static int
sm_mock_cmsg_service_port_get (const char *name, const char *proto)
{
    return 16000;
}

static cmsg_transport_info *
create_tcp_transport_info (uint32_t _addr)
{
    cmsg_transport_info *transport_info = NULL;
    cmsg_transport *transport = NULL;
    struct in_addr addr;

    addr.s_addr = _addr;

    np_mock (cmsg_service_port_get, sm_mock_cmsg_service_port_get);
    transport = cmsg_create_transport_tcp_ipv4 ("unused", &addr, true);
    np_unmock (cmsg_service_port_get);

    transport_info = cmsg_transport_info_create (transport);
    NP_ASSERT_NOT_NULL (transport_info);
    cmsg_transport_destroy (transport);

    return transport_info;
}

void
test_data_add_subscription_remote (void)
{
    cmsg_subscription_info *sub_info = NULL;

    sub_info = CMSG_MALLOC (sizeof (*sub_info));
    cmsg_subscription_info_init (sub_info);

    CMSG_SET_FIELD_VALUE (sub_info, remote_addr, 1234);

    NP_ASSERT_TRUE (data_add_subscription (sub_info));
}

void
test_data_add_subscription_local (void)
{
    cmsg_subscription_info *sub_info = NULL;

    sub_info = CMSG_MALLOC (sizeof (*sub_info));
    cmsg_subscription_info_init (sub_info);

    CMSG_SET_FIELD_PTR (sub_info, service, CMSG_STRDUP ("test"));
    CMSG_SET_FIELD_PTR (sub_info, method_name, CMSG_STRDUP ("test_method"));
    CMSG_SET_FIELD_PTR (sub_info, transport_info, create_unix_transport_info ());

    NP_ASSERT_FALSE (data_add_subscription (sub_info));
    cmsg_transport_info_free (sub_info->transport_info);
    CMSG_FREE (sub_info->service);
    CMSG_FREE (sub_info->method_name);
    CMSG_FREE (sub_info);
}

void
test_data_get_remote_subscriptions (void)
{
    cmsg_subscription_info *sub_info = NULL;
    GList *remote_subscriptions = NULL;

    sub_info = CMSG_MALLOC (sizeof (*sub_info));
    cmsg_subscription_info_init (sub_info);

    CMSG_SET_FIELD_VALUE (sub_info, remote_addr, 1234);

    data_add_subscription (sub_info);
    remote_subscriptions = data_get_remote_subscriptions ();

    NP_ASSERT_PTR_EQUAL (remote_subscriptions->data, sub_info);
}

void
test_data_check_remote_entries_list_unchanged (void)
{
    cmsg_subscription_info *sub_info = NULL;
    GList *remote_subscriptions = NULL;

    sub_info = CMSG_MALLOC (sizeof (*sub_info));
    cmsg_subscription_info_init (sub_info);

    CMSG_SET_FIELD_VALUE (sub_info, remote_addr, 1234);

    data_add_subscription (sub_info);
    remote_subscriptions = data_get_remote_subscriptions ();
    data_check_remote_entries ();

    NP_ASSERT_PTR_EQUAL (remote_subscriptions, data_get_remote_subscriptions ());
}

void
test_data_remove_remote_subscription (void)
{
    cmsg_subscription_info *sub_info = NULL;
    GList *remote_subscriptions = NULL;

    sub_info = CMSG_MALLOC (sizeof (*sub_info));
    cmsg_subscription_info_init (sub_info);

    CMSG_SET_FIELD_VALUE (sub_info, remote_addr, 1234);
    CMSG_SET_FIELD_PTR (sub_info, service, CMSG_STRDUP ("test"));
    CMSG_SET_FIELD_PTR (sub_info, method_name, CMSG_STRDUP ("test_method"));
    CMSG_SET_FIELD_PTR (sub_info, transport_info, create_tcp_transport_info (2222));
    data_add_subscription (sub_info);
    data_remove_subscription (sub_info);

    remote_subscriptions = data_get_remote_subscriptions ();
    NP_ASSERT_EQUAL (g_list_length (remote_subscriptions), 0);
    NP_ASSERT_PTR_EQUAL (remote_subscriptions, NULL);
}

void
test_data_remove_remote_subscription_unknown (void)
{
    cmsg_subscription_info *sub_info_1 = NULL;
    cmsg_subscription_info *sub_info_2 = NULL;
    GList *remote_subscriptions = NULL;

    sub_info_1 = CMSG_MALLOC (sizeof (*sub_info_1));
    cmsg_subscription_info_init (sub_info_1);
    sub_info_2 = CMSG_MALLOC (sizeof (*sub_info_2));
    cmsg_subscription_info_init (sub_info_2);

    CMSG_SET_FIELD_VALUE (sub_info_1, remote_addr, 1234);
    CMSG_SET_FIELD_PTR (sub_info_1, service, CMSG_STRDUP ("test"));
    CMSG_SET_FIELD_PTR (sub_info_1, method_name, CMSG_STRDUP ("test_method"));
    CMSG_SET_FIELD_PTR (sub_info_1, transport_info, create_tcp_transport_info (2222));
    data_add_subscription (sub_info_1);

    CMSG_SET_FIELD_VALUE (sub_info_2, remote_addr, 2345);
    CMSG_SET_FIELD_PTR (sub_info_2, service, CMSG_STRDUP ("test"));
    CMSG_SET_FIELD_PTR (sub_info_2, method_name, CMSG_STRDUP ("test_method"));
    CMSG_SET_FIELD_PTR (sub_info_2, transport_info, create_tcp_transport_info (3333));
    data_remove_subscription (sub_info_2);
    CMSG_FREE_RECV_MSG (sub_info_2);

    remote_subscriptions = data_get_remote_subscriptions ();
    NP_ASSERT_EQUAL (g_list_length (remote_subscriptions), 1);
    NP_ASSERT_PTR_EQUAL (remote_subscriptions->data, sub_info_1);
}

void
test_data_remove_local_subscription (void)
{
    cmsg_subscription_info *sub_info = NULL;

    sub_info = CMSG_MALLOC (sizeof (*sub_info));
    cmsg_subscription_info_init (sub_info);

    CMSG_SET_FIELD_PTR (sub_info, service, CMSG_STRDUP ("test"));
    CMSG_SET_FIELD_PTR (sub_info, method_name, CMSG_STRDUP ("test_method"));
    CMSG_SET_FIELD_PTR (sub_info, transport_info, create_tcp_transport_info (2222));
    data_add_subscription (sub_info);
    NP_ASSERT_EQUAL (g_hash_table_size (local_subscriptions_table), 1);

    data_remove_subscription (sub_info);
    NP_ASSERT_EQUAL (g_hash_table_size (local_subscriptions_table), 0);
    CMSG_FREE_RECV_MSG (sub_info);
}

void
test_data_remove_local_subscription_unknown (void)
{
    cmsg_subscription_info *sub_info_1 = NULL;
    cmsg_subscription_info *sub_info_2 = NULL;

    sub_info_1 = CMSG_MALLOC (sizeof (*sub_info_1));
    cmsg_subscription_info_init (sub_info_1);
    sub_info_2 = CMSG_MALLOC (sizeof (*sub_info_2));
    cmsg_subscription_info_init (sub_info_2);

    CMSG_SET_FIELD_PTR (sub_info_1, service, CMSG_STRDUP ("test"));
    CMSG_SET_FIELD_PTR (sub_info_1, method_name, CMSG_STRDUP ("test_method"));
    CMSG_SET_FIELD_PTR (sub_info_1, transport_info, create_tcp_transport_info (2222));
    data_add_subscription (sub_info_1);
    CMSG_FREE_RECV_MSG (sub_info_1);
    NP_ASSERT_EQUAL (g_hash_table_size (local_subscriptions_table), 1);

    CMSG_SET_FIELD_PTR (sub_info_2, service, CMSG_STRDUP ("test"));
    CMSG_SET_FIELD_PTR (sub_info_2, method_name, CMSG_STRDUP ("test_method"));
    CMSG_SET_FIELD_PTR (sub_info_2, transport_info, create_tcp_transport_info (3333));
    data_remove_subscription (sub_info_2);
    CMSG_FREE_RECV_MSG (sub_info_2);

    NP_ASSERT_EQUAL (g_hash_table_size (local_subscriptions_table), 1);
}

void
test_data_remove_subscriber (void)
{
    cmsg_subscription_info *sub_info = NULL;
    GList *remote_subscriptions = NULL;
    cmsg_transport_info *transport_info = NULL;

    sub_info = CMSG_MALLOC (sizeof (*sub_info));
    cmsg_subscription_info_init (sub_info);

    CMSG_SET_FIELD_PTR (sub_info, service, CMSG_STRDUP ("test"));
    CMSG_SET_FIELD_PTR (sub_info, method_name, CMSG_STRDUP ("test_method"));
    CMSG_SET_FIELD_PTR (sub_info, transport_info, create_tcp_transport_info (2222));

    /* Add a local subscription */
    data_add_subscription (sub_info);
    NP_ASSERT_EQUAL (g_hash_table_size (local_subscriptions_table), 1);

    CMSG_SET_FIELD_VALUE (sub_info, remote_addr, 1234);
    /* Add a remote subscription */
    data_add_subscription (sub_info);
    remote_subscriptions = data_get_remote_subscriptions ();
    NP_ASSERT_EQUAL (g_list_length (remote_subscriptions), 1);

    transport_info = create_tcp_transport_info (2222);
    data_remove_subscriber (transport_info);
    cmsg_transport_info_free (transport_info);

    NP_ASSERT_EQUAL (g_hash_table_size (local_subscriptions_table), 0);
    remote_subscriptions = data_get_remote_subscriptions ();
    NP_ASSERT_EQUAL (g_list_length (remote_subscriptions), 0);
}

void
test_data_remove_subscriber_unknown (void)
{
    cmsg_subscription_info *sub_info = NULL;
    GList *remote_subscriptions = NULL;
    cmsg_transport_info *transport_info = NULL;

    sub_info = CMSG_MALLOC (sizeof (*sub_info));
    cmsg_subscription_info_init (sub_info);

    CMSG_SET_FIELD_PTR (sub_info, service, CMSG_STRDUP ("test"));
    CMSG_SET_FIELD_PTR (sub_info, method_name, CMSG_STRDUP ("test_method"));
    CMSG_SET_FIELD_PTR (sub_info, transport_info, create_tcp_transport_info (2222));

    /* Add a local subscription */
    data_add_subscription (sub_info);
    NP_ASSERT_EQUAL (g_hash_table_size (local_subscriptions_table), 1);

    CMSG_SET_FIELD_VALUE (sub_info, remote_addr, 1234);
    /* Add a remote subscription */
    data_add_subscription (sub_info);
    remote_subscriptions = data_get_remote_subscriptions ();
    NP_ASSERT_EQUAL (g_list_length (remote_subscriptions), 1);

    transport_info = create_tcp_transport_info (3333);
    data_remove_subscriber (transport_info);
    cmsg_transport_info_free (transport_info);

    NP_ASSERT_EQUAL (g_hash_table_size (local_subscriptions_table), 1);
    remote_subscriptions = data_get_remote_subscriptions ();
    NP_ASSERT_EQUAL (g_list_length (remote_subscriptions), 1);
}

void
test_data_remove_local_subscriptions_for_addr (void)
{
    cmsg_subscription_info *sub_info = NULL;

    sub_info = CMSG_MALLOC (sizeof (*sub_info));
    cmsg_subscription_info_init (sub_info);

    CMSG_SET_FIELD_PTR (sub_info, service, CMSG_STRDUP ("test"));
    CMSG_SET_FIELD_PTR (sub_info, method_name, CMSG_STRDUP ("test_method"));
    CMSG_SET_FIELD_PTR (sub_info, transport_info, create_tcp_transport_info (2222));

    data_add_subscription (sub_info);
    NP_ASSERT_EQUAL (g_hash_table_size (local_subscriptions_table), 1);

    CMSG_FREE (sub_info->service);
    CMSG_SET_FIELD_PTR (sub_info, service, CMSG_STRDUP ("test2"));
    data_add_subscription (sub_info);
    NP_ASSERT_EQUAL (g_hash_table_size (local_subscriptions_table), 2);
    CMSG_FREE_RECV_MSG (sub_info);

    sub_info = CMSG_MALLOC (sizeof (*sub_info));
    cmsg_subscription_info_init (sub_info);

    CMSG_SET_FIELD_PTR (sub_info, service, CMSG_STRDUP ("test3"));
    CMSG_SET_FIELD_PTR (sub_info, method_name, CMSG_STRDUP ("test_method"));
    CMSG_SET_FIELD_PTR (sub_info, transport_info, create_tcp_transport_info (3333));
    data_add_subscription (sub_info);
    NP_ASSERT_EQUAL (g_hash_table_size (local_subscriptions_table), 3);
    CMSG_FREE_RECV_MSG (sub_info);

    data_remove_local_subscriptions_for_addr (2222);
    NP_ASSERT_EQUAL (g_hash_table_size (local_subscriptions_table), 1);
}
