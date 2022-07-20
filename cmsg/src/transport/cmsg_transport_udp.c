/*
 * Copyright 2021, Allied Telesis Labs New Zealand, Ltd
 */
#include "cmsg_private.h"
#include "cmsg_transport_private.h"
#include "cmsg_error.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <fcntl.h>

/**
 * Create a UDP socket for sending on.
 * Returns 0 on success or a negative integer on failure.
 */
static int32_t
cmsg_transport_udp_connect (cmsg_transport *transport)
{
    int ret;
    struct in_addr local_interface;
    struct ipv6_mreq group;
    int32_t yes = 1;

    transport->socket =
        socket (transport->config.socket.family, SOCK_DGRAM | SOCK_NONBLOCK, 0);

    if (transport->socket < 0)
    {
        ret = -errno;
        CMSG_LOG_TRANSPORT_ERROR (transport,
                                  "Unable to create socket (connect), family %u. Error:%s",
                                  transport->config.socket.family, strerror (errno));
        return ret;
    }

    if (transport->config.socket.family == PF_INET6)
    {
        ret = setsockopt (transport->socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof (yes));
        if (ret == -1)
        {
            CMSG_LOG_TRANSPORT_ERROR (transport, "Unable to setsockopt (connect). Error:%s",
                                      strerror (errno));
            close (transport->socket);
            return -1;
        }

        memset (&group, 0, sizeof (group));
        memcpy (group.ipv6mr_multiaddr.s6_addr,
                transport->config.socket.sockaddr.in6.sin6_addr.s6_addr,
                sizeof (transport->config.socket.sockaddr.in6.sin6_addr));

        /* The scope_id in a Linux IPv6 address corresponds to the interface,
         * for MC it is the source interface.
         */
        group.ipv6mr_interface = transport->config.socket.srcaddr.in6.sin6_scope_id;

        if (setsockopt
            (transport->socket, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, (char *) &group,
             sizeof (group)) < 0)
        {
            CMSG_LOG_TRANSPORT_ERROR (transport, "Unable to join the group. Error:%s",
                                      strerror (errno));
            close (transport->socket);
            return -1;
        }

    }
    else
    {
        local_interface.s_addr = transport->config.socket.srcaddr.in.sin_addr.s_addr;

        // Set the source interface for the UDP socket
        if (setsockopt
            (transport->socket, IPPROTO_IP, IP_MULTICAST_IF, &local_interface,
             sizeof (local_interface)) < 0)
        {
            ret = -errno;
            CMSG_LOG_TRANSPORT_ERROR (transport,
                                      "Unable to set IPv4 source interface. Error:%s",
                                      strerror (errno));
            close (transport->socket);
            transport->socket = -1;
            return ret;
        }

        // actual connect isn't required as the datagram socket is connectionless
        // but this sets the destination address for sending
        ret =
            connect (transport->socket, &transport->config.socket.sockaddr.in,
                     sizeof (transport->config.socket.sockaddr.in));
        if (ret < 0)
        {
            ret = -errno;
            CMSG_LOG_TRANSPORT_ERROR (transport,
                                      "Failed to connect/set the group. Error:%s",
                                      strerror (errno));

            close (transport->socket);
            transport->socket = -1;
            return ret;
        }
    }

    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] successfully connected\n");
    return 0;
}


/**
 * Bind to VCS management VLAN interface so that only
 * internal stacking traffic is accepted on the socket.
 */
static int32_t
_cmsg_transport_udp_bind_to_vcs_vlan (int32_t listening_socket)
{
    char ifname[256] = { };
    FILE *fp;
    int i;

    fp = fopen ("/var/lib/stack/reserved_interfaces", "r");
    if (fp != NULL)
    {
        fread (ifname, 1, 256, fp);
        fclose (fp);

        // Read first interface name, stop at first whitespace
        for (i = 0; i < sizeof (ifname); i++)
        {
            if (ifname[i] == ' ')
            {
                ifname[i] = '\0';
                break;
            }
        }

        if (setsockopt
            (listening_socket, SOL_SOCKET, SO_BINDTODEVICE, ifname,
             strnlen (ifname, sizeof (ifname))) == -1)
        {
            return -1;
        }
    }

    return 0;
}

