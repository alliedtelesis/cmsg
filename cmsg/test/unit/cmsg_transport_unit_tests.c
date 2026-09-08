/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Unit tests for cmsg_transport.c
 *
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */

#include <np.h>
#include <stdint.h>
#include <cmsg_transport.h>
#include "transport/cmsg_transport_private.h"

static void
init_transport_tcp (cmsg_transport *transport)
{
    transport->config.socket.sockaddr.in.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
    transport->config.socket.sockaddr.in.sin_port = htons ((unsigned short) 10);
}

void
test_cmsg_transport_compare__tcp (void)
{
    cmsg_transport *one = cmsg_transport_new (CMSG_TRANSPORT_RPC_TCP);
    cmsg_transport *two = cmsg_transport_new (CMSG_TRANSPORT_RPC_TCP);

    init_transport_tcp (one);
    init_transport_tcp (two);
    NP_ASSERT_TRUE (cmsg_transport_compare (one, two));

    one->config.socket.sockaddr.in.sin_addr.s_addr = htonl (INADDR_LOOPBACK + 1);
    NP_ASSERT_FALSE (cmsg_transport_compare (one, two));

    init_transport_tcp (one);
    one->config.socket.sockaddr.in.sin_port = htons ((unsigned short) 11);
    NP_ASSERT_FALSE (cmsg_transport_compare (one, two));

    cmsg_transport_destroy (one);
    cmsg_transport_destroy (two);
}

void
test_cmsg_transport_compare__different_types (void)
{
    cmsg_transport *one = cmsg_transport_new (CMSG_TRANSPORT_RPC_TCP);
    cmsg_transport *two = cmsg_transport_new (CMSG_TRANSPORT_RPC_TCP);

    init_transport_tcp (one);
    init_transport_tcp (two);
    NP_ASSERT_TRUE (cmsg_transport_compare (one, two));

    one->type = CMSG_TRANSPORT_LOOPBACK;
    NP_ASSERT_FALSE (cmsg_transport_compare (one, two));

    cmsg_transport_destroy (one);
    cmsg_transport_destroy (two);
}

static void
init_transport_unix (cmsg_transport *transport)
{
    transport->config.socket.family = AF_UNIX;
    transport->config.socket.sockaddr.un.sun_family = AF_UNIX;
    strncpy (transport->config.socket.sockaddr.un.sun_path, "test",
             sizeof (transport->config.socket.sockaddr.un.sun_path) - 1);
}

void
test_cmsg_transport_compare__unix (void)
{
    cmsg_transport *one = cmsg_transport_new (CMSG_TRANSPORT_RPC_UNIX);
    cmsg_transport *two = cmsg_transport_new (CMSG_TRANSPORT_RPC_UNIX);

    init_transport_unix (one);
    init_transport_unix (two);

    NP_ASSERT_TRUE (cmsg_transport_compare (one, two));

    one->config.socket.family = AF_INET;
    NP_ASSERT_FALSE (cmsg_transport_compare (one, two));

    init_transport_unix (one);
    one->config.socket.sockaddr.un.sun_family = AF_INET;
    NP_ASSERT_FALSE (cmsg_transport_compare (one, two));

    init_transport_unix (one);
    strncpy (one->config.socket.sockaddr.un.sun_path, "test2",
             sizeof (one->config.socket.sockaddr.un.sun_path) - 1);
    NP_ASSERT_FALSE (cmsg_transport_compare (one, two));

    cmsg_transport_destroy (one);
    cmsg_transport_destroy (two);
}

static void
init_transport_udp (cmsg_transport *transport)
{
    transport->config.socket.family = PF_INET;
    transport->config.socket.sockaddr.un.sun_family = AF_INET;
    transport->config.socket.sockaddr.in.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
    transport->config.socket.sockaddr.in.sin_port = htons ((unsigned short) 10);
}

void
test_cmsg_transport_compare__udp (void)
{
    cmsg_transport *one = cmsg_transport_new (CMSG_TRANSPORT_ONEWAY_MC_UDP);
    cmsg_transport *two = cmsg_transport_new (CMSG_TRANSPORT_ONEWAY_MC_UDP);

    init_transport_udp (one);
    init_transport_udp (two);
    NP_ASSERT_TRUE (cmsg_transport_compare (one, two));

    one->config.socket.sockaddr.in.sin_addr.s_addr = htonl (INADDR_LOOPBACK + 1);
    NP_ASSERT_FALSE (cmsg_transport_compare (one, two));

    init_transport_udp (one);
    one->config.socket.sockaddr.in.sin_port = htons ((unsigned short) 11);
    NP_ASSERT_FALSE (cmsg_transport_compare (one, two));

    cmsg_transport_destroy (one);
    cmsg_transport_destroy (two);
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
 * Baseline for the 'cmsg_transport_info_validate' length tests: a TCP server with
 * a 4 byte address and a 2 byte port is the only shape a well behaved peer
 * sends, so it must be accepted.
 */
void
test_cmsg_transport_info_validate_ipv4 (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr)] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    init_tcp_transport_info (&transport_info, &tcp_info, true,
                             addr, sizeof (addr), port, sizeof (port));

    NP_ASSERT_TRUE (cmsg_transport_info_validate ("test_service", &transport_info));
}

