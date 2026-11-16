/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Unit tests for the remote sync functionality.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include <np.h>
#include "../remote_sync.h"
#include "../remote_sync_impl_auto.h"
#include "../remote_sync_api_auto.h"
#include "../data.h"
#include <cmsg/cmsg_glib_helpers.h>
#include <cmsg/cmsg_composite_client.h>

/**
 * This informs the compiler that the function is, in fact, being used even though it
 * doesn't look like it. This is useful for static functions that get found by NovaProva
 * using debug symbols.
 */
#define USED __attribute__ ((used))

extern cmsg_server *remote_sync_server;
extern uint32_t local_ip_addr;
extern cmsg_client *comp_client;

static cmsg_server *server_test_ptr = (cmsg_server *) 0x15876;
static cmsg_client *remote_client = NULL;
static int data_add_server_called = 0;
static const cmsg_service_info *data_add_server_last_info = NULL;
static int data_remove_server_called = 0;

static int USED
set_up (void)
{
    remote_sync_server = NULL;
    local_ip_addr = 0;
    comp_client = NULL;

    remote_client = NULL;

    data_add_server_called = 0;
    data_add_server_last_info = NULL;
    data_remove_server_called = 0;

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
sm_mock_remote_sync_bulk_sync_services (cmsg_client *client)
{

}

static cmsg_client *
sm_mock_cmsg_create_client_tcp_ipv4_oneway (const char *service_name,
                                            struct in_addr *addr, const char *vrf_bind_dev,
                                            const ProtobufCServiceDescriptor *descriptor)
{
    remote_client = cmsg_create_client_loopback (CMSG_SERVICE (cmsg_sld, remote_sync));
    return remote_client;
}

static cmsg_client *
sm_mock_cmsg_composite_client_lookup_by_tcp_ipv4_addr (cmsg_client *_composite_client,
                                                       struct in_addr addr)
{
    return remote_client;
}

static int
sm_mock_cmsg_api_invoke (cmsg_client *client, const cmsg_api_descriptor *cmsg_desc,
                         int method_index, const ProtobufCMessage *send_msg,
                         ProtobufCMessage **recv_msg)
{
    if (cmsg_desc->service_desc == &cmsg_sld_remote_sync_descriptor)
    {
        switch (method_index)
        {
        case cmsg_sld_remote_sync_api_add_server_index:
        case cmsg_sld_remote_sync_api_remove_server_index:
            return CMSG_RET_OK;
        default:
            break;
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

    remote_sync_address_set (addr);

    NP_ASSERT_PTR_EQUAL (remote_sync_server, server_test_ptr);
    NP_ASSERT_EQUAL (test_addr, local_ip_addr);
}

void
test_remote_sync_address_set_called_twice (void)
{
    struct in_addr addr = { };
    const uint32_t test_addr = 1234;

    addr.s_addr = test_addr;

    np_mock (cmsg_glib_tcp_server_init_oneway,
             sm_mock_cmsg_glib_tcp_server_init_oneway_ptr_return);

    remote_sync_address_set (addr);

    np_mock (cmsg_glib_tcp_server_init_oneway,
             sm_mock_cmsg_glib_tcp_server_init_oneway_fail);

    NP_ASSERT_PTR_EQUAL (remote_sync_server, server_test_ptr);
    NP_ASSERT_EQUAL (test_addr, local_ip_addr);
}

void
test_remote_sync_add_delete_host (void)
{
    struct in_addr addr = { };

    np_mock_by_name ("remote_sync_bulk_sync_services",
                     sm_mock_remote_sync_bulk_sync_services);

    np_mock (cmsg_create_client_tcp_ipv4_oneway,
             sm_mock_cmsg_create_client_tcp_ipv4_oneway);
    remote_sync_add_host (addr);

    np_mock (cmsg_composite_client_lookup_by_tcp_ipv4_addr,
             sm_mock_cmsg_composite_client_lookup_by_tcp_ipv4_addr);

    remote_sync_delete_host (addr);
    NP_ASSERT_NULL (comp_client);
}

void
test_remote_sync_server_added_no_comp_client (void)
{
    cmsg_service_info service_info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;

    comp_client = NULL;

    CMSG_SET_FIELD_PTR (&service_info, service, "test_service");
    CMSG_SET_FIELD_PTR (&service_info, server_info, &transport_info);

    NP_ASSERT_FALSE (remote_sync_server_added (&service_info));
}

void
test_remote_sync_server_added_tport_not_tcp (void)
{
    cmsg_service_info service_info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;

    comp_client = (cmsg_client *) 0x1;

    CMSG_SET_FIELD_VALUE (&transport_info, type, CMSG_TRANSPORT_INFO_TYPE_UNIX);
    CMSG_SET_FIELD_PTR (&service_info, service, "test_service");
    CMSG_SET_FIELD_PTR (&service_info, server_info, &transport_info);

    NP_ASSERT_FALSE (remote_sync_server_added (&service_info));
}

void
test_remote_sync_server_added_tport_not_ipv4 (void)
{
    cmsg_service_info service_info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;

    comp_client = (cmsg_client *) 0x1;

    CMSG_SET_FIELD_VALUE (&tcp_info, ipv4, false);
    CMSG_SET_FIELD_VALUE (&transport_info, type, CMSG_TRANSPORT_INFO_TYPE_TCP);
    CMSG_SET_FIELD_ONEOF (&transport_info, tcp_info, &tcp_info,
                          data, CMSG_TRANSPORT_INFO_DATA_TCP_INFO);
    CMSG_SET_FIELD_PTR (&service_info, service, "test_service");
    CMSG_SET_FIELD_PTR (&service_info, server_info, &transport_info);

    NP_ASSERT_FALSE (remote_sync_server_added (&service_info));
}

void
test_remote_sync_server_added_address_not_local (void)
{
    cmsg_service_info service_info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint32_t non_local_ip_addr;

    comp_client = (cmsg_client *) 0x1;
    local_ip_addr = 1234;
    non_local_ip_addr = local_ip_addr + 1;

    CMSG_SET_FIELD_VALUE (&tcp_info, ipv4, true);
    CMSG_SET_FIELD_BYTES (&tcp_info, addr, (void *) &non_local_ip_addr,
                          sizeof (non_local_ip_addr));
    CMSG_SET_FIELD_VALUE (&transport_info, type, CMSG_TRANSPORT_INFO_TYPE_TCP);
    CMSG_SET_FIELD_ONEOF (&transport_info, tcp_info, &tcp_info,
                          data, CMSG_TRANSPORT_INFO_DATA_TCP_INFO);
    CMSG_SET_FIELD_PTR (&service_info, service, "test_service");
    CMSG_SET_FIELD_PTR (&service_info, server_info, &transport_info);

    NP_ASSERT_FALSE (remote_sync_server_added (&service_info));
}

void
test_remote_sync_server_added_address_local (void)
{
    cmsg_service_info service_info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;

    np_mock (cmsg_api_invoke, sm_mock_cmsg_api_invoke);

    comp_client = (cmsg_client *) 0x1;
    local_ip_addr = 1234;

    CMSG_SET_FIELD_VALUE (&tcp_info, ipv4, true);
    CMSG_SET_FIELD_BYTES (&tcp_info, addr, (void *) &local_ip_addr, sizeof (local_ip_addr));
    CMSG_SET_FIELD_VALUE (&transport_info, type, CMSG_TRANSPORT_INFO_TYPE_TCP);
    CMSG_SET_FIELD_ONEOF (&transport_info, tcp_info, &tcp_info,
                          data, CMSG_TRANSPORT_INFO_DATA_TCP_INFO);
    CMSG_SET_FIELD_PTR (&service_info, service, "test_service");
    CMSG_SET_FIELD_PTR (&service_info, server_info, &transport_info);

    NP_ASSERT_TRUE (remote_sync_server_added (&service_info));
}

void
test_remote_sync_server_removed_address_local (void)
{
    cmsg_service_info service_info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;

    np_mock (cmsg_api_invoke, sm_mock_cmsg_api_invoke);

    comp_client = (cmsg_client *) 0x1;
    local_ip_addr = 1234;

    CMSG_SET_FIELD_VALUE (&tcp_info, ipv4, true);
    CMSG_SET_FIELD_BYTES (&tcp_info, addr, (void *) &local_ip_addr, sizeof (local_ip_addr));
    CMSG_SET_FIELD_VALUE (&transport_info, type, CMSG_TRANSPORT_INFO_TYPE_TCP);
    CMSG_SET_FIELD_ONEOF (&transport_info, tcp_info, &tcp_info,
                          data, CMSG_TRANSPORT_INFO_DATA_TCP_INFO);
    CMSG_SET_FIELD_PTR (&service_info, service, "test_service");
    CMSG_SET_FIELD_PTR (&service_info, server_info, &transport_info);

    NP_ASSERT_TRUE (remote_sync_server_removed (&service_info));
}

static void
sm_mock_data_add_server (cmsg_service_info *server_info, bool local)
{
    data_add_server_called++;
    data_add_server_last_info = server_info;
}

static void
sm_mock_data_remove_server (const cmsg_service_info *server_info, bool local)
{
    data_remove_server_called++;
}

static void
sm_mock_cmsg_server_app_owns_current_msg_set (cmsg_server *server)
{
}

static void
sm_mock_cmsg_server_send_response (const ProtobufCMessage *send_msg, const void *service)
{
}

/**
 * Install mocks for everything the 'add_server'/'remove_server'/'bulk_sync'
 * implementations do once a message has been accepted, so that the tests below
 * only observe whether the message was accepted or dropped.
 */
static void
install_server_mocks (void)
{
    np_mock (data_add_server, sm_mock_data_add_server);
    np_mock (data_remove_server, sm_mock_data_remove_server);
    np_mock (cmsg_server_app_owns_current_msg_set,
             sm_mock_cmsg_server_app_owns_current_msg_set);
    np_mock (cmsg_server_send_response, sm_mock_cmsg_server_send_response);
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
init_tcp_transport_info (cmsg_transport_info *transport_info,
                         cmsg_tcp_transport_info *tcp_info,
                         uint8_t *addr, uint32_t addr_len, uint8_t *port, uint32_t port_len)
{
    CMSG_SET_FIELD_VALUE (tcp_info, ipv4, true);
    CMSG_SET_FIELD_BYTES (tcp_info, addr, addr, addr_len);
    CMSG_SET_FIELD_BYTES (tcp_info, port, port, port_len);

    CMSG_SET_FIELD_VALUE (transport_info, type, CMSG_TRANSPORT_INFO_TYPE_TCP);
    CMSG_SET_FIELD_ONEOF (transport_info, tcp_info, tcp_info,
                          data, CMSG_TRANSPORT_INFO_DATA_TCP_INFO);
}

/**
 * Baseline for the length tests below: a well formed remote TCP server is
 * added.
 */
void
test_cmsg_sld_remote_sync_impl_add_server_valid_lengths (void)
{
    cmsg_service_info service_info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr)] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    install_server_mocks ();

    init_tcp_transport_info (&transport_info, &tcp_info,
                             addr, sizeof (addr), port, sizeof (port));
    CMSG_SET_FIELD_PTR (&service_info, service, "test_service");
    CMSG_SET_FIELD_PTR (&service_info, server_info, &transport_info);

    cmsg_sld_remote_sync_impl_add_server (NULL, &service_info);

    NP_ASSERT_EQUAL (data_add_server_called, 1);
}

/**
 * A stack member sending an address longer than an IPv4 address must not have
 * its server stored, otherwise the stored length would be used to compare more
 * bytes than the buffer holds.
 */
void
test_cmsg_sld_remote_sync_impl_add_server_addr_too_long (void)
{
    cmsg_service_info service_info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in6_addr)] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    install_server_mocks ();

    init_tcp_transport_info (&transport_info, &tcp_info,
                             addr, sizeof (addr), port, sizeof (port));
    CMSG_SET_FIELD_PTR (&service_info, service, "test_service");
    CMSG_SET_FIELD_PTR (&service_info, server_info, &transport_info);

    np_syslog_match ("Invalid IPv4 address length", 0);

    cmsg_sld_remote_sync_impl_add_server (NULL, &service_info);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_EQUAL (data_add_server_called, 0);
}