/**
 *  Bind to the IPv4 socket and add the membership to the group and source.
 */
static int32_t
_cmsg_transport_udp_ipv4_vcs_mgmt_listen (cmsg_transport *transport,
                                          int32_t listening_socket)
{
    struct sockaddr_in in;
    struct ip_mreq group;

    memset ((char *) &in, 0, sizeof (in));
    in.sin_family = AF_INET;
    in.sin_port = transport->config.socket.sockaddr.in.sin_port;
    in.sin_addr.s_addr = INADDR_ANY;

    if (bind (listening_socket, &in, sizeof (in)))
    {
        CMSG_LOG_TRANSPORT_ERROR (transport, "Unable to bind socket. Error:%s",
                                  strerror (errno));
        close (listening_socket);
        return -1;
    }

    if (_cmsg_transport_udp_bind_to_vcs_vlan (listening_socket) == -1)
    {
        CMSG_LOG_TRANSPORT_ERROR (transport, "Unable to bind to VCS management interface.");
        close (listening_socket);
        return -1;
    }

    group.imr_multiaddr.s_addr = transport->config.socket.sockaddr.in.sin_addr.s_addr;
    group.imr_interface.s_addr = transport->config.socket.srcaddr.in.sin_addr.s_addr;

    if (setsockopt
        (listening_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *) &group,
         sizeof (group)) < 0)
    {
        CMSG_LOG_TRANSPORT_ERROR (transport, "Unable to join the group. Error:%s",
                                  strerror (errno));
        close (listening_socket);
        return -1;
    }

    transport->socket = listening_socket;

    return 0;
}


/**
 *  Listen on a IPv6 socket, there is no actual listen but this sets up the
 *  binding and adds the membership.
 *
 *  NOTE: scope_id in the transport must be set to the source interface kernel
 *  ifindex for the multicast group.
 */
static int32_t
_cmsg_transport_udp_ipv6_vcs_mgmt_listen (cmsg_transport *transport,
                                          int32_t listening_socket)
{
    struct sockaddr_in6 in6;
    struct ipv6_mreq group;

    memset ((char *) &in6, 0, sizeof (in6));
    in6.sin6_family = AF_INET6;
    in6.sin6_port = transport->config.socket.sockaddr.in.sin_port;
    in6.sin6_addr = in6addr_any;

    if (bind (listening_socket, &in6, sizeof (in6)))
    {
        CMSG_LOG_TRANSPORT_ERROR (transport, "Unable to bind socket. Error:%s",
                                  strerror (errno));
        close (listening_socket);
        return -1;
    }

    if (_cmsg_transport_udp_bind_to_vcs_vlan (listening_socket) == -1)
    {
        CMSG_LOG_TRANSPORT_ERROR (transport, "Unable to bind to VCS management interface.");
        close (listening_socket);
        return -1;
    }

    memset (&group, 0, sizeof (group));
    memcpy (group.ipv6mr_multiaddr.s6_addr,
            transport->config.socket.sockaddr.in6.sin6_addr.s6_addr,
            sizeof (struct in6_addr));

    /* The scope_id in a Linux IPv6 address corresponds to the interface,
     * for MC it is the source interface.
     */
    group.ipv6mr_interface = transport->config.socket.srcaddr.in6.sin6_scope_id;

    if (setsockopt
        (listening_socket, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, (char *) &group,
         sizeof (struct ipv6_mreq)) < 0)
    {
        CMSG_LOG_TRANSPORT_ERROR (transport, "Unable to join the group. Error:%s",
                                  strerror (errno));
        close (listening_socket);
        return -1;
    }

    transport->socket = listening_socket;

    return 0;
}


