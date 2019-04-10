/*
 * Unit tests for the remote sync functionality.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include <np.h>
#include "../remote_sync.h"
#include "../remote_sync_impl_auto.h"
#include "../remote_sync_api_auto.h"
#include <cmsg/cmsg_glib_helpers.h>
#include <cmsg/cmsg_composite_client.h>

/**
 * This informs the compiler that the function is, in fact, being used even though it
 * doesn't look like it. This is useful for static functions that get found by NovaProva
 * using debug symbols.
 */
#define USED __attribute__ ((used))

extern cmsg_server_accept_thread_info *server_info;
extern uint32_t local_ip_addr;
extern cmsg_client *comp_client;

static cmsg_server_accept_thread_info *server_info_test_ptr =
    (cmsg_server_accept_thread_info *) 0x15876;
static cmsg_client *remote_client = NULL;

static int USED
set_up (void)
{
    server_info = NULL;
    local_ip_addr = 0;
    comp_client = NULL;

    remote_client = NULL;

    return 0;
}

static cmsg_server_accept_thread_info *
sm_mock_cmsg_glib_tcp_server_init_oneway_ptr_return (const char *service_name,
                                                     struct in_addr *addr,
                                                     ProtobufCService *service)
{
    return server_info_test_ptr;
}

static cmsg_server_accept_thread_info *
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
                                            struct in_addr *addr,
                                            const ProtobufCServiceDescriptor *descriptor)
{
    remote_client = cmsg_create_client_loopback (CMSG_SERVICE (cmsg_sld, remote_sync));
    return remote_client;
}

static cmsg_client *
sm_mock_cmsg_composite_client_lookup_by_tcp_ipv4_addr (cmsg_client *_composite_client,
                                                       uint32_t addr)
{
    return remote_client;
}

static int
sm_mock_cmsg_sld_remote_sync_api_add_server (cmsg_client *_client,
                                             const cmsg_service_info *_send_msg)
{
    return CMSG_RET_OK;
}

static int
sm_mock_cmsg_sld_remote_sync_api_remove_server (cmsg_client *_client,
                                                const cmsg_service_info *_send_msg)
{
    return CMSG_RET_OK;
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

    NP_ASSERT_PTR_EQUAL (server_info, server_info_test_ptr);
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

    NP_ASSERT_PTR_EQUAL (server_info, server_info_test_ptr);
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
    CMSG_SET_FIELD_PTR (&service_info, server_info, &transport_info);

    NP_ASSERT_FALSE (remote_sync_server_added (&service_info));
}

void
test_remote_sync_server_added_address_local (void)
{
    cmsg_service_info service_info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;

    np_mock (cmsg_sld_remote_sync_api_add_server,
             sm_mock_cmsg_sld_remote_sync_api_add_server);

    comp_client = (cmsg_client *) 0x1;
    local_ip_addr = 1234;

    CMSG_SET_FIELD_VALUE (&tcp_info, ipv4, true);
    CMSG_SET_FIELD_BYTES (&tcp_info, addr, (void *) &local_ip_addr, sizeof (local_ip_addr));
    CMSG_SET_FIELD_VALUE (&transport_info, type, CMSG_TRANSPORT_INFO_TYPE_TCP);
    CMSG_SET_FIELD_ONEOF (&transport_info, tcp_info, &tcp_info,
                          data, CMSG_TRANSPORT_INFO_DATA_TCP_INFO);
    CMSG_SET_FIELD_PTR (&service_info, server_info, &transport_info);

    NP_ASSERT_TRUE (remote_sync_server_added (&service_info));
}

void
test_remote_sync_server_removed_address_local (void)
{
    cmsg_service_info service_info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;

    np_mock (cmsg_sld_remote_sync_api_remove_server,
             sm_mock_cmsg_sld_remote_sync_api_remove_server);

    comp_client = (cmsg_client *) 0x1;
    local_ip_addr = 1234;

    CMSG_SET_FIELD_VALUE (&tcp_info, ipv4, true);
    CMSG_SET_FIELD_BYTES (&tcp_info, addr, (void *) &local_ip_addr, sizeof (local_ip_addr));
    CMSG_SET_FIELD_VALUE (&transport_info, type, CMSG_TRANSPORT_INFO_TYPE_TCP);
    CMSG_SET_FIELD_ONEOF (&transport_info, tcp_info, &tcp_info,
                          data, CMSG_TRANSPORT_INFO_DATA_TCP_INFO);
    CMSG_SET_FIELD_PTR (&service_info, server_info, &transport_info);

    NP_ASSERT_TRUE (remote_sync_server_removed (&service_info));
}