/**
 * A stack member sending a port longer than a TCP port must not have its
 * server stored.
 */
void
test_cmsg_sld_remote_sync_impl_add_server_port_too_long (void)
{
    cmsg_service_info service_info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr)] = { };
    uint8_t port[sizeof (in_port_t) * 2] = { };

    install_server_mocks ();

    init_tcp_transport_info (&transport_info, &tcp_info,
                             addr, sizeof (addr), port, sizeof (port));
    CMSG_SET_FIELD_PTR (&service_info, service, "test_service");
    CMSG_SET_FIELD_PTR (&service_info, server_info, &transport_info);

    np_syslog_match ("Invalid TCP port length", 0);

    cmsg_sld_remote_sync_impl_add_server (NULL, &service_info);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_EQUAL (data_add_server_called, 0);
}

/**
 * A bulk sync carrying a mix of well formed and over long entries must add the
 * well formed entries and drop only the over long ones. Dropped entries are
 * left in the message so that their memory is still freed by the caller.
 */
void
test_cmsg_sld_remote_sync_impl_bulk_sync_addr_too_long (void)
{
    cmsg_sld_bulk_sync_data bulk_sync_data = CMSG_SLD_BULK_SYNC_DATA_INIT;
    cmsg_service_info bad_service_info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info bad_transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info bad_tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    cmsg_service_info good_service_info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info good_transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info good_tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t long_addr[sizeof (struct in6_addr)] = { };
    uint8_t addr[sizeof (struct in_addr)] = { };
    uint8_t port[sizeof (in_port_t)] = { };
    cmsg_service_info *entries[2] = { };

    install_server_mocks ();

    init_tcp_transport_info (&bad_transport_info, &bad_tcp_info,
                             long_addr, sizeof (long_addr), port, sizeof (port));
    CMSG_SET_FIELD_PTR (&bad_service_info, service, "test_service");
    CMSG_SET_FIELD_PTR (&bad_service_info, server_info, &bad_transport_info);

    init_tcp_transport_info (&good_transport_info, &good_tcp_info,
                             addr, sizeof (addr), port, sizeof (port));
    CMSG_SET_FIELD_PTR (&good_service_info, service, "test_service");
    CMSG_SET_FIELD_PTR (&good_service_info, server_info, &good_transport_info);

    entries[0] = &bad_service_info;
    entries[1] = &good_service_info;
    CMSG_SET_FIELD_REPEATED (&bulk_sync_data, data, entries, 2);

    np_syslog_match ("Invalid IPv4 address length", 0);

    cmsg_sld_remote_sync_impl_bulk_sync (NULL, &bulk_sync_data);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_EQUAL (data_add_server_called, 1);
    NP_ASSERT_PTR_EQUAL (data_add_server_last_info, &good_service_info);

    /* The over long entry is still owned by the message, the accepted one is not. */
    NP_ASSERT_PTR_EQUAL (bulk_sync_data.data[0], &bad_service_info);
    NP_ASSERT_NULL (bulk_sync_data.data[1]);
}

