/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#include "cmsg_private.h"
#include "cmsg_transport.h"
#include "cmsg_transport_private.h"
#include "cmsg_error.h"
#include <arpa/inet.h>
/* Limit the size of message read */
#define CMSG_RECV_ALL_CHUNK_SIZE  (16 * 1024)

/*
 * Create a TCP socket connection.
 * Returns 0 on success or a negative integer on failure.
 */
static int32_t
cmsg_transport_tcp_connect (cmsg_transport *transport, int timeout)
{
    int ret;
    struct sockaddr *addr;
    uint32_t addr_len;

    transport->socket = socket (transport->config.socket.family, SOCK_STREAM, 0);

    if (transport->socket < 0)
    {
        ret = -errno;
        CMSG_LOG_TRANSPORT_ERROR (transport, "Unable to create socket. Error:%s",
                                  strerror (errno));
        return ret;
    }

    if (transport->config.socket.family == PF_INET6)
    {
        addr = (struct sockaddr *) &transport->config.socket.sockaddr.in6;
        addr_len = sizeof (transport->config.socket.sockaddr.in6);
    }
    else
    {
        addr = (struct sockaddr *) &transport->config.socket.sockaddr.in;
        addr_len = sizeof (transport->config.socket.sockaddr.in);
    }

    if (connect (transport->socket, addr, addr_len) < 0)
    {
        if (errno == EINPROGRESS)
        {
            //?
        }

        ret = -errno;
        CMSG_LOG_TRANSPORT_ERROR (transport, "Failed to connect to remote host. Error:%s",
                                  strerror (errno));

        close (transport->socket);
        transport->socket = -1;

        return ret;
    }
    else
    {
        CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] successfully connected\n");
        return 0;
    }
}


static int32_t
cmsg_transport_tcp_listen (cmsg_transport *transport)
{
    int32_t yes = 1;    // for setsockopt() SO_REUSEADDR, below
    int32_t listening_socket = -1;
    int32_t ret = 0;
    socklen_t addrlen = 0;

    listening_socket = socket (transport->config.socket.family, SOCK_STREAM, 0);
    if (listening_socket == -1)
    {
        CMSG_LOG_TRANSPORT_ERROR (transport, "Unable to create socket. Error:%s",
                                  strerror (errno));
        return -1;
    }

    ret = setsockopt (listening_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof (int32_t));
    if (ret == -1)
    {
        CMSG_LOG_TRANSPORT_ERROR (transport, "Unable to setsockopt. Error:%s",
                                  strerror (errno));
        close (listening_socket);
        return -1;
    }

    /* IP_FREEBIND sock opt permits binding to a non-local or non-existent address.
     * This is done here to resolve the race condition with IPv6 DAD. Until DAD can
     * confirm that there is no other host with the same address, the address is
     * considered to be "tentative". While it is in this state, attempts to bind()
     * to the address fail with EADDRNOTAVAIL, as if the address doesn't exist.
     * */
    if (transport->use_ipfree_bind)
    {
        ret =
            setsockopt (listening_socket, IPPROTO_IP, IP_FREEBIND, &yes, sizeof (int32_t));
        if (ret == -1)
        {
            CMSG_LOG_TRANSPORT_ERROR (transport, "Unable to setsockopt. Error:%s",
                                      strerror (errno));
            close (listening_socket);
            return -1;
        }
    }

    if (transport->config.socket.family == PF_INET6)
    {
        addrlen = sizeof (transport->config.socket.sockaddr.in6);
    }
    else
    {
        addrlen = sizeof (transport->config.socket.sockaddr.in);
    }

    ret = bind (listening_socket, &transport->config.socket.sockaddr.generic, addrlen);
    if (ret < 0)
    {
        CMSG_LOG_TRANSPORT_ERROR (transport, "Unable to bind socket. Error:%s",
                                  strerror (errno));
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

    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] listening on tcp socket: %d\n", listening_socket);

#ifndef DEBUG_DISABLED
    int port = 0;

    if (transport->config.socket.family == PF_INET6)
    {
        port = (int) (ntohs (transport->config.socket.sockaddr.in6.sin6_port));
    }
    else
    {
        port = (int) (ntohs (transport->config.socket.sockaddr.in.sin_port));
    }

    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] listening on port: %d\n", port);
#endif /* !DEBUG_DISABLED */

    return 0;
}

/**
 *  MSG_WAITALL will cause the recv call to block until all data has been
 *  received, but it appears that in cases where the size of the data to
 *  be received is close to or larger than the size of sockets receive
 *  buffer it may be possible for the TCP connection to deadlock as the
 *  receiver is waiting for the sender to send more, and the sender is
 *  waiting for the receiver to receive more.
 *
 *  To avoid this situation, the data is now read in chunks that are
 *  significantly smaller than the receive buffer.
 */