static int32_t
cmsg_transport_udp_listen (cmsg_transport *transport)
{
    int32_t yes = 1;    // for setsockopt() SO_REUSEADDR, below
    int32_t listening_socket = -1;
    int32_t ret = 0;

    listening_socket = socket (transport->config.socket.family, SOCK_DGRAM, 0);
    if (listening_socket == -1)
    {
        CMSG_LOG_TRANSPORT_ERROR (transport,
                                  "Unable to create socket (listen), family %u. Error:%s",
                                  transport->config.socket.family, strerror (errno));
        return -1;
    }

    ret = setsockopt (listening_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof (yes));
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
    ret = setsockopt (listening_socket, IPPROTO_IP, IP_FREEBIND, &yes, sizeof (yes));
    if (ret == -1)
    {
        CMSG_LOG_TRANSPORT_ERROR (transport, "Unable to setsockopt. Error:%s",
                                  strerror (errno));
        close (listening_socket);
        return -1;
    }

    if (transport->config.socket.family == PF_INET)
    {
        ret = _cmsg_transport_udp_ipv4_vcs_mgmt_listen (transport, listening_socket);
    }
    else
    {
        ret = _cmsg_transport_udp_ipv6_vcs_mgmt_listen (transport, listening_socket);
    }

    if (ret < 0)
    {
        CMSG_LOG_TRANSPORT_ERROR (transport, "Unable to listen. family %s",
                                  transport->config.socket.family ==
                                  PF_INET ? "IPv4" : "IPv6");
        close (listening_socket);
        return ret;
    }
    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] listening on udp socket: %d\n", transport->socket);

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


/* Wrapper function to call "recv" on a UDP socket */
int
cmsg_transport_udp_recv (cmsg_transport *transport, int sock, void *buff, int len,
                         int flags)
{
    return cmsg_transport_socket_recv (sock, buff, len, flags);
}


static int32_t
cmsg_transport_udp_server_accept (cmsg_transport *transport)
{
    // UDP sockets don't accept
    return transport->socket;
}

static cmsg_status_code
cmsg_transport_udp_client_recv (cmsg_transport *transport,
                                const ProtobufCServiceDescriptor *descriptor,
                                ProtobufCMessage **messagePtPt)
{
    return cmsg_transport_client_recv (transport, descriptor, messagePtPt);
}


/**
 * Own implementation of the send function so as to allow usage of sendto
 */
static int32_t
cmsg_transport_udp_client_send (cmsg_transport *transport, void *buff, int length, int flag)
{
    ssize_t ret;
    ssize_t sent_bytes = 0;
    const char *ip_conv_ret;
    const uint8_t *data = (const uint8_t *) buff;
    char string[INET6_ADDRSTRLEN];

    while (sent_bytes < length)
    {
        ret =
            TEMP_FAILURE_RETRY (sendto
                                (transport->socket, data + sent_bytes, length - sent_bytes,
                                 flag, &transport->config.socket.sockaddr.in6,
                                 sizeof (transport->config.socket.sockaddr.in6)));
        if (ret == -1)
        {
            ip_conv_ret = inet_ntop (transport->config.socket.family,
                                     &transport->config.socket.sockaddr.in6.sin6_addr,
                                     string, INET6_ADDRSTRLEN);
            if (ip_conv_ret)
            {
                CMSG_LOG_TRANSPORT_ERROR (transport, "UDP send failed %s %s",
                                          string, strerror (errno));
            }
            else
            {
                CMSG_LOG_TRANSPORT_ERROR (transport,
                                          "UDP send failed, (can't convert address) %s",
                                          strerror (errno));
            }
            return -1;
        }
        sent_bytes += ret;
    }

    return sent_bytes;
}


static void
cmsg_transport_udp_socket_close (cmsg_transport *transport)
{
    if (transport->socket != -1)
    {
        CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] shutting down socket\n");
        shutdown (transport->socket, SHUT_RDWR);

        CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] closing socket\n");
        close (transport->socket);

        transport->socket = -1;
    }
}

