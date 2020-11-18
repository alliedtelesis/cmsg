/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#include "cmsg_private.h"
#include "cmsg_transport.h"
#include "cmsg_transport_private.h"
#include "cmsg_error.h"


/* Forward function declaration required for tipc connect function */
static int32_t
cmsg_transport_tipc_client_send (cmsg_transport *transport, void *buff, int length,
                                 int flag);

/**
 * Create a TIPC socket connection.
 * Returns 0 on success or a negative integer on failure.
 */
static int32_t
cmsg_transport_tipc_connect (cmsg_transport *transport)
{
    int ret;
    int tipc_timeout = transport->connect_timeout * 1000;   /* Timeout must be specified in milliseconds */

    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] cmsg_transport_tipc_connect\n");

    transport->socket = socket (transport->config.socket.family, SOCK_STREAM, 0);

    if (transport->socket < 0)
    {
        ret = -errno;
        CMSG_LOG_TRANSPORT_ERROR (transport, "Unable to create socket. Error:%s",
                                  strerror (errno));

        return ret;
    }

    setsockopt (transport->socket, SOL_TIPC,
                TIPC_CONN_TIMEOUT, &tipc_timeout, sizeof (int));

    ret = connect (transport->socket,
                   (struct sockaddr *) &transport->config.socket.sockaddr.tipc,
                   sizeof (transport->config.socket.sockaddr.tipc));
    if (ret < 0)
    {
        ret = -errno;
        CMSG_LOG_DEBUG ("[TRANSPORT] error connecting to remote host (port %d inst %d): %s",
                        transport->config.socket.sockaddr.tipc.addr.name.name.type,
                        transport->config.socket.sockaddr.tipc.addr.name.name.instance,
                        strerror (errno));

        shutdown (transport->socket, SHUT_RDWR);
        close (transport->socket);
        transport->socket = -1;

        return ret;
    }

    /* TIPC has changed stream sockets to do implied connection - where the
     * connection isn't actually on the connect call if the port is not in
     * the name table (i.e. hasn't been started).  In this case the connect
     * call succeeds but the next send will fail with EPIPE.  So by sending
     * a test packet we can ensure the connection is opened (if possible)
     * thereby ensuring that the connection is valid once this fn returns.
     */
    cmsg_header header = cmsg_header_create (CMSG_MSG_TYPE_CONN_OPEN,
                                             0, 0,
                                             CMSG_STATUS_CODE_UNSET);

    ret = cmsg_transport_tipc_client_send (transport, (void *) &header,
                                           sizeof (header), MSG_NOSIGNAL);

    /* Sending in this case should only fail if the server is not present - so
     * return without printing an error.
     */
    if (ret < (int) sizeof (header))
    {
        CMSG_LOG_DEBUG
            ("[TRANSPORT] error connecting (send) to remote host (port %d inst %d): ret %d %s",
             transport->config.socket.sockaddr.tipc.addr.name.name.type,
             transport->config.socket.sockaddr.tipc.addr.name.name.instance, ret,
             strerror (errno));
        shutdown (transport->socket, SHUT_RDWR);
        close (transport->socket);
        transport->socket = -1;
        return -1;
    }

    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] successfully connected\n");
    return 0;
}



static int32_t
cmsg_transport_tipc_listen (cmsg_transport *transport)
{
    int32_t yes = 1;    // for setsockopt() SO_REUSEADDR, below
    int32_t listening_socket = -1;
    int32_t ret = 0;
    socklen_t addrlen = 0;

    listening_socket = socket (transport->config.socket.family, SOCK_STREAM, 0);
    if (listening_socket == -1)
    {
        CMSG_LOG_TRANSPORT_ERROR (transport, "Socket failed. Error:%s", strerror (errno));
        return -1;
    }

    ret = setsockopt (listening_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof (int32_t));
    if (ret == -1)
    {
        CMSG_LOG_TRANSPORT_ERROR (transport, "Setsockopt failed. Error:%s",
                                  strerror (errno));
        close (listening_socket);
        return -1;
    }

    addrlen = sizeof (transport->config.socket.sockaddr.tipc);

    ret = bind (listening_socket,
                (struct sockaddr *) &transport->config.socket.sockaddr.tipc, addrlen);
    if (ret < 0)
    {
        CMSG_LOG_TRANSPORT_ERROR (transport, "Bind failed. Error:%s", strerror (errno));
        close (listening_socket);
        return -1;
    }

    ret = listen (listening_socket, 10);
    if (ret < 0)
    {
        CMSG_LOG_TRANSPORT_ERROR (transport, "Listen failed. Error:%s", strerror (errno));
        close (listening_socket);
        return -1;
    }

    transport->socket = listening_socket;

    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] listening on tipc socket: %d\n", listening_socket);

    CMSG_DEBUG (CMSG_INFO,
                "[TRANSPORT] listening on tipc type: %d\n",
                transport->config.socket.sockaddr.tipc.addr.name.name.type);

    CMSG_DEBUG (CMSG_INFO,
                "[TRANSPORT] listening on tipc instance: %d\n",
                transport->config.socket.sockaddr.tipc.addr.name.name.instance);

    CMSG_DEBUG (CMSG_INFO,
                "[TRANSPORT] listening on tipc domain: %d\n",
                transport->config.socket.sockaddr.tipc.addr.name.domain);

    CMSG_DEBUG (CMSG_INFO,
                "[TRANSPORT] listening on tipc scope: %d\n",
                transport->config.socket.sockaddr.tipc.scope);

    return 0;
}


