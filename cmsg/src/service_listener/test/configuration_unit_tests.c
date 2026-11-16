/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Unit tests for the configuration functionality.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include <np.h>
#include <stdint.h>
#include "../configuration_impl_auto.h"
#include "../data.h"

/**
 * This informs the compiler that the function is, in fact, being used even though it
 * doesn't look like it. This is useful for static functions that get found by NovaProva
 * using debug symbols.
 */
#define USED __attribute__ ((used))

static bool cmsg_server_app_owns_current_msg_set_called = false;
static bool data_add_server_called = false;
static bool data_remove_server_called = false;
static bool data_add_listener_called = false;
static bool data_remove_listener_called = false;

static void
sm_mock_cmsg_server_app_owns_current_msg_set (cmsg_server *server)
{
    cmsg_server_app_owns_current_msg_set_called = true;
}

static void
sm_mock_data_add_server (const cmsg_service_info *server_info, bool local)
{
    data_add_server_called = true;
}

static void
sm_mock_data_remove_server (const cmsg_service_info *server_info, bool local)
{
    data_remove_server_called = true;
}

static void
sm_mock_data_add_listener (const cmsg_sld_listener_info *info)
{
    data_add_listener_called = true;
}

static void
sm_mock_data_remove_listener (const cmsg_sld_listener_info *info)
{
    data_remove_listener_called = true;
}

static void
sm_mock_cmsg_server_send_response (const ProtobufCMessage *send_msg, const void *service)
{
}

static int USED
set_up (void)
{
    cmsg_server_app_owns_current_msg_set_called = false;
    data_add_server_called = false;
    data_remove_server_called = false;
    data_add_listener_called = false;
    data_remove_listener_called = false;

    np_mock (cmsg_server_app_owns_current_msg_set,
             sm_mock_cmsg_server_app_owns_current_msg_set);
    np_mock (data_remove_server, sm_mock_data_remove_server);
    np_mock (data_add_server, sm_mock_data_add_server);
    np_mock (data_add_listener, sm_mock_data_add_listener);
    np_mock (data_remove_listener, sm_mock_data_remove_listener);
    np_mock (cmsg_server_send_response, sm_mock_cmsg_server_send_response);

    return 0;
}

/**
 * Build a TCP 'cmsg_transport_info' on using the supplied address and port
 * buffer lengths. Other hosts are not trusted to send sane lengths, so tests
 * need to be able to claim any length.
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
 * Build a UNIX 'cmsg_transport_info' on the stack. A path is required, so the
 * tests that expect a message to be accepted must supply one.
 *
 * @param transport_info - The 'cmsg_transport_info' to initialise.
 * @param unix_info - The 'cmsg_unix_transport_info' to initialise.
 * @param path - The path to use, or NULL to leave it unset.
 */
static void
init_unix_transport_info (cmsg_transport_info *transport_info,
                          cmsg_unix_transport_info *unix_info, const char *path)
{
    if (path)
    {
        CMSG_SET_FIELD_PTR (unix_info, path, (char *) path);
    }

    CMSG_SET_FIELD_VALUE (transport_info, type, CMSG_TRANSPORT_INFO_TYPE_UNIX);
    CMSG_SET_FIELD_ONEOF (transport_info, unix_info, unix_info,
                          data, CMSG_TRANSPORT_INFO_DATA_UNIX_INFO);
}

void
test_cmsg_sld_configuration_impl_add_server_tcp (void)
{
    cmsg_service_info service_info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr)] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    init_tcp_transport_info (&transport_info, &tcp_info,
                             addr, sizeof (addr), port, sizeof (port));
    CMSG_SET_FIELD_PTR (&service_info, service, "test_service");
    CMSG_SET_FIELD_PTR (&service_info, server_info, &transport_info);

    cmsg_sld_configuration_impl_add_server (NULL, &service_info);

    NP_ASSERT_TRUE (cmsg_server_app_owns_current_msg_set_called);
    NP_ASSERT_TRUE (data_add_server_called);
    NP_ASSERT_FALSE (data_remove_server_called);
}

void
test_cmsg_sld_configuration_impl_add_server_unix (void)
{
    cmsg_service_info service_info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_unix_transport_info unix_info = CMSG_UNIX_TRANSPORT_INFO_INIT;

    init_unix_transport_info (&transport_info, &unix_info, "/tmp/test");
    CMSG_SET_FIELD_PTR (&service_info, service, "test_service");
    CMSG_SET_FIELD_PTR (&service_info, server_info, &transport_info);

    cmsg_sld_configuration_impl_add_server (NULL, &service_info);

    NP_ASSERT_TRUE (cmsg_server_app_owns_current_msg_set_called);
    NP_ASSERT_TRUE (data_add_server_called);
    NP_ASSERT_FALSE (data_remove_server_called);
}

