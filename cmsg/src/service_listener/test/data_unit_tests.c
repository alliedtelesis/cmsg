/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Unit tests for the data functionality.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include <np.h>
#include <stdint.h>
#include "../data.h"
#include "../process_watch.h"
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

static void
sm_mock_process_watch_add (int pid)
{
}

static void
sm_mock_process_watch_remove (int pid)
{
}

static int USED
set_up (void)
{
    cmsg_sld_events_api_server_added_called = 0;

    np_mock (process_watch_add, sm_mock_process_watch_add);
    np_mock (process_watch_remove, sm_mock_process_watch_remove);

    data_init ();

    return 0;
}

static int USED
tear_down (void)
{
    np_unmock (process_watch_add);
    np_unmock (process_watch_remove);

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
    transport = cmsg_create_transport_tcp_ipv4 ("unused", &addr, NULL, true);
    np_unmock (cmsg_service_port_get);

    transport_info = cmsg_transport_info_create (transport);
    NP_ASSERT_NOT_NULL (transport_info);
    cmsg_transport_destroy (transport);

    return transport_info;
}

static int
sm_mock_cmsg_api_invoke_ok (cmsg_client *client, const cmsg_api_descriptor *cmsg_desc,
                            int method_index, const ProtobufCMessage *send_msg,
                            ProtobufCMessage **recv_msg)
{
    if (cmsg_desc->service_desc == &cmsg_sld_events_descriptor &&
        method_index == cmsg_sld_events_api_server_added_index)
    {
        cmsg_sld_events_api_server_added_called++;
    }

    return CMSG_RET_OK;
}

static int
sm_mock_cmsg_api_invoke_fail (cmsg_client *client, const cmsg_api_descriptor *cmsg_desc,
                              int method_index, const ProtobufCMessage *send_msg,
                              ProtobufCMessage **recv_msg)
{
    if (cmsg_desc->service_desc == &cmsg_sld_events_descriptor &&
        method_index == cmsg_sld_events_api_server_added_index)
    {
        cmsg_sld_events_api_server_added_called++;
    }

    return CMSG_RET_ERR;
}

static int
sm_mock_cmsg_api_invoke_fail_on_first_call (cmsg_client *client,
                                            const cmsg_api_descriptor *cmsg_desc,
                                            int method_index,
                                            const ProtobufCMessage *send_msg,
                                            ProtobufCMessage **recv_msg)
{
    if (cmsg_desc->service_desc == &cmsg_sld_events_descriptor &&
        method_index == cmsg_sld_events_api_server_added_index)
    {
        cmsg_sld_events_api_server_added_called++;
        if (cmsg_sld_events_api_server_added_called == 1)
        {
            return CMSG_RET_ERR;
        }
        else
        {
            return CMSG_RET_OK;
        }
    }

    return cmsg_api_invoke_real (client, cmsg_desc, method_index, send_msg, recv_msg);
}

