/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Unit tests for the remote sync functionality.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include <np.h>
#include <stdint.h>
#include "../remote_sync.h"
#include "../remote_sync_impl_auto.h"
#include "../remote_sync_api_auto.h"
#include "../data.h"
#include <cmsg/cmsg_glib_helpers.h>
#include "cmsg_server_private.h"
#include "service_listener/cmsg_sl_api_private.h"
#include "transport/cmsg_transport_private.h"

/**
 * This informs the compiler that the function is, in fact, being used even though it
 * doesn't look like it. This is useful for static functions that get found by NovaProva
 * using debug symbols.
 */
#define USED __attribute__ ((used))

struct remote_sync_data
{
    cmsg_client *client;
    guint timer_id;
};

extern cmsg_server *remote_sync_server;
extern GList *remote_sync_client_list;

static cmsg_server *server_test_ptr = (cmsg_server *) 0x15876;

static const ProtobufCServiceDescriptor test_descriptor = {
    PROTOBUF_C__SERVICE_DESCRIPTOR_MAGIC,
    "test",
    "test",
    "test",
    "test",
    0,
    NULL,
    NULL,
};

static ProtobufCService test_service = {
    .descriptor = &test_descriptor,
};

static GList *mock_remote_subscriptions_list = NULL;
static GList *remote_subscriptions_seen = NULL;

static int cmsg_psd_remote_sync_api_add_subscription_called = 0;
static int cmsg_psd_remote_sync_api_remove_subscription_called = 0;

static int USED
set_up (void)
{
    cmsg_psd_remote_sync_api_add_subscription_called = 0;
    cmsg_psd_remote_sync_api_remove_subscription_called = 0;
    mock_remote_subscriptions_list = NULL;
    remote_subscriptions_seen = NULL;

    return 0;
}

static cmsg_server *
sm_mock_cmsg_glib_tcp_server_init_oneway_ptr_return (const char *service_name,
                                                     struct in_addr *addr,
                                                     ProtobufCService *service)
{
    return server_test_ptr;
}

static cmsg_server *
sm_mock_cmsg_glib_tcp_server_init_oneway_fail (const char *service_name,
                                               struct in_addr *addr,
                                               ProtobufCService *service)
{
    NP_FAIL;
    return NULL;
}

static void
sm_mock_remote_sync_sl_init (void)
{
    /* Do nothing. */
}

static void
sm_mock_cmsg_service_listener_remove_server (cmsg_server *server)
{
    /* Do nothing. */
}

static void
sm_mock_remote_sync_bulk_sync_subscriptions (struct remote_sync_data *remote)
{
    /* Do nothing. */
}

static GList *
sm_mock_data_get_remote_subscriptions (void)
{
    return mock_remote_subscriptions_list;
}

static int
sm_mock_cmsg_api_invoke (cmsg_client *client, const cmsg_api_descriptor *cmsg_desc,
                         int method_index, const ProtobufCMessage *send_msg,
                         ProtobufCMessage **recv_msg)
{
    if (cmsg_desc->service_desc == &cmsg_psd_remote_sync_descriptor)
    {
        if (method_index == cmsg_psd_remote_sync_api_add_subscription_index)
        {
            cmsg_psd_remote_sync_api_add_subscription_called++;
            return CMSG_RET_OK;
        }
        else if (method_index == cmsg_psd_remote_sync_api_remove_subscription_index)
        {
            cmsg_psd_remote_sync_api_remove_subscription_called++;
            return CMSG_RET_OK;
        }
        else if (method_index == cmsg_psd_remote_sync_api_bulk_sync_index)
        {
            int i;
            const cmsg_psd_bulk_sync_data *_send_msg =
                (const cmsg_psd_bulk_sync_data *) send_msg;
            cmsg_subscription_info *info;

            CMSG_REPEATED_FOREACH (_send_msg, data, info, i)
            {
                remote_subscriptions_seen = g_list_append (remote_subscriptions_seen, info);
            }
            return CMSG_RET_OK;
        }
    }

    return cmsg_api_invoke_real (client, cmsg_desc, method_index, send_msg, recv_msg);
}

