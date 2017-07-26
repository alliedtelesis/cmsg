#include <np.h>
#include <cmsg_pub.h>

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
init_transport_tipc (cmsg_transport *transport)
{
    transport->config.socket.family = AF_TIPC;
    transport->config.socket.sockaddr.tipc.family = AF_TIPC;
    transport->config.socket.sockaddr.tipc.addrtype = TIPC_ADDR_NAME;
    transport->config.socket.sockaddr.tipc.addr.name.domain = 0;
    transport->config.socket.sockaddr.tipc.addr.name.name.type = 10;
    transport->config.socket.sockaddr.tipc.addr.name.name.instance = 10;
    transport->config.socket.sockaddr.tipc.scope = TIPC_NODE_SCOPE;
}

void
test_cmsg_transport_compare__tipc (void)
{
    cmsg_transport *one = cmsg_transport_new (CMSG_TRANSPORT_RPC_TIPC);
    cmsg_transport *two = cmsg_transport_new (CMSG_TRANSPORT_RPC_TIPC);

    init_transport_tipc (one);
    init_transport_tipc (two);
    NP_ASSERT_TRUE (cmsg_transport_compare (one, two));

    one->config.socket.family = AF_UNIX;
    NP_ASSERT_FALSE (cmsg_transport_compare (one, two));

    init_transport_tipc (one);
    one->config.socket.sockaddr.tipc.family = AF_UNIX;
    NP_ASSERT_FALSE (cmsg_transport_compare (one, two));

    init_transport_tipc (one);
    one->config.socket.sockaddr.tipc.addrtype = TIPC_ADDR_NAMESEQ;
    NP_ASSERT_FALSE (cmsg_transport_compare (one, two));

    init_transport_tipc (one);
    one->config.socket.sockaddr.tipc.addr.name.domain = 1;
    NP_ASSERT_FALSE (cmsg_transport_compare (one, two));

    init_transport_tipc (one);
    one->config.socket.sockaddr.tipc.addr.name.name.type = 11;
    NP_ASSERT_FALSE (cmsg_transport_compare (one, two));

    init_transport_tipc (one);
    one->config.socket.sockaddr.tipc.addr.name.name.instance = 11;
    NP_ASSERT_FALSE (cmsg_transport_compare (one, two));

    init_transport_tipc (one);
    one->config.socket.sockaddr.tipc.scope = TIPC_CLUSTER_SCOPE;
    NP_ASSERT_FALSE (cmsg_transport_compare (one, two));

    cmsg_transport_destroy (one);
    cmsg_transport_destroy (two);
}

void
test_cmsg_sub_entry_compare__tcp (void)
{
    cmsg_sub_entry one_sub = { };
    cmsg_sub_entry two_sub = { };
    cmsg_transport *one = cmsg_transport_new (CMSG_TRANSPORT_RPC_TCP);
    cmsg_transport *two = cmsg_transport_new (CMSG_TRANSPORT_RPC_TCP);

    one_sub.transport = one;
    two_sub.transport = two;
    strncpy (one_sub.method_name, "test", sizeof (one_sub.method_name) - 1);
    strncpy (two_sub.method_name, "test", sizeof (two_sub.method_name) - 1);
    one_sub.to_be_removed = 0;
    two_sub.to_be_removed = 0;

    init_transport_tcp (one);
    init_transport_tcp (two);
    NP_ASSERT_EQUAL (cmsg_sub_entry_compare (&one_sub, &two_sub), 0);

    one->config.socket.sockaddr.in.sin_addr.s_addr = htonl (INADDR_LOOPBACK + 1);
    NP_ASSERT_EQUAL (cmsg_sub_entry_compare (&one_sub, &two_sub), -1);

    init_transport_tcp (one);
    one->config.socket.sockaddr.in.sin_port = htons ((unsigned short) 11);
    NP_ASSERT_EQUAL (cmsg_sub_entry_compare (&one_sub, &two_sub), -1);

    cmsg_transport_destroy (one);
    cmsg_transport_destroy (two);
}

void
test_cmsg_sub_entry_compare__different_types (void)
{
    cmsg_sub_entry one_sub = { };
    cmsg_sub_entry two_sub = { };
    cmsg_transport *one = cmsg_transport_new (CMSG_TRANSPORT_RPC_TCP);
    cmsg_transport *two = cmsg_transport_new (CMSG_TRANSPORT_RPC_TCP);

    one_sub.transport = one;
    two_sub.transport = two;
    strncpy (one_sub.method_name, "test", sizeof (one_sub.method_name) - 1);
    strncpy (two_sub.method_name, "test", sizeof (two_sub.method_name) - 1);
    one_sub.to_be_removed = 0;
    two_sub.to_be_removed = 0;

    init_transport_tcp (one);
    init_transport_tcp (two);
    NP_ASSERT_EQUAL (cmsg_sub_entry_compare (&one_sub, &two_sub), 0);

    one->type = CMSG_TRANSPORT_LOOPBACK;
    NP_ASSERT_EQUAL (cmsg_sub_entry_compare (&one_sub, &two_sub), -1);

    cmsg_transport_destroy (one);
    cmsg_transport_destroy (two);
}