/**
 * A stack member sending a message with no server information at all must have
 * it dropped rather than dereferenced. Every field of 'cmsg_service_info' is
 * optional, so a member can simply leave the server information out.
 */
void
test_cmsg_sld_remote_sync_impl_add_server_no_server_info (void)
{
    cmsg_service_info service_info = CMSG_SERVICE_INFO_INIT;

    install_server_mocks ();

    CMSG_SET_FIELD_PTR (&service_info, service, "test_service");

    np_syslog_match ("Missing transport info", 0);

    cmsg_sld_remote_sync_impl_add_server (NULL, &service_info);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_EQUAL (data_add_server_called, 0);
}

/**
 * Baseline for the remove tests: a well formed remote TCP server is removed.
 */
void
test_cmsg_sld_remote_sync_impl_remove_server_valid_lengths (void)
{
    cmsg_service_info service_info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr)] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    install_server_mocks ();

    init_tcp_transport_info (&transport_info, &tcp_info,
                             addr, sizeof (addr), port, sizeof (port));
    CMSG_SET_FIELD_PTR (&service_info, service, "test_service");
    CMSG_SET_FIELD_PTR (&service_info, server_info, &transport_info);

    cmsg_sld_remote_sync_impl_remove_server (NULL, &service_info);

    NP_ASSERT_EQUAL (data_remove_server_called, 1);
}