void
test_remote_sync_address_set (void)
{
    struct in_addr addr = { };
    const uint32_t test_addr = 1234;

    addr.s_addr = test_addr;

    np_mock (cmsg_glib_tcp_server_init_oneway,
             sm_mock_cmsg_glib_tcp_server_init_oneway_ptr_return);
    np_mock_by_name ("remote_sync_sl_init", sm_mock_remote_sync_sl_init);

    remote_sync_address_set (addr);

    NP_ASSERT_PTR_EQUAL (remote_sync_server, server_test_ptr);
    NP_ASSERT_EQUAL (test_addr, remote_sync_get_local_ip ());
}

void
test_remote_sync_address_set_called_twice (void)
{
    struct in_addr addr = { };
    const uint32_t test_addr = 1234;

    addr.s_addr = test_addr;

    np_mock (cmsg_glib_tcp_server_init_oneway,
             sm_mock_cmsg_glib_tcp_server_init_oneway_ptr_return);
    np_mock_by_name ("remote_sync_sl_init", sm_mock_remote_sync_sl_init);

    remote_sync_address_set (addr);

    np_mock (cmsg_glib_tcp_server_init_oneway,
             sm_mock_cmsg_glib_tcp_server_init_oneway_fail);

    NP_ASSERT_PTR_EQUAL (remote_sync_server, server_test_ptr);
    NP_ASSERT_EQUAL (test_addr, remote_sync_get_local_ip ());
}

static int
sm_mock_cmsg_service_port_get (const char *name, const char *proto)
{
    return 16000;
}

static cmsg_transport *
create_tcp_transport (uint32_t _addr)
{
    cmsg_transport *transport = NULL;
    struct in_addr addr;

    addr.s_addr = _addr;

    np_mock (cmsg_service_port_get, sm_mock_cmsg_service_port_get);
    transport = cmsg_create_transport_tcp_ipv4 ("unused", &addr, NULL, true);
    np_unmock (cmsg_service_port_get);

    return transport;
}

void
test_remote_sync_sl_event_handler (void)
{
    cmsg_transport *test_transport_1 = NULL;
    cmsg_transport *test_transport_2 = NULL;
    cmsg_transport *server_transport = NULL;
    struct remote_sync_data *remote = NULL;

    server_transport = create_tcp_transport (1234);
    remote_sync_server = cmsg_server_create (server_transport, &test_service);

    test_transport_1 = create_tcp_transport (1111);
    test_transport_2 = create_tcp_transport (2222);

    np_mock_by_name ("remote_sync_bulk_sync_subscriptions",
                     sm_mock_remote_sync_bulk_sync_subscriptions);

    remote_sync_sl_event_handler (test_transport_1, true, NULL);
    NP_ASSERT_EQUAL (g_list_length (remote_sync_client_list), 1);

    remote_sync_sl_event_handler (test_transport_2, true, NULL);
    NP_ASSERT_EQUAL (g_list_length (remote_sync_client_list), 2);

    remote_sync_sl_event_handler (test_transport_2, false, NULL);
    NP_ASSERT_EQUAL (g_list_length (remote_sync_client_list), 1);
    remote = (struct remote_sync_data *) remote_sync_client_list->data;
    NP_ASSERT_TRUE (cmsg_transport_compare (remote->client->_transport, test_transport_1));

    remote_sync_sl_event_handler (test_transport_1, false, NULL);
    NP_ASSERT_EQUAL (g_list_length (remote_sync_client_list), 0);

    np_mock (cmsg_service_listener_remove_server,
             sm_mock_cmsg_service_listener_remove_server);
    cmsg_destroy_server_and_transport (remote_sync_server);
    remote_sync_server = NULL;
    cmsg_transport_destroy (test_transport_1);
    cmsg_transport_destroy (test_transport_2);
}

void
test_remote_sync_sl_event_handler_unknown_transport (void)
{
    cmsg_transport *test_transport_1 = NULL;
    cmsg_transport *test_transport_2 = NULL;
    cmsg_transport *server_transport = NULL;

    server_transport = create_tcp_transport (1234);
    remote_sync_server = cmsg_server_create (server_transport, &test_service);

    test_transport_1 = create_tcp_transport (1111);
    test_transport_2 = create_tcp_transport (2222);

    np_mock_by_name ("remote_sync_bulk_sync_subscriptions",
                     sm_mock_remote_sync_bulk_sync_subscriptions);

    remote_sync_sl_event_handler (test_transport_1, true, NULL);
    NP_ASSERT_EQUAL (g_list_length (remote_sync_client_list), 1);

    remote_sync_sl_event_handler (test_transport_2, false, NULL);
    NP_ASSERT_EQUAL (g_list_length (remote_sync_client_list), 1);

    remote_sync_sl_event_handler (test_transport_1, false, NULL);
    NP_ASSERT_EQUAL (g_list_length (remote_sync_client_list), 0);

    np_mock (cmsg_service_listener_remove_server,
             sm_mock_cmsg_service_listener_remove_server);
    cmsg_destroy_server_and_transport (remote_sync_server);
    remote_sync_server = NULL;
    cmsg_transport_destroy (test_transport_1);
    cmsg_transport_destroy (test_transport_2);
}

