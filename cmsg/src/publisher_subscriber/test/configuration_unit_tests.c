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
static bool data_add_subscription_called = false;
static bool data_remove_subscription_called = false;
static bool data_remove_subscriber_called = false;
static bool data_add_publisher_called = false;
static bool data_remove_publisher_called = false;

static void
sm_mock_cmsg_server_app_owns_current_msg_set (cmsg_server *server)
{
    cmsg_server_app_owns_current_msg_set_called = true;
}

static void
sm_mock_cmsg_server_send_response (const ProtobufCMessage *send_msg, const void *service)
{
    /* Do nothing. */
}

static bool
sm_mock_data_add_subscription_return_true (const cmsg_subscription_info *info)
{
    data_add_subscription_called = true;
    return true;
}

static bool
sm_mock_data_add_subscription_return_false (const cmsg_subscription_info *info)
{
    data_add_subscription_called = true;
    return false;
}

static void
sm_mock_data_remove_subscription (const cmsg_subscription_info *info)
{
    data_remove_subscription_called = true;
}

static bool
sm_mock_data_remove_subscriber (const char *service,
                                const cmsg_transport_info *transport_info)
{
    data_remove_subscriber_called = true;
    return true;
}

static void
sm_mock_data_add_publisher (const char *service, cmsg_transport_info *transport_info)
{
    data_add_publisher_called = true;
}

static void
sm_mock_data_remove_publisher (const char *service, cmsg_transport_info *transport_info)
{
    data_remove_publisher_called = true;
}

static void
sm_mock_data_get_subscription_info_for_service (const char *service,
                                                cmsg_subscription_methods *msg)
{
    /* Do nothing. */
}

static void
sm_mock_data_get_subscription_info_for_service_free (cmsg_subscription_methods *msg)
{
    /* Do nothing. */
}

static int USED
set_up (void)
{
    cmsg_server_app_owns_current_msg_set_called = false;
    data_add_subscription_called = false;
    data_remove_subscription_called = false;
    data_remove_subscriber_called = false;
    data_add_publisher_called = false;
    data_remove_publisher_called = false;

    np_mock (cmsg_server_app_owns_current_msg_set,
             sm_mock_cmsg_server_app_owns_current_msg_set);
    np_mock (cmsg_server_send_response, sm_mock_cmsg_server_send_response);
    np_mock (data_remove_subscription, sm_mock_data_remove_subscription);
    np_mock (data_remove_subscriber, sm_mock_data_remove_subscriber);
    np_mock (data_add_publisher, sm_mock_data_add_publisher);
    np_mock (data_remove_publisher, sm_mock_data_remove_publisher);
    np_mock (data_get_subscription_info_for_service,
             sm_mock_data_get_subscription_info_for_service);
    np_mock (data_get_subscription_info_for_service_free,
             sm_mock_data_get_subscription_info_for_service_free);

    return 0;
}

/**
 * Build a TCP 'cmsg_transport_info' using the supplied address and port buffer
 * lengths. Other processes and hosts are not trusted to send sane lengths, so
 * tests need to be able to claim any length.
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

/**
 * Build a subscription for a UNIX subscriber that the daemon must accept.
 */
static void
init_valid_subscription (cmsg_subscription_info *info,
                         cmsg_transport_info *transport_info,
                         cmsg_unix_transport_info *unix_info)
{
    init_unix_transport_info (transport_info, unix_info, "/tmp/test");
    CMSG_SET_FIELD_PTR (info, service, "test_service");
    CMSG_SET_FIELD_PTR (info, method_name, "test_method");
    CMSG_SET_FIELD_PTR (info, transport_info, transport_info);
}

/**
 * Build a subscription claiming an address longer than the buffer it sent.
 */
static void
init_bad_length_subscription (cmsg_subscription_info *info,
                              cmsg_transport_info *transport_info,
                              cmsg_tcp_transport_info *tcp_info,
                              uint8_t *addr, uint8_t *port, uint32_t port_len)
{
    init_tcp_transport_info (transport_info, tcp_info, addr, UINT32_MAX, port, port_len);
    CMSG_SET_FIELD_PTR (info, service, "test_service");
    CMSG_SET_FIELD_PTR (info, method_name, "test_method");
    CMSG_SET_FIELD_PTR (info, transport_info, transport_info);
}

