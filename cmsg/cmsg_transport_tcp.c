#include "cmsg_private.h"
#include "cmsg_transport.h"
#include "cmsg_client.h"
#include "cmsg_server.h"
#include "cmsg_error.h"
#include <arpa/inet.h>

/*
 * Create a TCP socket connection.
 * Returns 0 on success or a negative integer on failure.
 */
static int32_t
cmsg_transport_tcp_connect (cmsg_client *client)
{
    int ret;
    struct sockaddr *addr;
    uint32_t addr_len;

    if (client == NULL)
        return 0;

    client->connection.socket = socket (client->_transport->config.socket.family,
                                        SOCK_STREAM, 0);

    if (client->connection.socket < 0)
    {
        ret = -errno;
        client->state = CMSG_CLIENT_STATE_FAILED;
        CMSG_LOG_CLIENT_ERROR (client, "Unable to create socket. Error:%s",
                               strerror (errno));
        return ret;
    }

    if (client->_transport->config.socket.family == PF_INET6)
    {
        addr = (struct sockaddr *) &client->_transport->config.socket.sockaddr.in6;
        addr_len = sizeof (client->_transport->config.socket.sockaddr.in6);
    }
    else
    {
        addr = (struct sockaddr *) &client->_transport->config.socket.sockaddr.in;
        addr_len = sizeof (client->_transport->config.socket.sockaddr.in);
    }

    if (connect (client->connection.socket, addr, addr_len) < 0)
    {
        if (errno == EINPROGRESS)
        {
            //?
        }

        ret = -errno;
        CMSG_LOG_CLIENT_ERROR (client,
                               "Failed to connect to remote host. Error:%s",
                               strerror (errno));

        close (client->connection.socket);
        client->connection.socket = -1;
        client->state = CMSG_CLIENT_STATE_FAILED;

        return ret;
    }
    else
    {
        client->state = CMSG_CLIENT_STATE_CONNECTED;
        CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] succesfully connected\n");
        return 0;
    }
}


static int32_t
cmsg_transport_tcp_listen (cmsg_server *server)
{
    int32_t yes = 1;    // for setsockopt() SO_REUSEADDR, below
    int32_t listening_socket = -1;
    int32_t ret = 0;
    socklen_t addrlen = 0;
    cmsg_transport *transport = NULL;
    int port = 0;

    if (server == NULL)
        return 0;

    server->connection.sockets.listening_socket = 0;
    server->connection.sockets.client_socket = 0;

    transport = server->_transport;
    listening_socket = socket (transport->config.socket.family, SOCK_STREAM, 0);
    if (listening_socket == -1)
    {
        CMSG_LOG_SERVER_ERROR (server, "Unable to create socket. Error:%s",
                               strerror (errno));
        return -1;
    }

    ret = setsockopt (listening_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof (int32_t));
    if (ret == -1)
    {
        CMSG_LOG_SERVER_ERROR (server, "Unable to setsockopt. Error:%s", strerror (errno));
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
            CMSG_LOG_SERVER_ERROR (server, "Unable to setsockopt. Error:%s",
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
        CMSG_LOG_SERVER_ERROR (server, "Unable to bind socket. Error:%s", strerror (errno));
        close (listening_socket);
        return -1;
    }

    ret = listen (listening_socket, 10);
    if (ret < 0)
    {
        CMSG_LOG_SERVER_ERROR (server, "Listen failed. Error:%s", strerror (errno));
        close (listening_socket);
        return -1;
    }

    server->connection.sockets.listening_socket = listening_socket;

    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] listening on tcp socket: %d\n", listening_socket);

#ifndef DEBUG_DISABLED
    if (server->_transport->config.socket.family == PF_INET6)
    {
        port = (int) (ntohs (server->_transport->config.socket.sockaddr.in6.sin6_port));
    }
    else
    {
        port = (int) (ntohs (server->_transport->config.socket.sockaddr.in.sin_port));
    }
#endif

    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] listening on port: %d\n", port);

    return 0;
}