void
test_remote_sync_sl_event_handler_local_server (void)
{
    cmsg_transport *server_transport = NULL;

    server_transport = create_tcp_transport (1234);
    remote_sync_server = cmsg_server_create (server_transport, &test_service);

    remote_sync_sl_event_handler (server_transport, true, NULL);
    NP_ASSERT_EQUAL (g_list_length (remote_sync_client_list), 0);

    np_mock (cmsg_service_listener_remove_server,
             sm_mock_cmsg_service_listener_remove_server);
    cmsg_destroy_server_and_transport (remote_sync_server);
    remote_sync_server = NULL;
}

void
test_remote_sync_bulk_sync_subscriptions (void)
{
    cmsg_subscription_info sub_info_1 = CMSG_SUBSCRIPTION_INFO_INIT;
    cmsg_subscription_info sub_info_2 = CMSG_SUBSCRIPTION_INFO_INIT;
    cmsg_subscription_info sub_info_3 = CMSG_SUBSCRIPTION_INFO_INIT;
    cmsg_transport *transport = NULL;
    struct remote_sync_data remote;

    CMSG_SET_FIELD_VALUE (&sub_info_1, remote_addr, 1111);
    CMSG_SET_FIELD_VALUE (&sub_info_2, remote_addr, 1111);
    CMSG_SET_FIELD_VALUE (&sub_info_3, remote_addr, 2222);

    mock_remote_subscriptions_list = g_list_append (mock_remote_subscriptions_list,
                                                    &sub_info_1);
    mock_remote_subscriptions_list = g_list_append (mock_remote_subscriptions_list,
                                                    &sub_info_2);
    mock_remote_subscriptions_list = g_list_append (mock_remote_subscriptions_list,
                                                    &sub_info_3);

    transport = create_tcp_transport (1111);
    remote.client = cmsg_client_new (transport, &test_descriptor);
    remote.timer_id = 0;

    np_mock (data_get_remote_subscriptions, sm_mock_data_get_remote_subscriptions);
    np_mock (cmsg_api_invoke, sm_mock_cmsg_api_invoke);
    remote_sync_bulk_sync_subscriptions (&remote);

    NP_ASSERT_EQUAL (g_list_length (remote_subscriptions_seen), 2);

    cmsg_destroy_client_and_transport (remote.client);
    g_list_free (mock_remote_subscriptions_list);
    g_list_free (remote_subscriptions_seen);
}

void
test_remote_sync_subscription_added_no_remote_host (void)
{
    cmsg_subscription_info sub_info = CMSG_SUBSCRIPTION_INFO_INIT;

    CMSG_SET_FIELD_VALUE (&sub_info, remote_addr, 1111);

    np_mock (cmsg_api_invoke, sm_mock_cmsg_api_invoke);
    remote_sync_subscription_added (&sub_info);
    NP_ASSERT_EQUAL (cmsg_psd_remote_sync_api_add_subscription_called, 0);
}

void
test_remote_sync_subscription_removed_no_remote_host (void)
{
    cmsg_subscription_info sub_info = CMSG_SUBSCRIPTION_INFO_INIT;

    CMSG_SET_FIELD_VALUE (&sub_info, remote_addr, 1111);

    np_mock (cmsg_api_invoke, sm_mock_cmsg_api_invoke);
    remote_sync_subscription_removed (&sub_info);
    NP_ASSERT_EQUAL (cmsg_psd_remote_sync_api_remove_subscription_called, 0);
}