void
test_data_add_server (void)
{
    cmsg_service_info *service_info = NULL;

    service_info = CMSG_MALLOC (sizeof (*service_info));
    cmsg_service_info_init (service_info);

    CMSG_SET_FIELD_PTR (service_info, service, CMSG_STRDUP ("test_service"));
    data_add_server (service_info, true);

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
    data_add_server (service_info, true);

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
    data_add_server (service_info, true);

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
    data_add_server (service_info_1, true);

    CMSG_SET_FIELD_PTR (service_info_2, service, CMSG_STRDUP ("test_service2"));
    data_add_server (service_info_2, true);

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
    data_add_server (service_info_1, true);

    CMSG_SET_FIELD_PTR (service_info_2, service, CMSG_STRDUP ("test_service2"));
    data_add_server (service_info_2, true);

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

    data_add_server (service_info_1, true);
    data_add_server (service_info_2, true);

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

    data_add_server (service_info_1, true);
    data_add_server (service_info_2, true);

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

    data_add_server (service_info_1, true);
    data_add_server (service_info_2, true);

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

    data_add_server (service_info_1, true);
    data_remove_server (service_info_2, true);

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

    data_add_server (service_info_1, true);
    data_add_server (service_info_2, true);
    data_remove_server (service_info_3, true);

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

    data_add_server (service_info_1, true);
    data_add_server (service_info_2, true);
    data_remove_server (service_info_3, true);

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

    data_add_server (service_info_1, true);
    data_add_server (service_info_2, true);
    data_add_server (service_info_3, true);
    data_add_server (service_info_4, true);

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

    data_add_server (service_info_1, true);
    data_add_server (service_info_2, true);
    data_add_server (service_info_3, true);
    data_add_server (service_info_4, true);

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

/**
 * Build a TCP 'cmsg_transport_info' on the stack using the supplied address and
 * port buffers. This allows tests to claim buffer lengths that a well behaved
 * peer would never send, mimicking a malformed message received from another
 * host.
 *
 * @param transport_info - The 'cmsg_transport_info' to initialise.
 * @param tcp_info - The 'cmsg_tcp_transport_info' to initialise.
 * @param ipv4 - Value for the 'ipv4' field.
 * @param addr - The address buffer.
 * @param addr_len - The length to claim for the address buffer.
 * @param port - The port buffer.
 * @param port_len - The length to claim for the port buffer.
 */
static void
init_tcp_transport_info (cmsg_transport_info *transport_info,
                         cmsg_tcp_transport_info *tcp_info, bool ipv4,
                         uint8_t *addr, uint32_t addr_len, uint8_t *port, uint32_t port_len)
{
    CMSG_SET_FIELD_VALUE (tcp_info, ipv4, ipv4);
    CMSG_SET_FIELD_BYTES (tcp_info, addr, addr, addr_len);
    CMSG_SET_FIELD_BYTES (tcp_info, port, port, port_len);

    CMSG_SET_FIELD_VALUE (transport_info, type, CMSG_TRANSPORT_INFO_TYPE_TCP);
    CMSG_SET_FIELD_ONEOF (transport_info, tcp_info, tcp_info,
                          data, CMSG_TRANSPORT_INFO_DATA_TCP_INFO);
}

/**
 * Baseline for the 'data_validate_transport_info' length tests: a TCP server with
 * a 4 byte address and a 2 byte port is the only shape a well behaved peer
 * sends, so it must be accepted.
 */
void
test_data_validate_transport_info_ipv4 (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr)] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    init_tcp_transport_info (&transport_info, &tcp_info, true,
                             addr, sizeof (addr), port, sizeof (port));

    NP_ASSERT_TRUE (data_validate_transport_info ("test_service", &transport_info));
}

/**
 * An address only one byte longer than an IPv4 address must be rejected too,
 * i.e. the check is an exact length match rather than an upper bound.
 */
void
test_data_validate_transport_info_addr_one_byte_too_long (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr) + 1] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    init_tcp_transport_info (&transport_info, &tcp_info, true,
                             addr, sizeof (addr), port, sizeof (port));

    np_syslog_match ("test_service: Invalid IPv4 address length", 0);

    NP_ASSERT_FALSE (data_validate_transport_info ("test_service", &transport_info));
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);
}

/**
 * An address shorter than an IPv4 address must be rejected. The lookup
 * functions compare a fixed 4 bytes, so a shorter buffer would be over read.
 */
void
test_data_validate_transport_info_addr_too_short (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr) - 1] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    init_tcp_transport_info (&transport_info, &tcp_info, true,
                             addr, sizeof (addr), port, sizeof (port));

    np_syslog_match ("test_service: Invalid IPv4 address length", 0);

    NP_ASSERT_FALSE (data_validate_transport_info ("test_service", &transport_info));
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);
}

/**
 * A zero length address must be rejected.
 */