/* Wrapper function to call "recv" on a TCP socket */
int
cmsg_transport_tcp_recv (void *handle, void *buff, int len, int flags)
{
    int *sock = (int *) handle;

    return recv (*sock, buff, len, flags);
}


static int32_t
cmsg_transport_tcp_server_recv (int32_t server_socket, cmsg_server *server)
{
    int32_t ret = 0;

    if (!server || server_socket < 0)
    {
        CMSG_LOG_GEN_ERROR ("TCP server receive error. Invalid arguments.");
        return -1;
    }

    /* Remember the client socket to use when send reply */
    server->connection.sockets.client_socket = server_socket;

    ret = cmsg_transport_server_recv (cmsg_transport_tcp_recv,
                                      (void *) &server_socket, server);

    return ret;
}


static int32_t
cmsg_transport_tcp_server_accept (int32_t listen_socket, cmsg_server *server)
{
    uint32_t client_len;
    cmsg_transport client_transport;
    int sock;
    struct sockaddr *addr;

    if (!server || listen_socket < 0)
    {
        CMSG_LOG_GEN_ERROR ("TCP server accept error. Invalid arguments.");
        return -1;
    }

    if (client_transport.config.socket.family == PF_INET6)
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
        CMSG_LOG_SERVER_ERROR (server, "Accept failed. Error:%s", strerror (errno));
        CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] sock = %d\n", sock);

        return -1;
    }

    return sock;
}


static cmsg_status_code
cmsg_transport_tcp_client_recv (cmsg_client *client, ProtobufCMessage **messagePtPt)
{
    cmsg_status_code ret;

    *messagePtPt = NULL;

    if (!client)
    {
        return CMSG_STATUS_CODE_SERVICE_FAILED;
    }

    ret = cmsg_transport_client_recv (cmsg_transport_tcp_recv,
                                      (void *) &client->connection.socket, client,
                                      messagePtPt);

    return ret;
}


static int32_t
cmsg_transport_tcp_client_send (cmsg_client *client, void *buff, int length, int flag)
{
    return (send (client->connection.socket, buff, length, flag));
}

static int32_t
cmsg_transport_tcp_rpc_server_send (cmsg_server *server, void *buff, int length, int flag)
{
    return (send (server->connection.sockets.client_socket, buff, length, flag));
}

/**
 * TCP oneway servers do not send replies to received messages. This function therefore
 * returns 0.
 */
static int32_t
cmsg_transport_tcp_oneway_server_send (cmsg_server *server, void *buff, int length,
                                       int flag)
{
    return 0;
}

static void
cmsg_transport_tcp_client_close (cmsg_client *client)
{
    if (client->connection.socket != -1)
    {
        CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] shutting down socket\n");
        shutdown (client->connection.socket, SHUT_RDWR);

        CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] closing socket\n");
        close (client->connection.socket);

        client->connection.socket = -1;
    }
}

static void
cmsg_transport_tcp_server_close (cmsg_server *server)
{
    CMSG_DEBUG (CMSG_INFO, "[SERVER] shutting down socket\n");
    shutdown (server->connection.sockets.client_socket, SHUT_RDWR);

    CMSG_DEBUG (CMSG_INFO, "[SERVER] closing socket\n");
    close (server->connection.sockets.client_socket);
}

static int
cmsg_transport_tcp_server_get_socket (cmsg_server *server)
{
    return server->connection.sockets.listening_socket;
}


static int
cmsg_transport_tcp_client_get_socket (cmsg_client *client)
{
    return client->connection.socket;
}

static void
cmsg_transport_tcp_client_destroy (cmsg_client *cmsg_client)
{
    //placeholder to make sure destroy functions are called in the right order
}