/**
 * An address only one byte longer than an IPv4 address must be rejected, i.e.
 * the check is an exact length match rather than an upper bound.
 */
void
test_cmsg_transport_info_validate_addr_one_byte_too_long (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr) + 1] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    init_tcp_transport_info (&transport_info, &tcp_info, true,
                             addr, sizeof (addr), port, sizeof (port));

    np_syslog_match ("test_service: Invalid IPv4 address length", 0);

    NP_ASSERT_FALSE (cmsg_transport_info_validate ("test_service", &transport_info));
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);
}

/**
 * An address shorter than an IPv4 address must be rejected. The lookup
 * functions compare a fixed 4 bytes, so a shorter buffer would be over read.
 */
void
test_cmsg_transport_info_validate_addr_too_short (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr) - 1] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    init_tcp_transport_info (&transport_info, &tcp_info, true,
                             addr, sizeof (addr), port, sizeof (port));

    np_syslog_match ("test_service: Invalid IPv4 address length", 0);

    NP_ASSERT_FALSE (cmsg_transport_info_validate ("test_service", &transport_info));
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);
}

/**
 * A zero length address must be rejected.
 */
void
test_cmsg_transport_info_validate_addr_len_zero (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t port[sizeof (in_port_t)] = { };

    init_tcp_transport_info (&transport_info, &tcp_info, true,
                             NULL, 0, port, sizeof (port));

    np_syslog_match ("test_service: Invalid IPv4 address length", 0);

    NP_ASSERT_FALSE (cmsg_transport_info_validate ("test_service", &transport_info));
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);
}

/**
 * A port longer than a TCP port must be rejected. 'data_debug_tcp_server_dump'
 * copies a fixed 'uint16_t' out of the buffer, so a longer buffer means the
 * stored length no longer describes what is read back.
 */
void
test_cmsg_transport_info_validate_port_too_long (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr)] = { };
    uint8_t port[sizeof (in_port_t) * 2] = { };

    init_tcp_transport_info (&transport_info, &tcp_info, true,
                             addr, sizeof (addr), port, sizeof (port));

    np_syslog_match ("test_service: Invalid TCP port length", 0);

    NP_ASSERT_FALSE (cmsg_transport_info_validate ("test_service", &transport_info));
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);
}

/**
 * A port claiming a length far larger than anything that could have been
 * allocated for it must be rejected.
 */
void
test_cmsg_transport_info_validate_port_len_huge (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr)] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    init_tcp_transport_info (&transport_info, &tcp_info, true,
                             addr, sizeof (addr), port, UINT32_MAX);

    np_syslog_match ("test_service: Invalid TCP port length", 0);

    NP_ASSERT_FALSE (cmsg_transport_info_validate ("test_service", &transport_info));
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);
}

/**
 * A port shorter than a TCP port must be rejected.
 */
void
test_cmsg_transport_info_validate_port_too_short (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr)] = { };
    uint8_t port[sizeof (in_port_t) - 1] = { };

    init_tcp_transport_info (&transport_info, &tcp_info, true,
                             addr, sizeof (addr), port, sizeof (port));

    np_syslog_match ("test_service: Invalid TCP port length", 0);

    NP_ASSERT_FALSE (cmsg_transport_info_validate ("test_service", &transport_info));
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);
}

/**
 * A zero length port must be rejected.
 */
void
test_cmsg_transport_info_validate_port_len_zero (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr)] = { };

    init_tcp_transport_info (&transport_info, &tcp_info, true,
                             addr, sizeof (addr), NULL, 0);

    np_syslog_match ("test_service: Invalid TCP port length", 0);

    NP_ASSERT_FALSE (cmsg_transport_info_validate ("test_service", &transport_info));
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);
}

/**
 * An IPv6 server carries a 16 byte address, which is what 'inet_ntop' and
 * 'cmsg_transport_info_to_transport' read back out of the buffer, so it must be
 * accepted.
 */
void
test_cmsg_transport_info_validate_ipv6 (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in6_addr)] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    init_tcp_transport_info (&transport_info, &tcp_info, false,
                             addr, sizeof (addr), port, sizeof (port));

    NP_ASSERT_TRUE (cmsg_transport_info_validate ("test_service", &transport_info));
}

/**
 * An IPv6 server sending an IPv4 sized address must be rejected. The length is
 * checked against the family the peer claimed, not against either family.
 */
void
test_cmsg_transport_info_validate_ipv6_addr_too_short (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr)] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    init_tcp_transport_info (&transport_info, &tcp_info, false,
                             addr, sizeof (addr), port, sizeof (port));

    np_syslog_match ("test_service: Invalid IPv6 address length", 0);

    NP_ASSERT_FALSE (cmsg_transport_info_validate ("test_service", &transport_info));
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);
}