/* Wrapper function to call "recv" on a TIPC socket */
int
cmsg_transport_tipc_recv (cmsg_transport *transport, int sock, void *buff, int len,
                          int flags)
{
    struct timeval timeout = { 1, 0 };
    fd_set read_fds;
    int maxfd;

    FD_ZERO (&read_fds);
    FD_SET (sock, &read_fds);
    maxfd = sock;

    /* Do select() on the socket to prevent it to go to usleep instantaneously in the loop
     * if the data is not yet available.*/
    select (maxfd + 1, &read_fds, NULL, NULL, &timeout);

    return cmsg_transport_socket_recv (sock, buff, len, flags);
}


static int32_t
cmsg_transport_tipc_server_accept (cmsg_transport *transport)
{
    uint32_t client_len;
    cmsg_transport client_transport;
    int sock;
    int listen_socket = transport->socket;

    if (listen_socket < 0)
    {
        return -1;
    }

    client_len = sizeof (client_transport.config.socket.sockaddr.tipc);
    sock = accept (listen_socket,
                   (struct sockaddr *) &client_transport.config.socket.sockaddr.tipc,
                   &client_len);

    if (sock < 0)
    {
        CMSG_LOG_TRANSPORT_ERROR (transport, "Accept failed. Error:%s", strerror (errno));
        CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] sock = %d\n", sock);

        return -1;
    }

    return sock;
}


static cmsg_status_code
cmsg_transport_tipc_client_recv (cmsg_transport *transport,
                                 const ProtobufCServiceDescriptor *descriptor,
                                 ProtobufCMessage **messagePtPt)
{
    return cmsg_transport_client_recv (transport, descriptor, messagePtPt);
}


static int32_t
cmsg_transport_tipc_client_send (cmsg_transport *transport, void *buff, int length,
                                 int flag)
{
    return (cmsg_transport_socket_send (transport->socket, buff, length, flag));
}

static void
_cmsg_transport_tipc_init_common (cmsg_transport *transport)
{
    transport->config.socket.family = PF_TIPC;
    transport->config.socket.sockaddr.generic.sa_family = PF_TIPC;
    transport->tport_funcs.recv_wrapper = cmsg_transport_tipc_recv;
    transport->tport_funcs.connect = cmsg_transport_tipc_connect;
    transport->tport_funcs.listen = cmsg_transport_tipc_listen;
    transport->tport_funcs.server_accept = cmsg_transport_tipc_server_accept;
    transport->tport_funcs.server_recv = cmsg_transport_server_recv;
    transport->tport_funcs.client_recv = cmsg_transport_tipc_client_recv;
    transport->tport_funcs.client_send = cmsg_transport_tipc_client_send;
    transport->tport_funcs.socket_close = cmsg_transport_socket_close;
    transport->tport_funcs.get_socket = cmsg_transport_get_socket;
    transport->tport_funcs.destroy = NULL;
    transport->tport_funcs.apply_send_timeout = cmsg_transport_apply_send_timeout;
    transport->tport_funcs.apply_recv_timeout = cmsg_transport_apply_recv_timeout;
}

void
cmsg_transport_tipc_init (cmsg_transport *transport)
{
    if (transport == NULL)
    {
        return;
    }

    _cmsg_transport_tipc_init_common (transport);

    transport->tport_funcs.server_send = cmsg_transport_rpc_server_send;

    CMSG_DEBUG (CMSG_INFO, "%s: done\n", __FUNCTION__);
}

void
cmsg_transport_oneway_tipc_init (cmsg_transport *transport)
{
    if (transport == NULL)
    {
        return;
    }

    _cmsg_transport_tipc_init_common (transport);

    transport->tport_funcs.server_send = cmsg_transport_oneway_server_send;

    CMSG_DEBUG (CMSG_INFO, "%s: done\n", __FUNCTION__);
}

cmsg_transport *
cmsg_create_transport_tipc (const char *server_name, int member_id, int scope,
                            cmsg_transport_type transport_type)
{
    uint32_t port = 0;
    cmsg_transport *transport = NULL;

    port = cmsg_service_port_get (server_name, "tipc");
    if (port <= 0)
    {
        CMSG_LOG_GEN_ERROR ("Unknown TIPC service. Server:%s, MemberID:%d", server_name,
                            member_id);
        return NULL;
    }

    transport = cmsg_transport_new (transport_type);
    if (transport == NULL)
    {
        CMSG_LOG_GEN_ERROR ("Unable to create TIPC transport. Server:%s, MemberID:%d",
                            server_name, member_id);
        return NULL;
    }

    transport->config.socket.family = AF_TIPC;
    transport->config.socket.sockaddr.tipc.family = AF_TIPC;
    transport->config.socket.sockaddr.tipc.addrtype = TIPC_ADDR_NAME;
    transport->config.socket.sockaddr.tipc.addr.name.domain = 0;
    transport->config.socket.sockaddr.tipc.addr.name.name.type = port;
    transport->config.socket.sockaddr.tipc.addr.name.name.instance = member_id;
    transport->config.socket.sockaddr.tipc.scope = scope;

    return transport;
}

cmsg_transport *
cmsg_create_transport_tipc_rpc (const char *server_name, int member_id, int scope)
{
    return cmsg_create_transport_tipc (server_name, member_id, scope,
                                       CMSG_TRANSPORT_RPC_TIPC);
}

cmsg_transport *
cmsg_create_transport_tipc_oneway (const char *server_name, int member_id, int scope)
{
    return cmsg_create_transport_tipc (server_name, member_id, scope,
                                       CMSG_TRANSPORT_ONEWAY_TIPC);
}