/**
 * A local process sending an address longer than an IPv4 address must not have
 * its server stored, otherwise the stored length would be used to compare more
 * bytes than the buffer holds.
 */
void
test_cmsg_sld_configuration_impl_add_server_tcp_addr_too_long (void)
{
    cmsg_service_info service_info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in6_addr)] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    init_tcp_transport_info (&transport_info, &tcp_info,
                             addr, sizeof (addr), port, sizeof (port));
    CMSG_SET_FIELD_PTR (&service_info, service, "test_service");
    CMSG_SET_FIELD_PTR (&service_info, server_info, &transport_info);

    np_syslog_match ("Invalid IPv4 address length", 0);

    cmsg_sld_configuration_impl_add_server (NULL, &service_info);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_FALSE (cmsg_server_app_owns_current_msg_set_called);
    NP_ASSERT_FALSE (data_add_server_called);
}

/**
 * A local process sending a port longer than a TCP port must not have its
 * server stored.
 */
void
test_cmsg_sld_configuration_impl_add_server_tcp_port_too_long (void)
{
    cmsg_service_info service_info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr)] = { };
    uint8_t port[sizeof (in_port_t) * 2] = { };

    init_tcp_transport_info (&transport_info, &tcp_info,
                             addr, sizeof (addr), port, sizeof (port));
    CMSG_SET_FIELD_PTR (&service_info, service, "test_service");
    CMSG_SET_FIELD_PTR (&service_info, server_info, &transport_info);

    np_syslog_match ("Invalid TCP port length", 0);

    cmsg_sld_configuration_impl_add_server (NULL, &service_info);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_FALSE (cmsg_server_app_owns_current_msg_set_called);
    NP_ASSERT_FALSE (data_add_server_called);
}

void
test_cmsg_sld_configuration_impl_remove_server_tcp (void)
{
    cmsg_service_info service_info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr)] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    init_tcp_transport_info (&transport_info, &tcp_info,
                             addr, sizeof (addr), port, sizeof (port));
    CMSG_SET_FIELD_PTR (&service_info, service, "test_service");
    CMSG_SET_FIELD_PTR (&service_info, server_info, &transport_info);

    cmsg_sld_configuration_impl_remove_server (NULL, &service_info);

    NP_ASSERT_FALSE (cmsg_server_app_owns_current_msg_set_called);
    NP_ASSERT_TRUE (data_remove_server_called);
    NP_ASSERT_FALSE (data_add_server_called);
}

void
test_cmsg_sld_configuration_impl_remove_server_unix (void)
{
    cmsg_service_info service_info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_unix_transport_info unix_info = CMSG_UNIX_TRANSPORT_INFO_INIT;

    init_unix_transport_info (&transport_info, &unix_info, "/tmp/test");
    CMSG_SET_FIELD_PTR (&service_info, service, "test_service");
    CMSG_SET_FIELD_PTR (&service_info, server_info, &transport_info);

    cmsg_sld_configuration_impl_remove_server (NULL, &service_info);

    NP_ASSERT_FALSE (cmsg_server_app_owns_current_msg_set_called);
    NP_ASSERT_TRUE (data_remove_server_called);
    NP_ASSERT_FALSE (data_add_server_called);
}

/**
 * A message with no server information at all must be dropped rather than
 * dereferenced. Every field of 'cmsg_service_info' is optional, so a local
 * process can simply leave the server information out.
 */
void
test_cmsg_sld_configuration_impl_add_server_no_server_info (void)
{
    cmsg_service_info service_info = CMSG_SERVICE_INFO_INIT;

    CMSG_SET_FIELD_PTR (&service_info, service, "test_service");

    np_syslog_match ("Missing transport info", 0);

    cmsg_sld_configuration_impl_add_server (NULL, &service_info);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_FALSE (cmsg_server_app_owns_current_msg_set_called);
    NP_ASSERT_FALSE (data_add_server_called);
}

/**
 * The service name keys the server database so a message that leaves it out
 * must be dropped.
 */
void
test_cmsg_sld_configuration_impl_add_server_no_service (void)
{
    cmsg_service_info service_info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_unix_transport_info unix_info = CMSG_UNIX_TRANSPORT_INFO_INIT;

    init_unix_transport_info (&transport_info, &unix_info, "/tmp/test");
    CMSG_SET_FIELD_PTR (&service_info, server_info, &transport_info);

    np_syslog_match ("Missing service key", 0);

    cmsg_sld_configuration_impl_add_server (NULL, &service_info);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_FALSE (cmsg_server_app_owns_current_msg_set_called);
    NP_ASSERT_FALSE (data_add_server_called);
}