static void
cmsg_transport_tcp_server_destroy (cmsg_server *server)
{
    CMSG_DEBUG (CMSG_INFO, "[SERVER] Shutting down listening socket\n");
    shutdown (server->connection.sockets.listening_socket, SHUT_RDWR);

    CMSG_DEBUG (CMSG_INFO, "[SERVER] Closing listening socket\n");
    close (server->connection.sockets.listening_socket);
}


/**
 * TCP is never congested
 */
uint32_t
cmsg_transport_tcp_is_congested (cmsg_client *client)
{
    return FALSE;
}

int32_t
cmsg_transport_tcp_send_called_multi_threads_enable (cmsg_transport *transport,
                                                     uint32_t enable)
{
    // Don't support sending from multiple threads
    return -1;
}


int32_t
cmsg_transport_tcp_send_can_block_enable (cmsg_transport *transport,
                                          uint32_t send_can_block)
{
    transport->send_can_block = send_can_block;
    return 0;
}


int32_t
cmsg_transport_tcp_ipfree_bind_enable (cmsg_transport *transport, cmsg_bool_t use_ipfree_bind)
{
    transport->use_ipfree_bind = use_ipfree_bind;
    return 0;
}

static void
_cmsg_transport_tcp_init_common (cmsg_transport *transport)
{
    transport->config.socket.family = PF_INET;
    transport->config.socket.sockaddr.generic.sa_family = PF_INET;
    transport->connect = cmsg_transport_tcp_connect;
    transport->listen = cmsg_transport_tcp_listen;
    transport->server_accept = cmsg_transport_tcp_server_accept;
    transport->server_recv = cmsg_transport_tcp_server_recv;
    transport->client_recv = cmsg_transport_tcp_client_recv;
    transport->client_send = cmsg_transport_tcp_client_send;
    transport->invoke_send = cmsg_client_invoke_send;
    transport->client_close = cmsg_transport_tcp_client_close;
    transport->server_close = cmsg_transport_tcp_server_close;
    transport->s_socket = cmsg_transport_tcp_server_get_socket;
    transport->c_socket = cmsg_transport_tcp_client_get_socket;
    transport->client_destroy = cmsg_transport_tcp_client_destroy;
    transport->server_destroy = cmsg_transport_tcp_server_destroy;
    transport->is_congested = cmsg_transport_tcp_is_congested;
    transport->send_called_multi_threads_enable =
        cmsg_transport_tcp_send_called_multi_threads_enable;
    transport->send_called_multi_enabled = FALSE;
    transport->send_can_block_enable = cmsg_transport_tcp_send_can_block_enable;
    transport->ipfree_bind_enable = cmsg_transport_tcp_ipfree_bind_enable;
}

void
cmsg_transport_tcp_init (cmsg_transport *transport)
{
    if (transport == NULL)
        return;

    _cmsg_transport_tcp_init_common (transport);

    transport->server_send = cmsg_transport_tcp_rpc_server_send;
    transport->closure = cmsg_server_closure_rpc;
    transport->invoke_recv = cmsg_client_invoke_recv;

    CMSG_DEBUG (CMSG_INFO, "%s: done\n", __FUNCTION__);
}


void
cmsg_transport_oneway_tcp_init (cmsg_transport *transport)
{
    if (transport == NULL)
        return;

    _cmsg_transport_tcp_init_common (transport);

    transport->server_send = cmsg_transport_tcp_oneway_server_send;
    transport->closure = cmsg_server_closure_oneway;
    transport->invoke_recv = NULL;

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
                       &(config->sockaddr.in6.sin6_addr), ip,
                       INET6_ADDRSTRLEN);
        }
        else
        {
            port = ntohs (config->sockaddr.in.sin_port);
            inet_ntop (config->sockaddr.generic.sa_family,
                       &(config->sockaddr.in.sin_addr), ip,
                       INET6_ADDRSTRLEN);
        }

        CMSG_LOG_GEN_ERROR ("Unable to create TCP RPC transport. tcp[[%s]:%d]",
                            ip, port);

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
    cmsg_transport_ipfree_bind_enable (transport, TRUE);

    return transport;
}