static void
_cmsg_transport_udp_init_common (cmsg_tport_functions *tport_funcs)
{
    tport_funcs->recv_wrapper = cmsg_transport_udp_recv;
    tport_funcs->connect = cmsg_transport_udp_connect;
    tport_funcs->listen = cmsg_transport_udp_listen;
    tport_funcs->server_accept = cmsg_transport_udp_server_accept;
    tport_funcs->server_recv = cmsg_transport_server_recv;
    tport_funcs->client_recv = cmsg_transport_udp_client_recv;
    tport_funcs->client_send = cmsg_transport_udp_client_send;
    tport_funcs->socket_close = cmsg_transport_udp_socket_close;
    tport_funcs->get_socket = cmsg_transport_get_socket;
    tport_funcs->destroy = NULL;
    tport_funcs->apply_send_timeout = cmsg_transport_apply_send_timeout;
    tport_funcs->apply_recv_timeout = cmsg_transport_apply_recv_timeout;
}


static void
_cmsg_transport_oneway_udp_funcs_init (cmsg_tport_functions *tport_funcs)
{
    _cmsg_transport_udp_init_common (tport_funcs);

    tport_funcs->server_send = cmsg_transport_oneway_server_send;
}


void
cmsg_transport_oneway_udp_vcs_mgmt_init (cmsg_transport *transport)
{
    if (transport == NULL)
    {
        return;
    }

    transport->config.socket.family = PF_INET;
    transport->config.socket.sockaddr.generic.sa_family = PF_INET;

    _cmsg_transport_oneway_udp_funcs_init (&transport->tport_funcs);

    CMSG_DEBUG (CMSG_INFO, "%s: done\n", __FUNCTION__);
}


/**
 * Create a CMSG transport that uses UDP over IPv4.
 *
 * @param service_name - The service name in the /etc/services file to get
 *                       the port number.
 * @param addr - The IPv4 address to use (in network byte order).
 * @param src_addr - The IPv4 address (in network byte order) of the source interface.
 * @param vrf_bind_dev - For VRF support, the device to bind to the socket (NULL if not relevant)
 * @param oneway - Whether to make a one-way transport, or a two-way (RPC) transport.
 */
cmsg_transport *
cmsg_create_transport_mc_udp_ipv4 (const char *service_name, struct in_addr *addr,
                                   struct in_addr *src_addr, const char *vrf_bind_dev,
                                   bool oneway)
{
    uint16_t port = 0;
    cmsg_transport *transport = NULL;
    char ip_addr[INET_ADDRSTRLEN] = { };
    cmsg_transport_type transport_type;

    if (!oneway)
    {
        inet_ntop (AF_INET, addr, ip_addr, INET_ADDRSTRLEN);
        CMSG_LOG_GEN_ERROR ("Only support oneway UDP. Server:%s, IP:%s", service_name,
                            ip_addr);
        return NULL;
    }

    if (vrf_bind_dev != NULL)
    {
        inet_ntop (AF_INET, addr, ip_addr, INET_ADDRSTRLEN);
        CMSG_LOG_GEN_ERROR ("Don't support VRF bind device. Server:%s, IP:%s", service_name,
                            ip_addr);
        return NULL;
    }

    transport_type = CMSG_TRANSPORT_ONEWAY_MC_UDP;

    port = cmsg_service_port_get (service_name, "udp");
    if (port == 0)
    {
        inet_ntop (AF_INET, addr, ip_addr, INET_ADDRSTRLEN);
        CMSG_LOG_GEN_ERROR ("Unknown UDP service. Server:%s, IP:%s", service_name, ip_addr);
        return NULL;
    }

    transport = cmsg_transport_new (transport_type);
    if (transport == NULL)
    {
        inet_ntop (AF_INET, addr, ip_addr, INET_ADDRSTRLEN);
        CMSG_LOG_GEN_ERROR ("Unable to create UDP transport. Server:%s, IP:%s",
                            service_name, ip_addr);
        return NULL;
    }

    transport->config.socket.family = PF_INET;
    transport->config.socket.sockaddr.generic.sa_family = PF_INET;
    transport->config.socket.sockaddr.in.sin_family = AF_INET;
    transport->config.socket.sockaddr.in.sin_port = htons (port);
    memcpy (&transport->config.socket.sockaddr.in.sin_addr, addr, sizeof (*addr));
    transport->config.socket.srcaddr.in.sin_family = AF_INET;
    memcpy (&transport->config.socket.srcaddr.in.sin_addr, src_addr, sizeof (*src_addr));

    return transport;
}