/**
 * A UNIX server with no path must not be stored. 'data_debug_unix_server_dump'
 * would dereference the missing path when the database is dumped.
 */
void
test_cmsg_sld_configuration_impl_add_server_unix_no_path (void)
{
    cmsg_service_info service_info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_unix_transport_info unix_info = CMSG_UNIX_TRANSPORT_INFO_INIT;

    init_unix_transport_info (&transport_info, &unix_info, NULL);
    CMSG_SET_FIELD_PTR (&service_info, service, "test_service");
    CMSG_SET_FIELD_PTR (&service_info, server_info, &transport_info);

    np_syslog_match ("Missing UNIX path", 0);

    cmsg_sld_configuration_impl_add_server (NULL, &service_info);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_FALSE (cmsg_server_app_owns_current_msg_set_called);
    NP_ASSERT_FALSE (data_add_server_called);
}

/**
 * Removing is as dangerous as adding: the incoming address is compared against
 * every stored server for the service using the length of the stored entry, so
 * an address shorter than that is read out of bounds. Such a message must be
 * dropped before it reaches 'data_remove_server'.
 */
void
test_cmsg_sld_configuration_impl_remove_server_tcp_addr_too_short (void)
{
    cmsg_service_info service_info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr) - 1] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    init_tcp_transport_info (&transport_info, &tcp_info,
                             addr, sizeof (addr), port, sizeof (port));
    CMSG_SET_FIELD_PTR (&service_info, service, "test_service");
    CMSG_SET_FIELD_PTR (&service_info, server_info, &transport_info);

    np_syslog_match ("Invalid IPv4 address length", 0);

    cmsg_sld_configuration_impl_remove_server (NULL, &service_info);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_FALSE (data_remove_server_called);
}

/**
 * A zero length port on the remove path must be dropped for the same reason.
 */
void
test_cmsg_sld_configuration_impl_remove_server_tcp_port_len_zero (void)
{
    cmsg_service_info service_info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr)] = { };

    init_tcp_transport_info (&transport_info, &tcp_info, addr, sizeof (addr), NULL, 0);
    CMSG_SET_FIELD_PTR (&service_info, service, "test_service");
    CMSG_SET_FIELD_PTR (&service_info, server_info, &transport_info);

    np_syslog_match ("Invalid TCP port length", 0);

    cmsg_sld_configuration_impl_remove_server (NULL, &service_info);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_FALSE (data_remove_server_called);
}

/**
 * A remove message with no server information at all must be dropped rather
 * than dereferenced.
 */
void
test_cmsg_sld_configuration_impl_remove_server_no_server_info (void)
{
    cmsg_service_info service_info = CMSG_SERVICE_INFO_INIT;

    CMSG_SET_FIELD_PTR (&service_info, service, "test_service");

    np_syslog_match ("Missing transport info", 0);

    cmsg_sld_configuration_impl_remove_server (NULL, &service_info);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_FALSE (data_remove_server_called);
}

/**
 * A remove message with no service name must be dropped for the same reason
 * as the add message.
 */
void
test_cmsg_sld_configuration_impl_remove_server_no_service (void)
{
    cmsg_service_info service_info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_unix_transport_info unix_info = CMSG_UNIX_TRANSPORT_INFO_INIT;

    init_unix_transport_info (&transport_info, &unix_info, "/tmp/test");
    CMSG_SET_FIELD_PTR (&service_info, server_info, &transport_info);

    np_syslog_match ("Missing service key", 0);

    cmsg_sld_configuration_impl_remove_server (NULL, &service_info);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_FALSE (data_remove_server_called);
}

/**
 * Baseline for the listener tests: a well formed listener is registered.
 */
void
test_cmsg_sld_configuration_impl_listen (void)
{
    cmsg_sld_listener_info listener_info = CMSG_SLD_LISTENER_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_unix_transport_info unix_info = CMSG_UNIX_TRANSPORT_INFO_INIT;

    init_unix_transport_info (&transport_info, &unix_info, "/tmp/test");
    CMSG_SET_FIELD_PTR (&listener_info, service, "test_service");
    CMSG_SET_FIELD_PTR (&listener_info, transport_info, &transport_info);

    cmsg_sld_configuration_impl_listen (NULL, &listener_info);

    NP_ASSERT_TRUE (data_add_listener_called);
}

/**
 * A listener claiming an address longer than the buffer it sent must be
 * rejected. 'data_add_listener' builds a transport from the message, copying
 * the claimed number of bytes into a fixed size socket address.
 */