void
test_remote_sync_subscription_added_remote_host_no_match (void)
{
    cmsg_subscription_info sub_info = CMSG_SUBSCRIPTION_INFO_INIT;
    cmsg_transport *test_transport = NULL;
    cmsg_transport *server_transport = NULL;

    server_transport = create_tcp_transport (1234);
    remote_sync_server = cmsg_server_create (server_transport, &test_service);

    test_transport = create_tcp_transport (1111);

    np_mock_by_name ("remote_sync_bulk_sync_subscriptions",
                     sm_mock_remote_sync_bulk_sync_subscriptions);
    remote_sync_sl_event_handler (test_transport, true, NULL);

    CMSG_SET_FIELD_VALUE (&sub_info, remote_addr, 2222);

    np_mock (cmsg_api_invoke, sm_mock_cmsg_api_invoke);
    remote_sync_subscription_added (&sub_info);
    NP_ASSERT_EQUAL (cmsg_psd_remote_sync_api_add_subscription_called, 0);

    np_mock (cmsg_service_listener_remove_server,
             sm_mock_cmsg_service_listener_remove_server);
    cmsg_destroy_server_and_transport (remote_sync_server);
    remote_sync_server = NULL;
    cmsg_transport_destroy (test_transport);
}

void
test_remote_sync_subscription_removed_remote_host_no_match (void)
{
    cmsg_subscription_info sub_info = CMSG_SUBSCRIPTION_INFO_INIT;
    cmsg_transport *test_transport = NULL;
    cmsg_transport *server_transport = NULL;

    server_transport = create_tcp_transport (1234);
    remote_sync_server = cmsg_server_create (server_transport, &test_service);

    test_transport = create_tcp_transport (1111);

    np_mock_by_name ("remote_sync_bulk_sync_subscriptions",
                     sm_mock_remote_sync_bulk_sync_subscriptions);
    remote_sync_sl_event_handler (test_transport, true, NULL);

    CMSG_SET_FIELD_VALUE (&sub_info, remote_addr, 2222);

    np_mock (cmsg_api_invoke, sm_mock_cmsg_api_invoke);
    remote_sync_subscription_removed (&sub_info);
    NP_ASSERT_EQUAL (cmsg_psd_remote_sync_api_remove_subscription_called, 0);

    np_mock (cmsg_service_listener_remove_server,
             sm_mock_cmsg_service_listener_remove_server);
    cmsg_destroy_server_and_transport (remote_sync_server);
    remote_sync_server = NULL;
    cmsg_transport_destroy (test_transport);
}

void
test_remote_sync_subscription_added_remote_host_match (void)
{
    cmsg_subscription_info sub_info = CMSG_SUBSCRIPTION_INFO_INIT;
    cmsg_transport *test_transport = NULL;
    cmsg_transport *server_transport = NULL;

    server_transport = create_tcp_transport (1234);
    remote_sync_server = cmsg_server_create (server_transport, &test_service);

    test_transport = create_tcp_transport (1111);

    np_mock_by_name ("remote_sync_bulk_sync_subscriptions",
                     sm_mock_remote_sync_bulk_sync_subscriptions);
    remote_sync_sl_event_handler (test_transport, true, NULL);

    CMSG_SET_FIELD_VALUE (&sub_info, remote_addr, 1111);

    np_mock (cmsg_api_invoke, sm_mock_cmsg_api_invoke);
    remote_sync_subscription_added (&sub_info);
    NP_ASSERT_EQUAL (cmsg_psd_remote_sync_api_add_subscription_called, 1);

    np_mock (cmsg_service_listener_remove_server,
             sm_mock_cmsg_service_listener_remove_server);
    cmsg_destroy_server_and_transport (remote_sync_server);
    remote_sync_server = NULL;
    cmsg_transport_destroy (test_transport);
}

void
test_remote_sync_subscription_removed_remote_host_match (void)
{
    cmsg_subscription_info sub_info = CMSG_SUBSCRIPTION_INFO_INIT;
    cmsg_transport *test_transport = NULL;
    cmsg_transport *server_transport = NULL;

    server_transport = create_tcp_transport (1234);
    remote_sync_server = cmsg_server_create (server_transport, &test_service);

    test_transport = create_tcp_transport (1111);

    np_mock_by_name ("remote_sync_bulk_sync_subscriptions",
                     sm_mock_remote_sync_bulk_sync_subscriptions);
    remote_sync_sl_event_handler (test_transport, true, NULL);

    CMSG_SET_FIELD_VALUE (&sub_info, remote_addr, 1111);

    np_mock (cmsg_api_invoke, sm_mock_cmsg_api_invoke);
    remote_sync_subscription_removed (&sub_info);
    NP_ASSERT_EQUAL (cmsg_psd_remote_sync_api_remove_subscription_called, 1);

    np_mock (cmsg_service_listener_remove_server,
             sm_mock_cmsg_service_listener_remove_server);
    cmsg_destroy_server_and_transport (remote_sync_server);
    remote_sync_server = NULL;
    cmsg_transport_destroy (test_transport);
}