void
test_cmsg_sub_entry_compare__tipc (void)
{
    cmsg_sub_entry one_sub = { };
    cmsg_sub_entry two_sub = { };
    cmsg_transport *one = cmsg_transport_new (CMSG_TRANSPORT_RPC_TIPC);
    cmsg_transport *two = cmsg_transport_new (CMSG_TRANSPORT_RPC_TIPC);

    one_sub.transport = one;
    two_sub.transport = two;
    strncpy (one_sub.method_name, "test", sizeof (one_sub.method_name) - 1);
    strncpy (two_sub.method_name, "test", sizeof (two_sub.method_name) - 1);
    one_sub.to_be_removed = 0;
    two_sub.to_be_removed = 0;

    init_transport_tipc (one);
    init_transport_tipc (two);
    NP_ASSERT_EQUAL (cmsg_sub_entry_compare (&one_sub, &two_sub), 0);

    one->config.socket.family = AF_UNIX;
    NP_ASSERT_EQUAL (cmsg_sub_entry_compare (&one_sub, &two_sub), -1);

    init_transport_tipc (one);
    one->config.socket.sockaddr.tipc.family = AF_UNIX;
    NP_ASSERT_EQUAL (cmsg_sub_entry_compare (&one_sub, &two_sub), -1);

    init_transport_tipc (one);
    one->config.socket.sockaddr.tipc.addrtype = TIPC_ADDR_NAMESEQ;
    NP_ASSERT_EQUAL (cmsg_sub_entry_compare (&one_sub, &two_sub), -1);

    init_transport_tipc (one);
    one->config.socket.sockaddr.tipc.addr.name.domain = 1;
    NP_ASSERT_EQUAL (cmsg_sub_entry_compare (&one_sub, &two_sub), -1);

    init_transport_tipc (one);
    one->config.socket.sockaddr.tipc.addr.name.name.type = 11;
    NP_ASSERT_EQUAL (cmsg_sub_entry_compare (&one_sub, &two_sub), -1);

    init_transport_tipc (one);
    one->config.socket.sockaddr.tipc.addr.name.name.instance = 11;
    NP_ASSERT_EQUAL (cmsg_sub_entry_compare (&one_sub, &two_sub), -1);

    init_transport_tipc (one);
    one->config.socket.sockaddr.tipc.scope = TIPC_CLUSTER_SCOPE;
    NP_ASSERT_EQUAL (cmsg_sub_entry_compare (&one_sub, &two_sub), -1);

    cmsg_transport_destroy (one);
    cmsg_transport_destroy (two);
}

void
test_cmsg_sub_entry_compare__different_method_name (void)
{
    cmsg_sub_entry one_sub = { };
    cmsg_sub_entry two_sub = { };
    cmsg_transport *one = cmsg_transport_new (CMSG_TRANSPORT_RPC_TCP);
    cmsg_transport *two = cmsg_transport_new (CMSG_TRANSPORT_RPC_TCP);

    one_sub.transport = one;
    two_sub.transport = two;
    strncpy (one_sub.method_name, "test", sizeof (one_sub.method_name) - 1);
    strncpy (two_sub.method_name, "test", sizeof (two_sub.method_name) - 1);
    one_sub.to_be_removed = 0;
    two_sub.to_be_removed = 0;

    init_transport_tcp (one);
    init_transport_tcp (two);
    NP_ASSERT_EQUAL (cmsg_sub_entry_compare (&one_sub, &two_sub), 0);

    strncpy (two_sub.method_name, "test2", sizeof (two_sub.method_name) - 1);
    NP_ASSERT_EQUAL (cmsg_sub_entry_compare (&one_sub, &two_sub), -1);

    cmsg_transport_destroy (one);
    cmsg_transport_destroy (two);
}

void
test_cmsg_sub_entry_compare__marked_for_deletion (void)
{
    cmsg_sub_entry one_sub = { };
    cmsg_sub_entry two_sub = { };
    cmsg_transport *one = cmsg_transport_new (CMSG_TRANSPORT_RPC_TCP);
    cmsg_transport *two = cmsg_transport_new (CMSG_TRANSPORT_RPC_TCP);

    one_sub.transport = one;
    two_sub.transport = two;
    strncpy (one_sub.method_name, "test", sizeof (one_sub.method_name) - 1);
    strncpy (two_sub.method_name, "test", sizeof (two_sub.method_name) - 1);
    one_sub.to_be_removed = 0;
    two_sub.to_be_removed = 0;

    init_transport_tcp (one);
    init_transport_tcp (two);
    NP_ASSERT_EQUAL (cmsg_sub_entry_compare (&one_sub, &two_sub), 0);

    one_sub.to_be_removed = 1;
    NP_ASSERT_EQUAL (cmsg_sub_entry_compare (&one_sub, &two_sub), -1);

    two_sub.to_be_removed = 1;
    NP_ASSERT_EQUAL (cmsg_sub_entry_compare (&one_sub, &two_sub), -1);

    cmsg_transport_destroy (one);
    cmsg_transport_destroy (two);
}