static ssize_t
recv_all (int sockfd, void *buf, size_t len, int flag)
{
    size_t chunk_size;
    size_t nbytes = 0;
    ssize_t ret;
    ssize_t bytes_left = 0;

    while (nbytes < len)
    {
        /* Do not read past the end of the message */
        bytes_left = len - nbytes;
        chunk_size = MIN (bytes_left, CMSG_RECV_ALL_CHUNK_SIZE);

        ret = recv (sockfd, buf + nbytes, chunk_size, flag);
        if (ret < 0)
        {
            /* error */
            return ret;
        }
        else if (ret == 0)
        {
            /* shutdown */
            return nbytes;
        }
        nbytes += ret;
    }

    return nbytes;
}

/* Wrapper function to call "recv" on a TCP socket */
int
cmsg_transport_tcp_recv (cmsg_transport *transport, int sock, void *buff, int len,
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

    return recv_all (sock, buff, len, flags);
}


static int32_t
cmsg_transport_tcp_server_accept (int32_t listen_socket, cmsg_transport *transport)
{
    uint32_t client_len;
    cmsg_transport client_transport;
    int sock;
    struct sockaddr *addr;

    if (listen_socket < 0)
    {
        CMSG_LOG_GEN_ERROR ("TCP server accept error. Invalid arguments.");
        return -1;
    }

    if (transport->config.socket.family == PF_INET6)
    {
        addr = (struct sockaddr *) &client_transport.config.socket.sockaddr.in6;
        client_len = sizeof (client_transport.config.socket.sockaddr.in6);
    }
    else
    {
        addr = (struct sockaddr *) &client_transport.config.socket.sockaddr.in;
        client_len = sizeof (client_transport.config.socket.sockaddr.in);
    }

    sock = accept (listen_socket, addr, &client_len);

    if (sock < 0)
    {
        CMSG_LOG_TRANSPORT_ERROR (transport, "Accept failed. Error:%s", strerror (errno));
        CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] sock = %d\n", sock);

        return -1;
    }

    return sock;
}

static cmsg_status_code
cmsg_transport_tcp_client_recv (cmsg_transport *transport,
                                const ProtobufCServiceDescriptor *descriptor,
                                ProtobufCMessage **messagePtPt)
{
    return cmsg_transport_client_recv (transport, descriptor, messagePtPt);
}


static int32_t
cmsg_transport_tcp_client_send (cmsg_transport *transport, void *buff, int length, int flag)
{
    return (send (transport->socket, buff, length, flag));
}

/**
 * TCP is never congested
 */
bool
cmsg_transport_tcp_is_congested (cmsg_transport *transport)
{
    return false;
}


int32_t
cmsg_transport_tcp_send_can_block_enable (cmsg_transport *transport,
                                          uint32_t send_can_block)
{
    transport->send_can_block = send_can_block;
    return 0;
}


int32_t
cmsg_transport_tcp_ipfree_bind_enable (cmsg_transport *transport,
                                       cmsg_bool_t use_ipfree_bind)
{
    transport->use_ipfree_bind = use_ipfree_bind;
    return 0;
}

static void
_cmsg_transport_tcp_init_common (cmsg_tport_functions *tport_funcs)
{
    tport_funcs->recv_wrapper = cmsg_transport_tcp_recv;
    tport_funcs->connect = cmsg_transport_tcp_connect;
    tport_funcs->listen = cmsg_transport_tcp_listen;
    tport_funcs->server_accept = cmsg_transport_tcp_server_accept;
    tport_funcs->server_recv = cmsg_transport_server_recv;
    tport_funcs->client_recv = cmsg_transport_tcp_client_recv;
    tport_funcs->client_send = cmsg_transport_tcp_client_send;
    tport_funcs->socket_close = cmsg_transport_socket_close;
    tport_funcs->get_socket = cmsg_transport_get_socket;
    tport_funcs->is_congested = cmsg_transport_tcp_is_congested;
    tport_funcs->send_can_block_enable = cmsg_transport_tcp_send_can_block_enable;
    tport_funcs->ipfree_bind_enable = cmsg_transport_tcp_ipfree_bind_enable;
    tport_funcs->destroy = NULL;
}


void
cmsg_transport_rpc_tcp_funcs_init (cmsg_tport_functions *tport_funcs)
{
    _cmsg_transport_tcp_init_common (tport_funcs);

    tport_funcs->server_send = cmsg_transport_rpc_server_send;
}


