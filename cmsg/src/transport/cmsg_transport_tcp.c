/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#include "cmsg_private.h"
#include "cmsg_transport.h"
#include "cmsg_transport_private.h"
#include "cmsg_error.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <simple_shm.h>

/* This value should match the maximum number of expected nodes in a
 * cluster using the CMSG service listener functionality. */
#define TCP_CONNECTION_CACHE_SIZE 24

typedef struct
{
    bool present;
    struct in_addr address;
} tcp_connection_cache_entry;

typedef struct
{
    uint8_t num_entries;
    tcp_connection_cache_entry entries[TCP_CONNECTION_CACHE_SIZE];
} tcp_connection_cache;

static void cmsg_transport_tcp_cache_init (void *_cache);

static simple_shm_info shm_info = {
    .shared_data = NULL,
    .shared_data_size = sizeof (tcp_connection_cache),
    .shared_mem_key = 0x436d5463,   /* Hex value of "CmTc" */
    .shared_sem_key = 0x436d5463,   /* Hex value of "CmTc" */
    .shared_sem_num = 1,
    .shm_id = -1,
    .sem_id = -1,
    .init_func = cmsg_transport_tcp_cache_init,
};

/**
 * Initialise the TCP connection cache.
 */
static void
cmsg_transport_tcp_cache_init (void *_cache)
{
    tcp_connection_cache *cache = (tcp_connection_cache *) _cache;

    cache->num_entries = 0;
}

/**
 * Set an entry for the given address in the TCP connection cache.
 * Note that this functionality is lockless and assumes only a single
 * thread in a single process will ever set these entries.
 *
 * @param address - The address to set in the cache.
 * @param present - Whether the address is present or not.
 */
void
cmsg_transport_tcp_cache_set (struct in_addr *address, bool present)
{
    tcp_connection_cache *cache = get_shared_memory (&shm_info);
    int index;

    for (index = 0; index < cache->num_entries; index++)
    {
        if (cache->entries[index].address.s_addr == address->s_addr)
        {
            cache->entries[index].present = present;
            return;
        }
    }

    if (cache->num_entries < TCP_CONNECTION_CACHE_SIZE - 1)
    {
        cache->entries[cache->num_entries].address = *address;
        cache->entries[cache->num_entries].present = present;
        cache->num_entries++;
    }
    else
    {
        CMSG_LOG_GEN_ERROR ("TCP connection cache exhausted");
    }
}

/**
 * Check the given address in the TCP connection cache.
 * Note that this functionality is lockless and assumes only a single
 * thread in a single process will ever set these entries.
 *
 * @param address - The address to check in the cache.
 *
 * @returns true if the address is available or not currently cached (meaning
 *          that we should attempt to connect to it).
 *          false if the address is not available (meaning we should not attempt
 *          to connect to it).
 */
static bool
cmsg_transport_tcp_cache_should_connect (struct in_addr *address)
{
    tcp_connection_cache *cache = get_shared_memory (&shm_info);
    int index;

    for (index = 0; index < cache->num_entries; index++)
    {
        if (cache->entries[index].address.s_addr == address->s_addr)
        {
            return cache->entries[index].present;
        }
    }

    return true;
}

/*
 * Create a TCP socket connection.
 * Returns 0 on success or a negative integer on failure.
 */