/**
 * An IPv4 server sending an IPv6 sized address must be rejected, i.e. the
 * IPv4 length check is not relaxed to allow the larger of the two families.
 */
void
test_cmsg_transport_info_validate_ipv4_addr_ipv6_length (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in6_addr)] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    init_tcp_transport_info (&transport_info, &tcp_info, true,
                             addr, sizeof (addr), port, sizeof (port));

    np_syslog_match ("test_service: Invalid IPv4 address length", 0);

    NP_ASSERT_FALSE (cmsg_transport_info_validate ("test_service", &transport_info));
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);
}

/**
 * A message with no transport information at all must be rejected rather than
 * dereferenced. Every field of the message is optional, so a peer can simply
 * leave it out.
 */
void
test_cmsg_transport_info_validate_null (void)
{
    np_syslog_match ("test_service: Missing transport info", 0);

    NP_ASSERT_FALSE (cmsg_transport_info_validate ("test_service", NULL));
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);
}

/**
 * A message with no service key must also be rejected.
 */
void
test_cmsg_transport_info_validate_null_key (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_tcp_transport_info tcp_info = CMSG_TCP_TRANSPORT_INFO_INIT;
    uint8_t addr[sizeof (struct in_addr)] = { };
    uint8_t port[sizeof (in_port_t)] = { };

    init_tcp_transport_info (&transport_info, &tcp_info, true,
                             addr, sizeof (addr), port, sizeof (port));

    np_syslog_match ("Missing service key", 0);

    NP_ASSERT_FALSE (cmsg_transport_info_validate (NULL, &transport_info));
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);
}

/**
 * A peer claiming a TCP transport but leaving the 'tcp_info' side of the oneof
 * unset must be rejected rather than dereferenced.
 */
void
test_cmsg_transport_info_validate_tcp_info_missing (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;

    CMSG_SET_FIELD_VALUE (&transport_info, type, CMSG_TRANSPORT_INFO_TYPE_TCP);

    np_syslog_match ("test_service: Missing TCP info", 0);

    NP_ASSERT_FALSE (cmsg_transport_info_validate ("test_service", &transport_info));
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);
}

/**
 * A peer claiming a UNIX transport but leaving the 'unix_info' side of the
 * oneof unset must be rejected. 'data_debug_unix_server_dump' and
 * 'cmsg_transport_info_compare' both dereference the path unconditionally.
 */
void
test_cmsg_transport_info_validate_unix_info_missing (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;

    CMSG_SET_FIELD_VALUE (&transport_info, type, CMSG_TRANSPORT_INFO_TYPE_UNIX);

    np_syslog_match ("test_service: Missing UNIX path", 0);

    NP_ASSERT_FALSE (cmsg_transport_info_validate ("test_service", &transport_info));
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);
}

/**
 * A UNIX transport with the 'unix_info' set but no path in it must be rejected
 * for the same reason.
 */
void
test_cmsg_transport_info_validate_unix_path_missing (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_unix_transport_info unix_info = CMSG_UNIX_TRANSPORT_INFO_INIT;

    CMSG_SET_FIELD_VALUE (&transport_info, type, CMSG_TRANSPORT_INFO_TYPE_UNIX);
    CMSG_SET_FIELD_ONEOF (&transport_info, unix_info, &unix_info,
                          data, CMSG_TRANSPORT_INFO_DATA_UNIX_INFO);

    np_syslog_match ("test_service: Missing UNIX path", 0);

    NP_ASSERT_FALSE (cmsg_transport_info_validate ("test_service", &transport_info));
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);
}

/**
 * A transport type this daemon cannot use is rejected, and rejected quietly.
 * The type is chosen by the peer, so logging it would be a log flood waiting to
 * happen; NovaProva fails this test if anything is written to the log.
 */
void
test_cmsg_transport_info_validate_unsupported_type (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;

    CMSG_SET_FIELD_VALUE (&transport_info, type, CMSG_TRANSPORT_INFO_TYPE_NOT_SET);

    NP_ASSERT_FALSE (cmsg_transport_info_validate ("test_service", &transport_info));
}

/**
 * The length checks only apply to TCP servers. A UNIX server has no address or
 * port buffers to validate, so it must be accepted.
 */
void
test_cmsg_transport_info_validate_unix (void)
{
    cmsg_transport_info transport_info = CMSG_TRANSPORT_INFO_INIT;
    cmsg_unix_transport_info unix_info = CMSG_UNIX_TRANSPORT_INFO_INIT;
    char path[] = "/tmp/test_service";

    CMSG_SET_FIELD_VALUE (&transport_info, type, CMSG_TRANSPORT_INFO_TYPE_UNIX);
    CMSG_SET_FIELD_PTR (&unix_info, path, path);
    CMSG_SET_FIELD_ONEOF (&transport_info, unix_info, &unix_info,
                          data, CMSG_TRANSPORT_INFO_DATA_UNIX_INFO);

    NP_ASSERT_TRUE (cmsg_transport_info_validate ("test_service", &transport_info));
}
