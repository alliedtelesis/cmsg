/*
 * Unit tests for cmsg_transport.c
 *
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */

#include <np.h>
#include <cmsg_transport.h>

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