/**
 * Removing is as risky as adding: the address supplied by the stack member is
 * compared against the stored servers using the length of the stored entry, so
 * a shorter address is read out of bounds. It must be dropped before it reaches
 * 'data_remove_server'.
 */
void
test_cmsg_sld_remote_sync_impl_remove_server_addr_too_short (void)
{
    cmsg_service_info service_info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr) - 1] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    install_server_mocks ();

    init_tcp_transport_info (&transport_info, &tcp_info,
                             addr, sizeof (addr), port, sizeof (port));
    CMSG_SET_FIELD_PTR (&service_info, service, "test_service");
    CMSG_SET_FIELD_PTR (&service_info, server_info, &transport_info);

    np_syslog_match ("Invalid IPv4 address length", 0);

    cmsg_sld_remote_sync_impl_remove_server (NULL, &service_info);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_EQUAL (data_remove_server_called, 0);
}

/**
 * A remove message with no server information at all must be dropped rather
 * than dereferenced.
 */
void
test_cmsg_sld_remote_sync_impl_remove_server_no_server_info (void)
{
    cmsg_service_info service_info = CMSG_SERVICE_INFO_INIT;

    install_server_mocks ();

    CMSG_SET_FIELD_PTR (&service_info, service, "test_service");

    np_syslog_match ("Missing transport info", 0);

    cmsg_sld_remote_sync_impl_remove_server (NULL, &service_info);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_EQUAL (data_remove_server_called, 0);
}