void
test_data_validate_transport_info_addr_len_zero (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t port[sizeof (in_port_t)] = { };

    init_tcp_transport_info (&transport_info, &tcp_info, true,
                             NULL, 0, port, sizeof (port));

    np_syslog_match ("test_service: Invalid IPv4 address length", 0);

    NP_ASSERT_FALSE (data_validate_transport_info ("test_service", &transport_info));
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);
}

/**
 * A port longer than a TCP port must be rejected. 'data_debug_tcp_server_dump'
 * copies a fixed 'uint16_t' out of the buffer, so a longer buffer means the
 * stored length no longer describes what is read back.
 */
void
test_data_validate_transport_info_port_too_long (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr)] = { };
    uint8_t port[sizeof (in_port_t) * 2] = { };

    init_tcp_transport_info (&transport_info, &tcp_info, true,
                             addr, sizeof (addr), port, sizeof (port));

    np_syslog_match ("test_service: Invalid TCP port length", 0);

    NP_ASSERT_FALSE (data_validate_transport_info ("test_service", &transport_info));
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);
}

/**
 * A port claiming a length far larger than anything that could have been
 * allocated for it must be rejected.
 */
void
test_data_validate_transport_info_port_len_huge (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr)] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    init_tcp_transport_info (&transport_info, &tcp_info, true,
                             addr, sizeof (addr), port, UINT32_MAX);

    np_syslog_match ("test_service: Invalid TCP port length", 0);

    NP_ASSERT_FALSE (data_validate_transport_info ("test_service", &transport_info));
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);
}

/**
 * A port shorter than a TCP port must be rejected.
 */
void
test_data_validate_transport_info_port_too_short (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr)] = { };
    uint8_t port[sizeof (in_port_t) - 1] = { };

    init_tcp_transport_info (&transport_info, &tcp_info, true,
                             addr, sizeof (addr), port, sizeof (port));

    np_syslog_match ("test_service: Invalid TCP port length", 0);

    NP_ASSERT_FALSE (data_validate_transport_info ("test_service", &transport_info));
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);
}

/**
 * A zero length port must be rejected.
 */
void
test_data_validate_transport_info_port_len_zero (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr)] = { };

    init_tcp_transport_info (&transport_info, &tcp_info, true,
                             addr, sizeof (addr), NULL, 0);

    np_syslog_match ("test_service: Invalid TCP port length", 0);

    NP_ASSERT_FALSE (data_validate_transport_info ("test_service", &transport_info));
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);
}

/**
 * An IPv6 server carries a 16 byte address, which is what 'inet_ntop' and
 * 'cmsg_transport_info_to_transport' read back out of the buffer, so it must be
 * accepted.
 */
void
test_data_validate_transport_info_ipv6 (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in6_addr)] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    init_tcp_transport_info (&transport_info, &tcp_info, false,
                             addr, sizeof (addr), port, sizeof (port));

    NP_ASSERT_TRUE (data_validate_transport_info ("test_service", &transport_info));
}

/**
 * An IPv6 server sending an IPv4 sized address must be rejected. The length is
 * checked against the family the peer claimed, not against either family.
 */
void
test_data_validate_transport_info_ipv6_addr_too_short (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr)] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    init_tcp_transport_info (&transport_info, &tcp_info, false,
                             addr, sizeof (addr), port, sizeof (port));

    np_syslog_match ("test_service: Invalid IPv6 address length", 0);

    NP_ASSERT_FALSE (data_validate_transport_info ("test_service", &transport_info));
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);
}

/**
 * An IPv4 server sending an IPv6 sized address must be rejected, i.e. the
 * IPv4 length check is not relaxed to allow the larger of the two families.
 */
void
test_data_validate_transport_info_ipv4_addr_ipv6_length (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in6_addr)] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    init_tcp_transport_info (&transport_info, &tcp_info, true,
                             addr, sizeof (addr), port, sizeof (port));

    np_syslog_match ("test_service: Invalid IPv4 address length", 0);

    NP_ASSERT_FALSE (data_validate_transport_info ("test_service", &transport_info));
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);
}

