/*
 * Unit tests for the data functionality.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include <np.h>
#include "../data.h"
#include <cmsg/cmsg_private.h>
#include "transport/cmsg_transport_private.h"
#include "../events_api_auto.h"

/**
 * This informs the compiler that the function is, in fact, being used even though it
 * doesn't look like it. This is useful for static functions that get found by NovaProva
 * using debug symbols.
 */
#define USED __attribute__ ((used))

extern GHashTable *hash_table;

static int cmsg_sld_events_api_server_added_called = 0;

static int USED
set_up (void)
{
    cmsg_sld_events_api_server_added_called = 0;

    data_init (false);

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

static int
sm_mock_cmsg_sld_events_api_server_added_ok (cmsg_client *_client,
                                             const cmsg_sld_server_event *_send_msg)
{
    cmsg_sld_events_api_server_added_called++;
    return CMSG_RET_OK;
}

static int
sm_mock_cmsg_sld_events_api_server_added_fail (cmsg_client *_client,
                                               const cmsg_sld_server_event *_send_msg)
{
    cmsg_sld_events_api_server_added_called++;
    return CMSG_RET_ERR;
}

static int
sm_mock_cmsg_sld_events_api_server_added_fail_on_first_call (cmsg_client *_client,
                                                             const cmsg_sld_server_event
                                                             *_send_msg)
{
    cmsg_sld_events_api_server_added_called++;
    if (cmsg_sld_events_api_server_added_called == 1)
    {
        return CMSG_RET_ERR;
    }

    return CMSG_RET_OK;
}

void
test_data_add_server (void)
{
    cmsg_service_info *service_info = NULL;

    service_info = CMSG_MALLOC (sizeof (*service_info));
    cmsg_service_info_init (service_info);

    CMSG_SET_FIELD_PTR (service_info, service, CMSG_STRDUP ("test_service"));
    data_add_server (service_info);

    NP_ASSERT_EQUAL (g_hash_table_size (hash_table), 1);
}

void
test_data_add_server_then_lookup (void)
{
    cmsg_service_info *service_info = NULL;
    service_data_entry *entry = NULL;

    service_info = CMSG_MALLOC (sizeof (*service_info));
    cmsg_service_info_init (service_info);

    CMSG_SET_FIELD_PTR (service_info, service, CMSG_STRDUP ("test_service"));
    data_add_server (service_info);

    entry = get_service_entry_or_create ("test_service", false);
    NP_ASSERT_NOT_NULL (entry);
    NP_ASSERT_EQUAL (g_list_length (entry->servers), 1);
    NP_ASSERT_PTR_EQUAL (entry->servers->data, service_info);
}

void
test_get_service_entry_or_create_non_existent_service (void)
{
    cmsg_service_info *service_info = NULL;
    service_data_entry *entry = NULL;

    service_info = CMSG_MALLOC (sizeof (*service_info));
    cmsg_service_info_init (service_info);

    CMSG_SET_FIELD_PTR (service_info, service, CMSG_STRDUP ("test_service"));
    data_add_server (service_info);

    entry = get_service_entry_or_create ("abcdefg", false);
    NP_ASSERT_NULL (entry);
}

void
test_data_add_server_multiple_different_service (void)
{
    cmsg_service_info *service_info_1 = NULL;
    cmsg_service_info *service_info_2 = NULL;

    service_info_1 = CMSG_MALLOC (sizeof (*service_info_1));
    cmsg_service_info_init (service_info_1);

    service_info_2 = CMSG_MALLOC (sizeof (*service_info_2));
    cmsg_service_info_init (service_info_2);

    CMSG_SET_FIELD_PTR (service_info_1, service, CMSG_STRDUP ("test_service1"));
    data_add_server (service_info_1);

    CMSG_SET_FIELD_PTR (service_info_2, service, CMSG_STRDUP ("test_service2"));
    data_add_server (service_info_2);

    NP_ASSERT_EQUAL (g_hash_table_size (hash_table), 2);
}

void
test_data_add_server_multiple_different_service_then_lookup (void)
{
    cmsg_service_info *service_info_1 = NULL;
    cmsg_service_info *service_info_2 = NULL;
    service_data_entry *entry = NULL;

    service_info_1 = CMSG_MALLOC (sizeof (*service_info_1));
    cmsg_service_info_init (service_info_1);

    service_info_2 = CMSG_MALLOC (sizeof (*service_info_2));
    cmsg_service_info_init (service_info_2);

    CMSG_SET_FIELD_PTR (service_info_1, service, CMSG_STRDUP ("test_service1"));
    data_add_server (service_info_1);

    CMSG_SET_FIELD_PTR (service_info_2, service, CMSG_STRDUP ("test_service2"));
    data_add_server (service_info_2);

    entry = get_service_entry_or_create ("test_service1", false);
    NP_ASSERT_NOT_NULL (entry);
    NP_ASSERT_EQUAL (g_list_length (entry->servers), 1);
    NP_ASSERT_PTR_EQUAL (entry->servers->data, service_info_1);

    entry = get_service_entry_or_create ("test_service2", false);
    NP_ASSERT_NOT_NULL (entry);
    NP_ASSERT_EQUAL (g_list_length (entry->servers), 1);
    NP_ASSERT_PTR_EQUAL (entry->servers->data, service_info_2);
}

void
test_data_add_server_multiple_times (void)
{
    cmsg_service_info *service_info_1 = NULL;
    cmsg_service_info *service_info_2 = NULL;
    service_data_entry *entry = NULL;

    service_info_1 = CMSG_MALLOC (sizeof (*service_info_1));
    cmsg_service_info_init (service_info_1);

    service_info_2 = CMSG_MALLOC (sizeof (*service_info_2));
    cmsg_service_info_init (service_info_2);

    CMSG_SET_FIELD_PTR (service_info_1, service, CMSG_STRDUP ("test_service1"));
    CMSG_SET_FIELD_PTR (service_info_1, server_info, create_tcp_transport_info (123));

    CMSG_SET_FIELD_PTR (service_info_2, service, CMSG_STRDUP ("test_service1"));
    CMSG_SET_FIELD_PTR (service_info_2, server_info, create_tcp_transport_info (123));

    data_add_server (service_info_1);
    data_add_server (service_info_2);

    NP_ASSERT_EQUAL (g_hash_table_size (hash_table), 1);
    entry = get_service_entry_or_create ("test_service1", false);
    NP_ASSERT_NOT_NULL (entry);
    NP_ASSERT_EQUAL (g_list_length (entry->servers), 1);
    NP_ASSERT_PTR_EQUAL (entry->servers->data, service_info_2);
}

void
test_data_add_server_multiple_same_service (void)
{
    cmsg_service_info *service_info_1 = NULL;
    cmsg_service_info *service_info_2 = NULL;

    service_info_1 = CMSG_MALLOC (sizeof (*service_info_1));
    cmsg_service_info_init (service_info_1);

    service_info_2 = CMSG_MALLOC (sizeof (*service_info_2));
    cmsg_service_info_init (service_info_2);

    CMSG_SET_FIELD_PTR (service_info_1, service, CMSG_STRDUP ("test_service1"));
    CMSG_SET_FIELD_PTR (service_info_1, server_info, create_tcp_transport_info (123));

    CMSG_SET_FIELD_PTR (service_info_2, service, CMSG_STRDUP ("test_service1"));
    CMSG_SET_FIELD_PTR (service_info_2, server_info, create_tcp_transport_info (456));

    data_add_server (service_info_1);
    data_add_server (service_info_2);

    NP_ASSERT_EQUAL (g_hash_table_size (hash_table), 1);
}

void
test_data_add_server_multiple_same_service_then_lookup (void)
{
    cmsg_service_info *service_info_1 = NULL;
    cmsg_service_info *service_info_2 = NULL;
    service_data_entry *entry = NULL;

    service_info_1 = CMSG_MALLOC (sizeof (*service_info_1));
    cmsg_service_info_init (service_info_1);

    service_info_2 = CMSG_MALLOC (sizeof (*service_info_2));
    cmsg_service_info_init (service_info_2);

    CMSG_SET_FIELD_PTR (service_info_1, service, CMSG_STRDUP ("test_service1"));
    CMSG_SET_FIELD_PTR (service_info_1, server_info, create_tcp_transport_info (123));

    CMSG_SET_FIELD_PTR (service_info_2, service, CMSG_STRDUP ("test_service1"));
    CMSG_SET_FIELD_PTR (service_info_2, server_info, create_tcp_transport_info (456));

    data_add_server (service_info_1);
    data_add_server (service_info_2);

    entry = get_service_entry_or_create ("test_service1", false);
    NP_ASSERT_NOT_NULL (entry);
    NP_ASSERT_EQUAL (g_list_length (entry->servers), 2);
}

void
test_data_remove_server (void)
{
    cmsg_service_info *service_info_1 = NULL;
    cmsg_service_info *service_info_2 = NULL;

    service_info_1 = CMSG_MALLOC (sizeof (*service_info_1));
    cmsg_service_info_init (service_info_1);

    service_info_2 = CMSG_MALLOC (sizeof (*service_info_2));
    cmsg_service_info_init (service_info_2);

    CMSG_SET_FIELD_PTR (service_info_1, service, CMSG_STRDUP ("test_service"));
    CMSG_SET_FIELD_PTR (service_info_1, server_info, create_unix_transport_info ());

    CMSG_SET_FIELD_PTR (service_info_2, service, CMSG_STRDUP ("test_service"));
    CMSG_SET_FIELD_PTR (service_info_2, server_info, create_unix_transport_info ());

    data_add_server (service_info_1);
    data_remove_server (service_info_2);

    CMSG_FREE_RECV_MSG (service_info_2);

    NP_ASSERT_EQUAL (g_hash_table_size (hash_table), 0);
}

void
test_data_remove_server_correct_service (void)
{
    cmsg_service_info *service_info_1 = NULL;
    cmsg_service_info *service_info_2 = NULL;
    cmsg_service_info *service_info_3 = NULL;
    service_data_entry *entry = NULL;

    service_info_1 = CMSG_MALLOC (sizeof (*service_info_1));
    cmsg_service_info_init (service_info_1);

    service_info_2 = CMSG_MALLOC (sizeof (*service_info_2));
    cmsg_service_info_init (service_info_2);

    service_info_3 = CMSG_MALLOC (sizeof (*service_info_3));
    cmsg_service_info_init (service_info_3);

    CMSG_SET_FIELD_PTR (service_info_1, service, CMSG_STRDUP ("test_service1"));
    CMSG_SET_FIELD_PTR (service_info_1, server_info, create_unix_transport_info ());

    CMSG_SET_FIELD_PTR (service_info_2, service, CMSG_STRDUP ("test_service2"));
    CMSG_SET_FIELD_PTR (service_info_2, server_info, create_unix_transport_info ());

    CMSG_SET_FIELD_PTR (service_info_3, service, CMSG_STRDUP ("test_service1"));
    CMSG_SET_FIELD_PTR (service_info_3, server_info, create_unix_transport_info ());

    data_add_server (service_info_1);
    data_add_server (service_info_2);
    data_remove_server (service_info_3);

    CMSG_FREE_RECV_MSG (service_info_3);

    NP_ASSERT_EQUAL (g_hash_table_size (hash_table), 1);

    entry = get_service_entry_or_create ("test_service1", false);
    NP_ASSERT_NULL (entry);
    entry = get_service_entry_or_create ("test_service2", false);
    NP_ASSERT_NOT_NULL (entry);
}

void
test_data_remove_server_correct_server (void)
{
    cmsg_service_info *service_info_1 = NULL;
    cmsg_service_info *service_info_2 = NULL;
    cmsg_service_info *service_info_3 = NULL;
    service_data_entry *entry = NULL;

    service_info_1 = CMSG_MALLOC (sizeof (*service_info_1));
    cmsg_service_info_init (service_info_1);

    service_info_2 = CMSG_MALLOC (sizeof (*service_info_2));
    cmsg_service_info_init (service_info_2);

    service_info_3 = CMSG_MALLOC (sizeof (*service_info_3));
    cmsg_service_info_init (service_info_3);

    CMSG_SET_FIELD_PTR (service_info_1, service, CMSG_STRDUP ("test_service1"));
    CMSG_SET_FIELD_PTR (service_info_1, server_info, create_unix_transport_info ());

    CMSG_SET_FIELD_PTR (service_info_2, service, CMSG_STRDUP ("test_service1"));
    CMSG_SET_FIELD_PTR (service_info_2, server_info, create_tcp_transport_info (123));

    CMSG_SET_FIELD_PTR (service_info_3, service, CMSG_STRDUP ("test_service1"));
    CMSG_SET_FIELD_PTR (service_info_3, server_info, create_tcp_transport_info (123));

    data_add_server (service_info_1);
    data_add_server (service_info_2);
    data_remove_server (service_info_3);

    CMSG_FREE_RECV_MSG (service_info_3);

    NP_ASSERT_EQUAL (g_hash_table_size (hash_table), 1);
    entry = get_service_entry_or_create ("test_service1", false);
    NP_ASSERT_NOT_NULL (entry);
    NP_ASSERT_EQUAL (g_list_length (entry->servers), 1);
    NP_ASSERT_PTR_EQUAL (entry->servers->data, service_info_1);
}

void
test_data_remove_servers_by_addr (void)
{
    cmsg_service_info *service_info_1 = NULL;
    cmsg_service_info *service_info_2 = NULL;
    cmsg_service_info *service_info_3 = NULL;
    cmsg_service_info *service_info_4 = NULL;
    struct in_addr addr;
    service_data_entry *entry = NULL;

    service_info_1 = CMSG_MALLOC (sizeof (*service_info_1));
    cmsg_service_info_init (service_info_1);

    service_info_2 = CMSG_MALLOC (sizeof (*service_info_2));
    cmsg_service_info_init (service_info_2);

    service_info_3 = CMSG_MALLOC (sizeof (*service_info_3));
    cmsg_service_info_init (service_info_3);

    service_info_4 = CMSG_MALLOC (sizeof (*service_info_4));
    cmsg_service_info_init (service_info_4);

    CMSG_SET_FIELD_PTR (service_info_1, service, CMSG_STRDUP ("test_service1"));
    CMSG_SET_FIELD_PTR (service_info_1, server_info, create_tcp_transport_info (123));

    CMSG_SET_FIELD_PTR (service_info_2, service, CMSG_STRDUP ("test_service1"));
    CMSG_SET_FIELD_PTR (service_info_2, server_info, create_tcp_transport_info (999));

    CMSG_SET_FIELD_PTR (service_info_3, service, CMSG_STRDUP ("test_service2"));
    CMSG_SET_FIELD_PTR (service_info_3, server_info, create_tcp_transport_info (123));

    CMSG_SET_FIELD_PTR (service_info_4, service, CMSG_STRDUP ("test_service2"));
    CMSG_SET_FIELD_PTR (service_info_4, server_info, create_tcp_transport_info (999));

    data_add_server (service_info_1);
    data_add_server (service_info_2);
    data_add_server (service_info_3);
    data_add_server (service_info_4);

    addr.s_addr = 123;
    data_remove_servers_by_addr (addr);

    NP_ASSERT_EQUAL (g_hash_table_size (hash_table), 2);

    entry = get_service_entry_or_create ("test_service1", false);
    NP_ASSERT_NOT_NULL (entry);
    NP_ASSERT_EQUAL (g_list_length (entry->servers), 1);
    NP_ASSERT_PTR_EQUAL (entry->servers->data, service_info_2);

    entry = get_service_entry_or_create ("test_service2", false);
    NP_ASSERT_NOT_NULL (entry);
    NP_ASSERT_EQUAL (g_list_length (entry->servers), 1);
    NP_ASSERT_PTR_EQUAL (entry->servers->data, service_info_4);
}

void
test_data_get_servers_by_addr (void)
{
    cmsg_service_info *service_info_1 = NULL;
    cmsg_service_info *service_info_2 = NULL;
    cmsg_service_info *service_info_3 = NULL;
    cmsg_service_info *service_info_4 = NULL;
    GList *addr_list = NULL;

    service_info_1 = CMSG_MALLOC (sizeof (*service_info_1));
    cmsg_service_info_init (service_info_1);

    service_info_2 = CMSG_MALLOC (sizeof (*service_info_2));
    cmsg_service_info_init (service_info_2);

    service_info_3 = CMSG_MALLOC (sizeof (*service_info_3));
    cmsg_service_info_init (service_info_3);

    service_info_4 = CMSG_MALLOC (sizeof (*service_info_4));
    cmsg_service_info_init (service_info_4);

    CMSG_SET_FIELD_PTR (service_info_1, service, CMSG_STRDUP ("test_service1"));
    CMSG_SET_FIELD_PTR (service_info_1, server_info, create_tcp_transport_info (123));

    CMSG_SET_FIELD_PTR (service_info_2, service, CMSG_STRDUP ("test_service1"));
    CMSG_SET_FIELD_PTR (service_info_2, server_info, create_tcp_transport_info (999));

    CMSG_SET_FIELD_PTR (service_info_3, service, CMSG_STRDUP ("test_service2"));
    CMSG_SET_FIELD_PTR (service_info_3, server_info, create_tcp_transport_info (123));

    CMSG_SET_FIELD_PTR (service_info_4, service, CMSG_STRDUP ("test_service2"));
    CMSG_SET_FIELD_PTR (service_info_4, server_info, create_tcp_transport_info (999));

    data_add_server (service_info_1);
    data_add_server (service_info_2);
    data_add_server (service_info_3);
    data_add_server (service_info_4);

    addr_list = data_get_servers_by_addr (123);
    NP_ASSERT_EQUAL (g_list_length (addr_list), 2);

    GList *list = NULL;

    for (list = g_list_first (addr_list); list; list = g_list_next (list))
    {
        if (list->data == service_info_1)
        {
            /* Change the pointer so that it can't match this entry again */
            service_info_1 = NULL;
        }
        else if (list->data == service_info_3)
        {
            /* Change the pointer so that it can't match this entry again */
            service_info_3 = NULL;
        }
        else
        {
            NP_FAIL;
        }
    }

    g_list_free (addr_list);
}

void
test_data_add_listener (void)
{
    cmsg_sld_listener_info listener_info = CMSG_SLD_LISTENER_INFO_INIT;
    cmsg_transport_info *transport_info = NULL;
    service_data_entry *entry = NULL;
    listener_data *listener_entry = NULL;
    uint32_t addr;

    transport_info = create_tcp_transport_info (999);

    CMSG_SET_FIELD_PTR (&listener_info, service, "test_service");
    CMSG_SET_FIELD_PTR (&listener_info, transport_info, transport_info);
    CMSG_SET_FIELD_VALUE (&listener_info, id, 5);

    data_add_listener (&listener_info);
    cmsg_transport_info_free (transport_info);

    NP_ASSERT_EQUAL (g_hash_table_size (hash_table), 1);
    entry = get_service_entry_or_create ("test_service", false);
    NP_ASSERT_NOT_NULL (entry);
    NP_ASSERT_EQUAL (g_list_length (entry->listeners), 1);

    listener_entry = (listener_data *) entry->listeners->data;
    NP_ASSERT_EQUAL (listener_entry->id, 5);
    NP_ASSERT_EQUAL (listener_entry->client->_transport->type, CMSG_TRANSPORT_ONEWAY_TCP);

    addr = listener_entry->client->_transport->config.socket.sockaddr.in.sin_addr.s_addr;
    NP_ASSERT_EQUAL (addr, 999);
}

void
test_data_remove_listener (void)
{
    cmsg_sld_listener_info listener_info = CMSG_SLD_LISTENER_INFO_INIT;
    cmsg_transport_info *transport_info = NULL;

    transport_info = create_tcp_transport_info (999);

    CMSG_SET_FIELD_PTR (&listener_info, service, "test_service");
    CMSG_SET_FIELD_PTR (&listener_info, transport_info, transport_info);
    CMSG_SET_FIELD_VALUE (&listener_info, id, 5);

    data_add_listener (&listener_info);
    data_remove_listener (&listener_info);
    cmsg_transport_info_free (transport_info);

    NP_ASSERT_EQUAL (g_hash_table_size (hash_table), 0);
}

void
test_data_add_listener_with_existing_server (void)
{
    cmsg_sld_listener_info listener_info = CMSG_SLD_LISTENER_INFO_INIT;
    cmsg_transport_info *transport_info = NULL;
    service_data_entry *entry = NULL;
    cmsg_service_info *service_info_1 = NULL;

    service_info_1 = CMSG_MALLOC (sizeof (*service_info_1));
    cmsg_service_info_init (service_info_1);

    CMSG_SET_FIELD_PTR (service_info_1, service, CMSG_STRDUP ("test_service1"));
    CMSG_SET_FIELD_PTR (service_info_1, server_info, create_tcp_transport_info (123));
    data_add_server (service_info_1);

    transport_info = create_tcp_transport_info (999);

    CMSG_SET_FIELD_PTR (&listener_info, service, "test_service1");
    CMSG_SET_FIELD_PTR (&listener_info, transport_info, transport_info);
    CMSG_SET_FIELD_VALUE (&listener_info, id, 5);

    np_mock (cmsg_sld_events_api_server_added, sm_mock_cmsg_sld_events_api_server_added_ok);

    data_add_listener (&listener_info);
    cmsg_transport_info_free (transport_info);

    NP_ASSERT_EQUAL (g_hash_table_size (hash_table), 1);
    entry = get_service_entry_or_create ("test_service1", false);
    NP_ASSERT_NOT_NULL (entry);
    NP_ASSERT_EQUAL (g_list_length (entry->listeners), 1);
    NP_ASSERT_EQUAL (cmsg_sld_events_api_server_added_called, 1);
}

void
test_data_add_listener_with_existing_server_rpc_failure (void)
{
    cmsg_sld_listener_info listener_info = CMSG_SLD_LISTENER_INFO_INIT;
    cmsg_transport_info *transport_info = NULL;
    service_data_entry *entry = NULL;
    cmsg_service_info *service_info_1 = NULL;

    service_info_1 = CMSG_MALLOC (sizeof (*service_info_1));
    cmsg_service_info_init (service_info_1);

    CMSG_SET_FIELD_PTR (service_info_1, service, CMSG_STRDUP ("test_service1"));
    CMSG_SET_FIELD_PTR (service_info_1, server_info, create_tcp_transport_info (123));
    data_add_server (service_info_1);

    transport_info = create_tcp_transport_info (999);

    CMSG_SET_FIELD_PTR (&listener_info, service, "test_service1");
    CMSG_SET_FIELD_PTR (&listener_info, transport_info, transport_info);
    CMSG_SET_FIELD_VALUE (&listener_info, id, 5);

    np_mock (cmsg_sld_events_api_server_added,
             sm_mock_cmsg_sld_events_api_server_added_fail);

    data_add_listener (&listener_info);
    cmsg_transport_info_free (transport_info);

    NP_ASSERT_EQUAL (g_hash_table_size (hash_table), 1);
    entry = get_service_entry_or_create ("test_service1", false);
    NP_ASSERT_NOT_NULL (entry);
    NP_ASSERT_EQUAL (g_list_length (entry->listeners), 0);
    NP_ASSERT_EQUAL (cmsg_sld_events_api_server_added_called, 1);
}

void
test_data_add_server_with_existing_listeners_rpc_failure (void)
{
    cmsg_sld_listener_info listener_info = CMSG_SLD_LISTENER_INFO_INIT;
    cmsg_transport_info *transport_info = NULL;
    service_data_entry *entry = NULL;
    cmsg_service_info *service_info_1 = NULL;

    service_info_1 = CMSG_MALLOC (sizeof (*service_info_1));
    cmsg_service_info_init (service_info_1);

    CMSG_SET_FIELD_PTR (service_info_1, service, CMSG_STRDUP ("test_service1"));
    CMSG_SET_FIELD_PTR (service_info_1, server_info, create_tcp_transport_info (123));

    transport_info = create_tcp_transport_info (999);
    CMSG_SET_FIELD_PTR (&listener_info, service, "test_service1");
    CMSG_SET_FIELD_PTR (&listener_info, transport_info, transport_info);
    CMSG_SET_FIELD_VALUE (&listener_info, id, 5);
    data_add_listener (&listener_info);
    cmsg_transport_info_free (transport_info);

    transport_info = create_tcp_transport_info (888);
    CMSG_SET_FIELD_PTR (&listener_info, service, "test_service1");
    CMSG_SET_FIELD_PTR (&listener_info, transport_info, transport_info);
    CMSG_SET_FIELD_VALUE (&listener_info, id, 4);
    data_add_listener (&listener_info);
    cmsg_transport_info_free (transport_info);

    entry = get_service_entry_or_create ("test_service1", false);
    NP_ASSERT_NOT_NULL (entry);
    NP_ASSERT_EQUAL (g_list_length (entry->listeners), 2);

    np_mock (cmsg_sld_events_api_server_added,
             sm_mock_cmsg_sld_events_api_server_added_fail_on_first_call);
    data_add_server (service_info_1);

    NP_ASSERT_EQUAL (g_list_length (entry->listeners), 1);
    NP_ASSERT_EQUAL (cmsg_sld_events_api_server_added_called, 2);
}