static int32_t
cmsg_transport_tcp_connect (cmsg_transport *transport)
{
    int ret;
    struct sockaddr *addr;
    uint32_t addr_len;
    struct in_addr *address = &transport->config.socket.sockaddr.in.sin_addr;

    /* Check the connection cache for IPv4 addresses */
    if (transport->config.socket.family == PF_INET &&
        !cmsg_transport_tcp_cache_should_connect (address))
    {
        CMSG_LOG_TRANSPORT_ERROR (transport, "Failed to connect to remote host. Error: %s",
                                  "Dead cache entry");
        return -1;
    }

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

        if (transport->config.socket.sockaddr.in6.sin6_scope_id == 0 &&
            transport->config.socket.vrf_bind_dev[0] != '\0')
        {
            ret = setsockopt (transport->socket, SOL_SOCKET, SO_BINDTODEVICE,
                              transport->config.socket.vrf_bind_dev,
                              strlen (transport->config.socket.vrf_bind_dev) + 1);
            if (ret < 0)
            {
                ret = -errno;
                CMSG_LOG_TRANSPORT_ERROR (transport, "Unable to create socket. Error:%s",
                                          strerror (errno));
                close (transport->socket);
                transport->socket = -1;
                return ret;
            }
        }
    }
    else
    {
        addr = (struct sockaddr *) &transport->config.socket.sockaddr.in;
        addr_len = sizeof (transport->config.socket.sockaddr.in);

        if (transport->config.socket.vrf_bind_dev[0] != '\0')
        {
            ret = setsockopt (transport->socket, SOL_SOCKET, SO_BINDTODEVICE,
                              transport->config.socket.vrf_bind_dev,
                              strlen (transport->config.socket.vrf_bind_dev) + 1);
            if (ret < 0)
            {
                ret = -errno;
                CMSG_LOG_TRANSPORT_ERROR (transport, "Unable to create socket. Error:%s",
                                          strerror (errno));
                close (transport->socket);
                transport->socket = -1;
                return ret;
            }
        }
    }

    if (connect_nb (transport->socket, addr, addr_len, transport->connect_timeout) < 0)
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

    /* If IPv6 and it's not link local, or if it's IPv4, then if VRF bind device is set,
     * add it as a socket option.
     */
    if ((transport->config.socket.family == PF_INET6 &&
         transport->config.socket.sockaddr.in6.sin6_scope_id == 0) ||
        (transport->config.socket.family == PF_INET))
    {
        if (transport->config.socket.vrf_bind_dev[0] != '\0')
        {
            ret = setsockopt (listening_socket, SOL_SOCKET, SO_BINDTODEVICE,
                              transport->config.socket.vrf_bind_dev,
                              strlen (transport->config.socket.vrf_bind_dev) + 1);
            if (ret < 0)
            {
                CMSG_LOG_TRANSPORT_ERROR (transport, "Unable to setsockopt. Error:%s",
                                          strerror (errno));
                close (listening_socket);
                return -1;
            }
        }
    }

    /* IP_FREEBIND sock opt permits binding to a non-local or non-existent address.
     * This is done here to resolve the race condition with IPv6 DAD. Until DAD can
     * confirm that there is no other host with the same address, the address is
     * considered to be "tentative". While it is in this state, attempts to bind()
     * to the address fail with EADDRNOTAVAIL, as if the address doesn't exist.
     * */
    ret = setsockopt (listening_socket, IPPROTO_IP, IP_FREEBIND, &yes, sizeof (int32_t));
    if (ret == -1)
    {
        CMSG_LOG_TRANSPORT_ERROR (transport, "Unable to setsockopt. Error:%s",
                                  strerror (errno));
        close (listening_socket);
        return -1;
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

    return cmsg_transport_socket_recv (sock, buff, len, flags);
}

/**
 * Set SO_LINGER with a timeout of zero to ensure that the TCP connection is
 * reset on close, rather than shutting down gracefully. We do this because
 * most often we are shutting down the connection after the other end of the
 * connection has already disappeared. Because of this the kernel will keep
 * the socket around for a long time in the FIN_WAIT_1 state waiting to finish
 * the TCP connection gracefully. To avoid the socket resources being kept we
 * choose to simply reset the connection, causing the socket resource to be
 * released immediately.
 */
static void
cmsg_transport_tcp_set_so_linger (int sock)
{
    struct linger sl;

    sl.l_onoff = 1;
    sl.l_linger = 0;
    setsockopt (sock, SOL_SOCKET, SO_LINGER, &sl, sizeof (sl));
}

static int32_t
cmsg_transport_tcp_server_accept (cmsg_transport *transport)
{
    uint32_t client_len;
    cmsg_transport client_transport;
    int sock;
    struct sockaddr *addr;
    int listen_socket = transport->socket;

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

    cmsg_transport_tcp_set_so_linger (sock);

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
    return (cmsg_transport_socket_send (transport->socket, buff, length, flag));
}

static void
cmsg_transport_tcp_enable_keepalive (int sock)
{
    int value;

    value = 5;  /* Idle time in seconds until keepalive probes start */
    setsockopt (sock, SOL_TCP, TCP_KEEPIDLE, (void *) &value, sizeof (value));

    value = 3;  /* Number keepalive probes sent before dropping the connection */
    setsockopt (sock, SOL_TCP, TCP_KEEPCNT, (void *) &value, sizeof (value));

    value = 1;  /* The time in seconds between keepalive probes */
    setsockopt (sock, SOL_TCP, TCP_KEEPINTVL, (void *) &value, sizeof (value));

    value = 1;  /* Enable keepalive probes */
    setsockopt (sock, SOL_SOCKET, SO_KEEPALIVE, (void *) &value, sizeof (value));
}

static void
cmsg_transport_tcp_socket_close (cmsg_transport *transport)
{
    if (transport->socket != -1)
    {
        if (transport->type == CMSG_TRANSPORT_ONEWAY_TCP)
        {
            /* Oneway transports are not synchronous so we do not know if the kernel
             * has fully drained the send buffer before closing the socket. Therefore
             * we cannot hard reset the connection via 'cmsg_transport_set_so_linger'.
             * To allow the kernel to fully drain the socket, and avoid orphaned sockets,
             * we enable TCP keepalive probes so that the kernel can detect the dead
             * connection and remove the socket. */
            cmsg_transport_tcp_enable_keepalive (transport->socket);
        }
        else
        {
            /* RPC transports are synchronous so we know that all the data has been
             * sent before closing the connection. Simply use SO_LINGER to hard reset
             * the connection. */
            cmsg_transport_tcp_set_so_linger (transport->socket);
        }

        CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] shutting down socket\n");
        shutdown (transport->socket, SHUT_RDWR);

        CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] closing socket\n");
        close (transport->socket);

        transport->socket = -1;
    }
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
    tport_funcs->socket_close = cmsg_transport_tcp_socket_close;
    tport_funcs->get_socket = cmsg_transport_get_socket;
    tport_funcs->destroy = NULL;
    tport_funcs->apply_send_timeout = cmsg_transport_apply_send_timeout;
    tport_funcs->apply_recv_timeout = cmsg_transport_apply_recv_timeout;
}