/**
 * Build service information claiming an address longer than the buffer it sent.
 */
static void
init_bad_length_service_info (cmsg_service_info *info,
                              cmsg_transport_info *transport_info,
                              cmsg_tcp_transport_info *tcp_info,
                              uint8_t *addr, uint8_t *port, uint32_t port_len)
{
    init_tcp_transport_info (transport_info, tcp_info, addr, UINT32_MAX, port, port_len);
    CMSG_SET_FIELD_PTR (info, service, "test_service");
    CMSG_SET_FIELD_PTR (info, server_info, transport_info);
}

void
test_cmsg_psd_configuration_impl_add_subscription_remote (void)
{
    cmsg_subscription_info info = CMSG_SUBSCRIPTION_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_unix_transport_info unix_info = CMSG_UNIX_TRANSPORT_INFO_INIT;

    init_valid_subscription (&info, &transport_info, &unix_info);

    np_mock (data_add_subscription, sm_mock_data_add_subscription_return_true);
    cmsg_psd_configuration_impl_add_subscription (NULL, &info);
    NP_ASSERT_TRUE (cmsg_server_app_owns_current_msg_set_called);
}

void
test_cmsg_psd_configuration_impl_add_subscription_local (void)
{
    cmsg_subscription_info info = CMSG_SUBSCRIPTION_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_unix_transport_info unix_info = CMSG_UNIX_TRANSPORT_INFO_INIT;

    init_valid_subscription (&info, &transport_info, &unix_info);

    np_mock (data_add_subscription, sm_mock_data_add_subscription_return_false);
    cmsg_psd_configuration_impl_add_subscription (NULL, &info);
    NP_ASSERT_FALSE (cmsg_server_app_owns_current_msg_set_called);
}

/**
 * A subscriber claiming an address longer than the buffer it sent must be
 * rejected. The daemon stores the message and later builds a transport from
 * it, copying the claimed number of bytes into a fixed size socket address.
 */
void
test_cmsg_psd_configuration_impl_add_subscription_addr_too_long (void)
{
    cmsg_subscription_info info = CMSG_SUBSCRIPTION_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr)] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    init_bad_length_subscription (&info, &transport_info, &tcp_info,
                                  addr, port, sizeof (port));

    np_mock (data_add_subscription, sm_mock_data_add_subscription_return_true);
    np_syslog_match ("Invalid IPv4 address length", 0);

    cmsg_psd_configuration_impl_add_subscription (NULL, &info);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_FALSE (data_add_subscription_called);
}

/**
 * A subscriber claiming a port longer than the buffer it sent must be rejected.
 */
void
test_cmsg_psd_configuration_impl_add_subscription_port_too_long (void)
{
    cmsg_subscription_info info = CMSG_SUBSCRIPTION_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr)] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    init_tcp_transport_info (&transport_info, &tcp_info,
                             addr, sizeof (addr), port, UINT32_MAX);
    CMSG_SET_FIELD_PTR (&info, service, "test_service");
    CMSG_SET_FIELD_PTR (&info, method_name, "test_method");
    CMSG_SET_FIELD_PTR (&info, transport_info, &transport_info);

    np_mock (data_add_subscription, sm_mock_data_add_subscription_return_true);
    np_syslog_match ("Invalid TCP port length", 0);

    cmsg_psd_configuration_impl_add_subscription (NULL, &info);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_FALSE (data_add_subscription_called);
}

/**
 * A subscription with no transport information at all must be dropped rather
 * than dereferenced.
 */