static int data_add_local_subscription_called = 0;
static const cmsg_subscription_info *data_add_local_subscription_last_info = NULL;
static int data_remove_local_subscription_called = 0;

static void
sm_mock_data_add_local_subscription (const cmsg_subscription_info *info)
{
    data_add_local_subscription_called++;
    data_add_local_subscription_last_info = info;
}

static void
sm_mock_data_remove_local_subscription (const cmsg_subscription_info *info)
{
    data_remove_local_subscription_called++;
}

static void
sm_mock_cmsg_server_send_response_subscription (const ProtobufCMessage *send_msg,
                                                const void *service)
{
    /* Do nothing. */
}

/**
 * Install mocks for everything the subscription implementations do once a
 * message has been accepted, so that the tests below only observe whether the
 * message was accepted or dropped.
 */
static void
install_subscription_mocks (void)
{
    data_add_local_subscription_called = 0;
    data_add_local_subscription_last_info = NULL;
    data_remove_local_subscription_called = 0;

    np_mock (data_add_local_subscription, sm_mock_data_add_local_subscription);
    np_mock (data_remove_local_subscription, sm_mock_data_remove_local_subscription);
    np_mock (cmsg_server_send_response, sm_mock_cmsg_server_send_response_subscription);
}

/**
 * Build a TCP 'cmsg_transport_info' on the stack using the supplied address and
 * port buffer lengths. Remote stack members are not trusted to send sane
 * lengths, so tests need to be able to claim any length.
 *
 * @param transport_info - The 'cmsg_transport_info' to initialise.
 * @param tcp_info - The 'cmsg_tcp_transport_info' to initialise.
 * @param addr - The address buffer.
 * @param addr_len - The length to claim for the address buffer.
 * @param port - The port buffer.
 * @param port_len - The length to claim for the port buffer.
 */
static void
init_subscription_tcp_transport_info (cmsg_transport_info *transport_info,
                                      cmsg_tcp_transport_info *tcp_info,
                                      uint8_t *addr, uint32_t addr_len,
                                      uint8_t *port, uint32_t port_len)
{
    CMSG_SET_FIELD_VALUE (tcp_info, ipv4, true);
    CMSG_SET_FIELD_BYTES (tcp_info, addr, addr, addr_len);
    CMSG_SET_FIELD_BYTES (tcp_info, port, port, port_len);

    CMSG_SET_FIELD_VALUE (transport_info, type, CMSG_TRANSPORT_INFO_TYPE_TCP);
    CMSG_SET_FIELD_ONEOF (transport_info, tcp_info, tcp_info,
                          data, CMSG_TRANSPORT_INFO_DATA_TCP_INFO);
}

/**
 * Baseline for the length tests below: a well formed subscription synced from
 * a remote member is added.
 */
void
test_cmsg_psd_remote_sync_impl_add_subscription_valid_lengths (void)
{
    cmsg_subscription_info info = CMSG_SUBSCRIPTION_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr)] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    install_subscription_mocks ();

    init_subscription_tcp_transport_info (&transport_info, &tcp_info,
                                          addr, sizeof (addr), port, sizeof (port));
    CMSG_SET_FIELD_PTR (&info, service, "test_service");
    CMSG_SET_FIELD_PTR (&info, method_name, "test_method");
    CMSG_SET_FIELD_PTR (&info, transport_info, &transport_info);

    cmsg_psd_remote_sync_impl_add_subscription (NULL, &info);

    NP_ASSERT_EQUAL (data_add_local_subscription_called, 1);
}

/**
 * A stack member claiming an address longer than the buffer it sent must be
 * rejected. The daemon stores a copy of the message and later builds a
 * transport from it, copying the claimed number of bytes into a fixed size
 * socket address.
 */
void
test_cmsg_psd_remote_sync_impl_add_subscription_addr_too_long (void)
{
    cmsg_subscription_info info = CMSG_SUBSCRIPTION_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in6_addr)] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    install_subscription_mocks ();

    init_subscription_tcp_transport_info (&transport_info, &tcp_info,
                                          addr, sizeof (addr), port, sizeof (port));
    CMSG_SET_FIELD_PTR (&info, service, "test_service");
    CMSG_SET_FIELD_PTR (&info, method_name, "test_method");
    CMSG_SET_FIELD_PTR (&info, transport_info, &transport_info);

    np_syslog_match ("Invalid IPv4 address length", 0);

    cmsg_psd_remote_sync_impl_add_subscription (NULL, &info);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_EQUAL (data_add_local_subscription_called, 0);
}