void
cmsg_transport_tcp_init (cmsg_transport *transport)
{
    if (transport == NULL)
    {
        return;
    }

    transport->config.socket.family = PF_INET;
    transport->config.socket.sockaddr.generic.sa_family = PF_INET;

    cmsg_transport_rpc_tcp_funcs_init (&transport->tport_funcs);

    CMSG_DEBUG (CMSG_INFO, "%s: done\n", __FUNCTION__);
}


void
cmsg_transport_oneway_tcp_funcs_init (cmsg_tport_functions *tport_funcs)
{
    _cmsg_transport_tcp_init_common (tport_funcs);

    tport_funcs->server_send = cmsg_transport_oneway_server_send;
}


void
cmsg_transport_oneway_tcp_init (cmsg_transport *transport)
{
    if (transport == NULL)
    {
        return;
    }

    transport->config.socket.family = PF_INET;
    transport->config.socket.sockaddr.generic.sa_family = PF_INET;

    cmsg_transport_oneway_tcp_funcs_init (&transport->tport_funcs);

    CMSG_DEBUG (CMSG_INFO, "%s: done\n", __FUNCTION__);
}

cmsg_transport *
cmsg_create_transport_tcp (cmsg_socket *config, cmsg_transport_type transport_type)
{
    cmsg_transport *transport = NULL;

    transport = cmsg_transport_new (transport_type);
    if (transport == NULL)
    {
        char ip[INET6_ADDRSTRLEN] = { };
        uint16_t port;

        if (config->family == PF_INET6)
        {
            port = ntohs (config->sockaddr.in6.sin6_port);
            inet_ntop (config->sockaddr.generic.sa_family,
                       &(config->sockaddr.in6.sin6_addr), ip, INET6_ADDRSTRLEN);
        }
        else
        {
            port = ntohs (config->sockaddr.in.sin_port);
            inet_ntop (config->sockaddr.generic.sa_family,
                       &(config->sockaddr.in.sin_addr), ip, INET6_ADDRSTRLEN);
        }

        CMSG_LOG_GEN_ERROR ("Unable to create TCP RPC transport. tcp[[%s]:%d]", ip, port);

        return NULL;
    }

    transport->config.socket.family = config->family;
    if (config->family == PF_INET6)
    {
        memcpy (&transport->config.socket.sockaddr.in6, &config->sockaddr.in6,
                sizeof (struct sockaddr_in6));
    }
    else
    {
        memcpy (&transport->config.socket.sockaddr.in, &config->sockaddr.in,
                sizeof (struct sockaddr_in));
    }
    cmsg_transport_ipfree_bind_enable (transport, true);

    return transport;
}

/**
 * Create a CMSG transport that uses TCP over IPv4.
 *
 * @param service_name - The service name in the /etc/services file to get
 *                       the port number.
 * @param addr - The IPv4 address to use (in network byte order).
 * @param oneway - Whether to make a one-way transport, or a two-way (RPC) transport.
 */
cmsg_transport *
cmsg_create_transport_tcp_ipv4 (const char *service_name, struct in_addr *addr, bool oneway)
{
    uint32_t port = 0;
    cmsg_transport *transport = NULL;
    char ip_addr[INET_ADDRSTRLEN] = { };
    cmsg_transport_type transport_type;

    transport_type = (oneway == true ? CMSG_TRANSPORT_ONEWAY_TCP : CMSG_TRANSPORT_RPC_TCP);

    port = cmsg_service_port_get (service_name, "tcp");
    if (port <= 0)
    {
        inet_ntop (AF_INET, addr, ip_addr, INET_ADDRSTRLEN);
        CMSG_LOG_GEN_ERROR ("Unknown TCP service. Server:%s, IP:%s", service_name, ip_addr);
        return NULL;
    }

    transport = cmsg_transport_new (transport_type);
    if (transport == NULL)
    {
        inet_ntop (AF_INET, addr, ip_addr, INET_ADDRSTRLEN);
        CMSG_LOG_GEN_ERROR ("Unable to create TCP transport. Server:%s, IP:%s",
                            service_name, ip_addr);
        return NULL;
    }

    transport->config.socket.family = PF_INET;
    transport->config.socket.sockaddr.generic.sa_family = PF_INET;
    transport->config.socket.sockaddr.in.sin_family = AF_INET;
    transport->config.socket.sockaddr.in.sin_port = port;
    transport->config.socket.sockaddr.in.sin_addr.s_addr = addr->s_addr;

    cmsg_transport_ipfree_bind_enable (transport, true);

    return transport;
}