void
test_cmsg_psd_configuration_impl_add_subscription_no_transport_info (void)
{
    cmsg_subscription_info info = CMSG_SUBSCRIPTION_INFO_INIT;

    CMSG_SET_FIELD_PTR (&info, service, "test_service");
    CMSG_SET_FIELD_PTR (&info, method_name, "test_method");

    np_mock (data_add_subscription, sm_mock_data_add_subscription_return_true);
    np_syslog_match ("Missing transport info", 0);

    cmsg_psd_configuration_impl_add_subscription (NULL, &info);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_FALSE (data_add_subscription_called);
}

/**
 * A subscription with no service name must be dropped. The name is the key the
 * subscription is stored under.
 */
void
test_cmsg_psd_configuration_impl_add_subscription_no_service (void)
{
    cmsg_subscription_info info = CMSG_SUBSCRIPTION_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_unix_transport_info unix_info = CMSG_UNIX_TRANSPORT_INFO_INIT;

    init_unix_transport_info (&transport_info, &unix_info, "/tmp/test");
    CMSG_SET_FIELD_PTR (&info, method_name, "test_method");
    CMSG_SET_FIELD_PTR (&info, transport_info, &transport_info);

    np_mock (data_add_subscription, sm_mock_data_add_subscription_return_true);
    np_syslog_match ("Missing service key", 0);

    cmsg_psd_configuration_impl_add_subscription (NULL, &info);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_FALSE (data_add_subscription_called);
}

/**
 * A subscription with no UNIX path must be dropped. The path is copied into
 * the socket address of the transport built from the message.
 */
void
test_cmsg_psd_configuration_impl_add_subscription_no_unix_path (void)
{
    cmsg_subscription_info info = CMSG_SUBSCRIPTION_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_unix_transport_info unix_info = CMSG_UNIX_TRANSPORT_INFO_INIT;

    init_unix_transport_info (&transport_info, &unix_info, NULL);
    CMSG_SET_FIELD_PTR (&info, service, "test_service");
    CMSG_SET_FIELD_PTR (&info, method_name, "test_method");
    CMSG_SET_FIELD_PTR (&info, transport_info, &transport_info);

    np_mock (data_add_subscription, sm_mock_data_add_subscription_return_true);
    np_syslog_match ("Missing UNIX path", 0);

    cmsg_psd_configuration_impl_add_subscription (NULL, &info);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_FALSE (data_add_subscription_called);
}

/**
 * A subscription using a transport type this daemon does not support is
 * ignored rather than logged, matching how the service listener daemon
 * handles them.
 */
void
test_cmsg_psd_configuration_impl_add_subscription_unsupported_type (void)
{
    cmsg_subscription_info info = CMSG_SUBSCRIPTION_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;

    CMSG_SET_FIELD_VALUE (&transport_info, type, CMSG_TRANSPORT_INFO_TYPE_NOT_SET);
    CMSG_SET_FIELD_PTR (&info, service, "test_service");
    CMSG_SET_FIELD_PTR (&info, method_name, "test_method");
    CMSG_SET_FIELD_PTR (&info, transport_info, &transport_info);

    np_mock (data_add_subscription, sm_mock_data_add_subscription_return_true);

    cmsg_psd_configuration_impl_add_subscription (NULL, &info);

    NP_ASSERT_FALSE (data_add_subscription_called);
}

/**
 * Baseline for removal: a well formed subscription reaches the data layer.
 */
void
test_cmsg_psd_configuration_impl_remove_subscription (void)
{
    cmsg_subscription_info info = CMSG_SUBSCRIPTION_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_unix_transport_info unix_info = CMSG_UNIX_TRANSPORT_INFO_INIT;

    init_valid_subscription (&info, &transport_info, &unix_info);

    cmsg_psd_configuration_impl_remove_subscription (NULL, &info);

    NP_ASSERT_TRUE (data_remove_subscription_called);
}

/**
 * Removal compares the supplied transport against the stored subscriptions, so
 * a bad length must be dropped before it reaches the data layer.
 */
void
test_cmsg_psd_configuration_impl_remove_subscription_addr_too_long (void)
{
    cmsg_subscription_info info = CMSG_SUBSCRIPTION_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr)] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    init_bad_length_subscription (&info, &transport_info, &tcp_info,
                                  addr, port, sizeof (port));

    np_syslog_match ("Invalid IPv4 address length", 0);

    cmsg_psd_configuration_impl_remove_subscription (NULL, &info);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_FALSE (data_remove_subscription_called);
}