void
test_cmsg_sld_configuration_impl_listen_addr_too_long (void)
{
    cmsg_sld_listener_info listener_info = CMSG_SLD_LISTENER_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr)] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    init_tcp_transport_info (&transport_info, &tcp_info,
                             addr, UINT32_MAX, port, sizeof (port));
    CMSG_SET_FIELD_PTR (&listener_info, service, "test_service");
    CMSG_SET_FIELD_PTR (&listener_info, transport_info, &transport_info);

    np_syslog_match ("Invalid IPv4 address length", 0);

    cmsg_sld_configuration_impl_listen (NULL, &listener_info);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_FALSE (data_add_listener_called);
}

/**
 * A listener with no transport information at all must be dropped rather than
 * dereferenced.
 */
void
test_cmsg_sld_configuration_impl_listen_no_transport_info (void)
{
    cmsg_sld_listener_info listener_info = CMSG_SLD_LISTENER_INFO_INIT;

    CMSG_SET_FIELD_PTR (&listener_info, service, "test_service");

    np_syslog_match ("Missing transport info", 0);

    cmsg_sld_configuration_impl_listen (NULL, &listener_info);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_FALSE (data_add_listener_called);
}

/**
 * A listener with no service name must be dropped. The name selects the
 * service whose events the listener is registered against.
 */
void
test_cmsg_sld_configuration_impl_listen_no_service (void)
{
    cmsg_sld_listener_info listener_info = CMSG_SLD_LISTENER_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_unix_transport_info unix_info = CMSG_UNIX_TRANSPORT_INFO_INIT;

    init_unix_transport_info (&transport_info, &unix_info, "/tmp/test");
    CMSG_SET_FIELD_PTR (&listener_info, transport_info, &transport_info);

    np_syslog_match ("Missing service key", 0);

    cmsg_sld_configuration_impl_listen (NULL, &listener_info);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_FALSE (data_add_listener_called);
}

/**
 * A listener using a transport type this daemon does not support is ignored
 * rather than logged, matching how unsupported servers are handled.
 */
void
test_cmsg_sld_configuration_impl_listen_unsupported_type (void)
{
    cmsg_sld_listener_info listener_info = CMSG_SLD_LISTENER_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;

    CMSG_SET_FIELD_VALUE (&transport_info, type, CMSG_TRANSPORT_INFO_TYPE_NOT_SET);
    CMSG_SET_FIELD_PTR (&listener_info, service, "test_service");
    CMSG_SET_FIELD_PTR (&listener_info, transport_info, &transport_info);

    cmsg_sld_configuration_impl_listen (NULL, &listener_info);

    NP_ASSERT_FALSE (data_add_listener_called);
}

/**
 * Unlistening compares the supplied transport against the stored listeners
 * using the length the peer claimed, so a bad length must be dropped before it
 * reaches 'data_remove_listener'.
 */
void
test_cmsg_sld_configuration_impl_unlisten_addr_too_long (void)
{
    cmsg_sld_listener_info listener_info = CMSG_SLD_LISTENER_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr)] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    init_tcp_transport_info (&transport_info, &tcp_info,
                             addr, UINT32_MAX, port, sizeof (port));
    CMSG_SET_FIELD_PTR (&listener_info, service, "test_service");
    CMSG_SET_FIELD_PTR (&listener_info, transport_info, &transport_info);

    np_syslog_match ("Invalid IPv4 address length", 0);

    cmsg_sld_configuration_impl_unlisten (NULL, &listener_info);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_FALSE (data_remove_listener_called);
}

/**
 * An unlisten message with no transport information at all must be dropped
 * rather than dereferenced.
 */
void
test_cmsg_sld_configuration_impl_unlisten_no_transport_info (void)
{
    cmsg_sld_listener_info listener_info = CMSG_SLD_LISTENER_INFO_INIT;

    CMSG_SET_FIELD_PTR (&listener_info, service, "test_service");

    np_syslog_match ("Missing transport info", 0);

    cmsg_sld_configuration_impl_unlisten (NULL, &listener_info);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_FALSE (data_remove_listener_called);
}

/**
 * An unlisten message with no service name must be dropped for the same
 * reason as the listen message.
 */
void
test_cmsg_sld_configuration_impl_unlisten_no_service (void)
{
    cmsg_sld_listener_info listener_info = CMSG_SLD_LISTENER_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_unix_transport_info unix_info = CMSG_UNIX_TRANSPORT_INFO_INIT;

    init_unix_transport_info (&transport_info, &unix_info, "/tmp/test");
    CMSG_SET_FIELD_PTR (&listener_info, transport_info, &transport_info);

    np_syslog_match ("Missing service key", 0);

    cmsg_sld_configuration_impl_unlisten (NULL, &listener_info);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_FALSE (data_remove_listener_called);
}