/**
 * A message with no transport information at all must be rejected rather than
 * dereferenced. Every field of the message is optional, so a peer can simply
 * leave it out.
 */
void
test_data_validate_transport_info_null (void)
{
    np_syslog_match ("test_service: Missing transport info", 0);

    NP_ASSERT_FALSE (data_validate_transport_info ("test_service", NULL));
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);
}

/**
 * A message with no service key must also be rejected.
 */
void
test_data_validate_transport_info_null_key (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr)] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    init_tcp_transport_info (&transport_info, &tcp_info, true,
                             addr, sizeof (addr), port, sizeof (port));

    np_syslog_match ("Missing service key", 0);

    NP_ASSERT_FALSE (data_validate_transport_info (NULL, &transport_info));
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);
}

/**
 * A peer claiming a TCP transport but leaving the 'tcp_info' side of the oneof
 * unset must be rejected rather than dereferenced.
 */
void
test_data_validate_transport_info_tcp_info_missing (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;

    CMSG_SET_FIELD_VALUE (&transport_info, type, CMSG_TRANSPORT_INFO_TYPE_TCP);

    np_syslog_match ("test_service: Missing TCP info", 0);

    NP_ASSERT_FALSE (data_validate_transport_info ("test_service", &transport_info));
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);
}

/**
 * A peer claiming a UNIX transport but leaving the 'unix_info' side of the
 * oneof unset must be rejected. 'data_debug_unix_server_dump' and
 * 'cmsg_transport_info_compare' both dereference the path unconditionally.
 */
void
test_data_validate_transport_info_unix_info_missing (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;

    CMSG_SET_FIELD_VALUE (&transport_info, type, CMSG_TRANSPORT_INFO_TYPE_UNIX);

    np_syslog_match ("test_service: Missing UNIX path", 0);

    NP_ASSERT_FALSE (data_validate_transport_info ("test_service", &transport_info));
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);
}

/**
 * A UNIX transport with the 'unix_info' set but no path in it must be rejected
 * for the same reason.
 */
void
test_data_validate_transport_info_unix_path_missing (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_unix_transport_info unix_info = CMSG_UNIX_TRANSPORT_INFO_INIT;

    CMSG_SET_FIELD_VALUE (&transport_info, type, CMSG_TRANSPORT_INFO_TYPE_UNIX);
    CMSG_SET_FIELD_ONEOF (&transport_info, unix_info, &unix_info,
                          data, CMSG_TRANSPORT_INFO_DATA_UNIX_INFO);

    np_syslog_match ("test_service: Missing UNIX path", 0);

    NP_ASSERT_FALSE (data_validate_transport_info ("test_service", &transport_info));
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);
}

/**
 * A transport type this daemon cannot use is rejected, and rejected quietly.
 * The type is chosen by the peer, so logging it would be a log flood waiting to
 * happen; NovaProva fails this test if anything is written to the log.
 */
void
test_data_validate_transport_info_unsupported_type (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;

    CMSG_SET_FIELD_VALUE (&transport_info, type, CMSG_TRANSPORT_INFO_TYPE_NOT_SET);

    NP_ASSERT_FALSE (data_validate_transport_info ("test_service", &transport_info));
}

/**
 * The length checks only apply to TCP servers. A UNIX server has no address or
 * port buffers to validate, so it must be accepted.
 */