/**
 * Baseline for subscriber removal: a well formed message reaches the data
 * layer.
 */
void
test_cmsg_psd_configuration_impl_remove_subscriber (void)
{
    cmsg_service_info info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_unix_transport_info unix_info = CMSG_UNIX_TRANSPORT_INFO_INIT;

    init_unix_transport_info (&transport_info, &unix_info, "/tmp/test");
    CMSG_SET_FIELD_PTR (&info, service, "test_service");
    CMSG_SET_FIELD_PTR (&info, server_info, &transport_info);

    cmsg_psd_configuration_impl_remove_subscriber (NULL, &info);

    NP_ASSERT_TRUE (data_remove_subscriber_called);
}

/**
 * Removing a subscriber compares the supplied transport against every stored
 * subscription, so a bad length must be dropped first.
 */
void
test_cmsg_psd_configuration_impl_remove_subscriber_addr_too_long (void)
{
    cmsg_service_info info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr)] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    init_bad_length_service_info (&info, &transport_info, &tcp_info,
                                  addr, port, sizeof (port));

    np_syslog_match ("Invalid IPv4 address length", 0);

    cmsg_psd_configuration_impl_remove_subscriber (NULL, &info);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_FALSE (data_remove_subscriber_called);
}

/**
 * Baseline for publisher registration: a well formed message reaches the data
 * layer.
 */
void
test_cmsg_psd_configuration_impl_add_publisher (void)
{
    cmsg_service_info info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_unix_transport_info unix_info = CMSG_UNIX_TRANSPORT_INFO_INIT;

    init_unix_transport_info (&transport_info, &unix_info, "/tmp/test");
    CMSG_SET_FIELD_PTR (&info, service, "test_service");
    CMSG_SET_FIELD_PTR (&info, server_info, &transport_info);

    cmsg_psd_configuration_impl_add_publisher (NULL, &info);

    NP_ASSERT_TRUE (data_add_publisher_called);
}

/**
 * A publisher claiming an address longer than the buffer it sent must be
 * rejected. The daemon builds a client to the publisher from the message
 * immediately.
 */
void
test_cmsg_psd_configuration_impl_add_publisher_addr_too_long (void)
{
    cmsg_service_info info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr)] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    init_bad_length_service_info (&info, &transport_info, &tcp_info,
                                  addr, port, sizeof (port));

    np_syslog_match ("Invalid IPv4 address length", 0);

    cmsg_psd_configuration_impl_add_publisher (NULL, &info);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_FALSE (data_add_publisher_called);
}

/**
 * Baseline for publisher removal: a well formed message reaches the data
 * layer.
 */
void
test_cmsg_psd_configuration_impl_remove_publisher (void)
{
    cmsg_service_info info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_unix_transport_info unix_info = CMSG_UNIX_TRANSPORT_INFO_INIT;

    init_unix_transport_info (&transport_info, &unix_info, "/tmp/test");
    CMSG_SET_FIELD_PTR (&info, service, "test_service");
    CMSG_SET_FIELD_PTR (&info, server_info, &transport_info);

    cmsg_psd_configuration_impl_remove_publisher (NULL, &info);

    NP_ASSERT_TRUE (data_remove_publisher_called);
}

/**
 * Removing a publisher compares the supplied transport against the registered
 * clients, so a bad length must be dropped first.
 */
void
test_cmsg_psd_configuration_impl_remove_publisher_addr_too_long (void)
{
    cmsg_service_info info = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr)] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    init_bad_length_service_info (&info, &transport_info, &tcp_info,
                                  addr, port, sizeof (port));

    np_syslog_match ("Invalid IPv4 address length", 0);

    cmsg_psd_configuration_impl_remove_publisher (NULL, &info);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);

    NP_ASSERT_FALSE (data_remove_publisher_called);
}