/**
 * Create a CMSG transport that uses UDP over IPv6.
 *
 * @param service_name - The service name in the /etc/services file to get
 *                       the port number.
 * @param addr - The IPv6 address to use (in network byte order).
 * @param src_addr - The IPv6 address (in network byte order) of the source interface.
 * @param scope_id - the ifindex of the incoming interface for the MC group
 * @param vrf_bind_dev - For VRF support, the device to bind to the socket (NULL if not relevant)
 * @param oneway - Whether to make a one-way transport, or a two-way (RPC) transport.
 */
cmsg_transport *
cmsg_create_transport_mc_udp_ipv6 (const char *service_name, struct in6_addr *addr,
                                   struct in6_addr *src_addr, uint32_t scope_id,
                                   const char *vrf_bind_dev, bool oneway)
{
    uint16_t port = 0;
    cmsg_transport *transport = NULL;
    char ip6_addr[INET6_ADDRSTRLEN] = { };
    cmsg_transport_type transport_type;

    if (!oneway)
    {
        inet_ntop (AF_INET6, addr, ip6_addr, INET6_ADDRSTRLEN);
        CMSG_LOG_GEN_ERROR ("Only support oneway UDP. Server:%s, IP:%s", service_name,
                            ip6_addr);
        return NULL;
    }

    if (vrf_bind_dev != NULL)
    {
        inet_ntop (AF_INET6, addr, ip6_addr, INET6_ADDRSTRLEN);
        CMSG_LOG_GEN_ERROR ("Don't support VRF bind device. Server:%s, IP:%s", service_name,
                            ip6_addr);
        return NULL;
    }

    if (scope_id == 0)
    {
        inet_ntop (AF_INET6, addr, ip6_addr, INET6_ADDRSTRLEN);
        CMSG_LOG_GEN_ERROR ("IPv6 MC %s must have the scope_id set to the source ifindex",
                            ip6_addr);
        return NULL;
    }

    transport_type = CMSG_TRANSPORT_ONEWAY_MC_UDP;

    port = cmsg_service_port_get (service_name, "udp");
    if (port == 0)
    {
        inet_ntop (AF_INET6, addr, ip6_addr, INET6_ADDRSTRLEN);
        CMSG_LOG_GEN_ERROR ("Unknown UDP service. Server:%s, IP:%s", service_name,
                            ip6_addr);
        return NULL;
    }

    transport = cmsg_transport_new (transport_type);
    if (transport == NULL)
    {
        inet_ntop (AF_INET6, addr, ip6_addr, INET6_ADDRSTRLEN);
        CMSG_LOG_GEN_ERROR ("Unable to create UDP6 transport. Server:%s, IP:%s",
                            service_name, ip6_addr);
        return NULL;
    }

    transport->config.socket.family = PF_INET6;
    transport->config.socket.sockaddr.generic.sa_family = PF_INET6;
    transport->config.socket.sockaddr.in6.sin6_family = AF_INET6;
    transport->config.socket.sockaddr.in6.sin6_port = htons (port);
    transport->config.socket.sockaddr.in6.sin6_flowinfo = 0;
    transport->config.socket.sockaddr.in6.sin6_scope_id = scope_id;
    memcpy (&transport->config.socket.sockaddr.in6.sin6_addr, addr, sizeof (*addr));

    transport->config.socket.srcaddr.generic.sa_family = PF_INET6;
    transport->config.socket.srcaddr.in6.sin6_family = AF_INET6;
    transport->config.socket.srcaddr.in6.sin6_port = htons (port);
    transport->config.socket.srcaddr.in6.sin6_flowinfo = 0;
    transport->config.socket.srcaddr.in6.sin6_scope_id = scope_id;
    memcpy (&transport->config.socket.srcaddr.in6.sin6_addr, src_addr, sizeof (*src_addr));

    if (vrf_bind_dev)
    {
        strncpy (transport->config.socket.vrf_bind_dev, vrf_bind_dev,
                 CMSG_BIND_DEV_NAME_MAX);
        transport->config.socket.vrf_bind_dev[CMSG_BIND_DEV_NAME_MAX - 1] = '\0';
    }

    return transport;
}