void
test_data_validate_transport_info_unix (void)
{
    cmsg_transport_info *transport_info = NULL;

    transport_info = create_unix_transport_info ();

    NP_ASSERT_TRUE (data_validate_transport_info ("test_service", transport_info));

    cmsg_transport_info_free (transport_info);
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
    data_add_server (service_info_1, true);

    transport_info = create_tcp_transport_info (999);

    CMSG_SET_FIELD_PTR (&listener_info, service, "test_service1");
    CMSG_SET_FIELD_PTR (&listener_info, transport_info, transport_info);
    CMSG_SET_FIELD_VALUE (&listener_info, id, 5);

    np_mock (cmsg_api_invoke, sm_mock_cmsg_api_invoke_ok);

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
    data_add_server (service_info_1, true);

    transport_info = create_tcp_transport_info (999);

    CMSG_SET_FIELD_PTR (&listener_info, service, "test_service1");
    CMSG_SET_FIELD_PTR (&listener_info, transport_info, transport_info);
    CMSG_SET_FIELD_VALUE (&listener_info, id, 5);

    np_mock (cmsg_api_invoke, sm_mock_cmsg_api_invoke_fail);

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

    np_mock (cmsg_api_invoke, sm_mock_cmsg_api_invoke_fail_on_first_call);
    data_add_server (service_info_1, true);

    NP_ASSERT_EQUAL (g_list_length (entry->listeners), 1);
    NP_ASSERT_EQUAL (cmsg_sld_events_api_server_added_called, 2);
}

void
test_data_remove_by_pid (void)
{
    cmsg_service_info *service_info_1 = NULL;
    cmsg_service_info *service_info_2 = NULL;
    service_data_entry *entry = NULL;
    cmsg_sld_listener_info listener_info = CMSG_SLD_LISTENER_INFO_INIT;
    cmsg_transport_info *transport_info = NULL;

    np_mock (cmsg_api_invoke, sm_mock_cmsg_api_invoke_ok);

    /* Add two listeners for different services,
     * one with PID 10, one with PID 11. */
    transport_info = create_tcp_transport_info (999);

    CMSG_SET_FIELD_PTR (&listener_info, service, "test_service1");
    CMSG_SET_FIELD_PTR (&listener_info, transport_info, transport_info);
    CMSG_SET_FIELD_VALUE (&listener_info, id, 5);
    CMSG_SET_FIELD_VALUE (&listener_info, pid, 10);

    data_add_listener (&listener_info);

    CMSG_SET_FIELD_PTR (&listener_info, service, "test_service2");
    CMSG_SET_FIELD_VALUE (&listener_info, pid, 11);
    data_add_listener (&listener_info);

    cmsg_transport_info_free (transport_info);

    /* Add two servers for the different services, both with PID 10. */
    service_info_1 = CMSG_MALLOC (sizeof (*service_info_1));
    cmsg_service_info_init (service_info_1);

    service_info_2 = CMSG_MALLOC (sizeof (*service_info_2));
    cmsg_service_info_init (service_info_2);

    CMSG_SET_FIELD_PTR (service_info_1, service, CMSG_STRDUP ("test_service1"));
    CMSG_SET_FIELD_VALUE (service_info_1, pid, 10);
    data_add_server (service_info_1, true);

    CMSG_SET_FIELD_PTR (service_info_2, service, CMSG_STRDUP ("test_service2"));
    CMSG_SET_FIELD_VALUE (service_info_2, pid, 10);
    data_add_server (service_info_2, true);

    NP_ASSERT_EQUAL (g_hash_table_size (hash_table), 2);

    /* Remove by PID 10, the only remaining thing should be the listener
     * with PID 11. */
    data_remove_by_pid (10);
    NP_ASSERT_EQUAL (g_hash_table_size (hash_table), 1);

    NP_ASSERT_NULL (get_service_entry_or_create ("test_service1", false));
    entry = get_service_entry_or_create ("test_service2", false);
    NP_ASSERT_NOT_NULL (entry);
    NP_ASSERT_EQUAL (g_list_length (entry->servers), 0);
    NP_ASSERT_EQUAL (g_list_length (entry->listeners), 1);

    /* Remove by PID 11, the hash table should now be empty. */
    data_remove_by_pid (11);
    NP_ASSERT_EQUAL (g_hash_table_size (hash_table), 0);
}