/**
 * A subscription synced from a member with no transport information at all
 * must be dropped rather than dereferenced. Every field of
 * 'cmsg_subscription_info' is optional, so a client can simply leave it out.
 */
void
test_cmsg_psd_remote_sync_impl_add_subscription_no_transport_info (void)
{
    cmsg_subscription_info info = CMSG_SUBSCRIPTION_INFO_INIT;

    install_subscription_mocks ();

    CMSG_SET_FIELD_PTR (&info, service, "test_service");
    CMSG_SET_FIELD_PTR (&info, method_name, "test_method");

    np_syslog_match ("Missing transport info", 0);

    cmsg_psd_remote_sync_impl_add_subscription (NULL, &info);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_EQUAL (data_add_local_subscription_called, 0);
}

/**
 * Removing a synced subscription compares the supplied transport against the
 * stored subscriptions using the length the member claimed, so a bad length
 * must be dropped before it reaches the data layer.
 */
void
test_cmsg_psd_remote_sync_impl_remove_subscription_addr_too_long (void)
{
    cmsg_subscription_info info = CMSG_SUBSCRIPTION_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in6_addr)] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    install_subscription_mocks ();

    init_subscription_tcp_transport_info (&transport_info, &tcp_info,
                                          addr, sizeof (addr), port, sizeof (port));
    CMSG_SET_FIELD_PTR (&info, service, "test_service");
    CMSG_SET_FIELD_PTR (&info, method_name, "test_method");
    CMSG_SET_FIELD_PTR (&info, transport_info, &transport_info);

    np_syslog_match ("Invalid IPv4 address length", 0);

    cmsg_psd_remote_sync_impl_remove_subscription (NULL, &info);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_EQUAL (data_remove_local_subscription_called, 0);
}

/**
 * A bulk sync carrying a mix of well formed and too-long entries must add the
 * well formed entries and drop only the too-long ones.
 */
void
test_cmsg_psd_remote_sync_impl_bulk_sync_addr_too_long (void)
{
    cmsg_psd_bulk_sync_data bulk_sync_data = CMSG_PSD_BULK_SYNC_DATA_INIT;
    cmsg_subscription_info bad_info = CMSG_SUBSCRIPTION_INFO_INIT;
    cmsg_transport_info bad_transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info bad_tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    cmsg_subscription_info good_info = CMSG_SUBSCRIPTION_INFO_INIT;
    cmsg_transport_info good_transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info good_tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t long_addr[sizeof (struct in6_addr)] = { };
    uint8_t addr[sizeof (struct in_addr)] = { };
    uint8_t port[sizeof (in_port_t)] = { };
    cmsg_subscription_info *entries[2] = { };

    install_subscription_mocks ();

    init_subscription_tcp_transport_info (&bad_transport_info, &bad_tcp_info,
                                          long_addr, sizeof (long_addr),
                                          port, sizeof (port));
    CMSG_SET_FIELD_PTR (&bad_info, service, "test_service");
    CMSG_SET_FIELD_PTR (&bad_info, method_name, "test_method");
    CMSG_SET_FIELD_PTR (&bad_info, transport_info, &bad_transport_info);

    init_subscription_tcp_transport_info (&good_transport_info, &good_tcp_info,
                                          addr, sizeof (addr), port, sizeof (port));
    CMSG_SET_FIELD_PTR (&good_info, service, "test_service");
    CMSG_SET_FIELD_PTR (&good_info, method_name, "test_method");
    CMSG_SET_FIELD_PTR (&good_info, transport_info, &good_transport_info);

    entries[0] = &bad_info;
    entries[1] = &good_info;
    CMSG_SET_FIELD_REPEATED (&bulk_sync_data, data, entries, 2);

    np_syslog_match ("Invalid IPv4 address length", 0);

    cmsg_psd_remote_sync_impl_bulk_sync (NULL, &bulk_sync_data);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_EQUAL (data_add_local_subscription_called, 1);
    NP_ASSERT_PTR_EQUAL (data_add_local_subscription_last_info, &good_info);
}