static void
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


static void
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

/**
 * Create a CMSG transport that uses TCP over IPv4.
 *
 * @param service_name - The service name in the /etc/services file to get
 *                       the port number.
 * @param addr - The IPv4 address to use (in network byte order).
 * @param vrf_bind_dev - For VRF support, the device to bind to the socket (NULL if not relevant)
 * @param oneway - Whether to make a one-way transport, or a two-way (RPC) transport.
 */
cmsg_transport *
cmsg_create_transport_tcp_ipv4 (const char *service_name, struct in_addr *addr,
                                const char *vrf_bind_dev, bool oneway)
{
    uint16_t port = 0;
    cmsg_transport *transport = NULL;
    char ip_addr[INET_ADDRSTRLEN] = { };
    cmsg_transport_type transport_type;

    transport_type = (oneway == true ? CMSG_TRANSPORT_ONEWAY_TCP : CMSG_TRANSPORT_RPC_TCP);

    port = cmsg_service_port_get (service_name, "tcp");
    if (port == 0)
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
    transport->config.socket.sockaddr.in.sin_port = htons (port);
    transport->config.socket.sockaddr.in.sin_addr.s_addr = addr->s_addr;
    if (vrf_bind_dev)
    {
        strncpy (transport->config.socket.vrf_bind_dev, vrf_bind_dev,
                 CMSG_BIND_DEV_NAME_MAX);
        transport->config.socket.vrf_bind_dev[CMSG_BIND_DEV_NAME_MAX - 1] = '\0';
    }

    return transport;
}

/**
 * Create a CMSG transport that uses TCP over IPv6.
 *
 * @param service_name - The service name in the /etc/services file to get
 *                       the port number.
 * @param addr - The IPv6 address to use (in network byte order).
 * @param scope_id - The scope id if a link local address is used, zero otherwise
 * @param vrf_bind_dev - For VRF support, the device to bind to the socket (NULL if not relevant)
 * @param oneway - Whether to make a one-way transport, or a two-way (RPC) transport.
 */
cmsg_transport *
cmsg_create_transport_tcp_ipv6 (const char *service_name, struct in6_addr *addr,
                                uint32_t scope_id, const char *vrf_bind_dev, bool oneway)
{
    uint16_t port = 0;
    cmsg_transport *transport = NULL;
    char ip6_addr[INET6_ADDRSTRLEN] = { };
    cmsg_transport_type transport_type;

    transport_type = (oneway == true ? CMSG_TRANSPORT_ONEWAY_TCP : CMSG_TRANSPORT_RPC_TCP);

    port = cmsg_service_port_get (service_name, "tcp");

    if (port == 0)
    {
        inet_ntop (AF_INET6, addr, ip6_addr, INET6_ADDRSTRLEN);
        CMSG_LOG_GEN_ERROR ("Unknown TCP service. Server:%s, IP:%s",
                            service_name, ip6_addr);
        return NULL;
    }

    transport = cmsg_transport_new (transport_type);
    if (transport == NULL)
    {
        inet_ntop (AF_INET6, addr, ip6_addr, INET6_ADDRSTRLEN);
        CMSG_LOG_GEN_ERROR ("Unable to create TCP transport. Server:%s, IP:%s",
                            service_name, ip6_addr);
        return NULL;
    }

    transport->config.socket.family = PF_INET6;
    transport->config.socket.sockaddr.generic.sa_family = PF_INET6;
    transport->config.socket.sockaddr.in6.sin6_family = AF_INET6;
    transport->config.socket.sockaddr.in6.sin6_port = htons (port);
    transport->config.socket.sockaddr.in6.sin6_flowinfo = 0;
    transport->config.socket.sockaddr.in6.sin6_scope_id = scope_id;
    memcpy (&transport->config.socket.sockaddr.in6.sin6_addr, addr,
            sizeof (struct in6_addr));
    if (vrf_bind_dev)
    {
        strncpy (transport->config.socket.vrf_bind_dev, vrf_bind_dev,
                 CMSG_BIND_DEV_NAME_MAX);
        transport->config.socket.vrf_bind_dev[CMSG_BIND_DEV_NAME_MAX - 1] = '\0';
    }

    return transport;
}
