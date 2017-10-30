/**
 * @file cmsg_transport_unix.c
 *
 * Transport layer using UNIX sockets. This transport should be used for
 * process to process IPC
 *
 * Copyright 2016 Allied Telesis Labs, New Zealand
 *
 */

#include "cmsg_private.h"
#include "cmsg_transport.h"
#include "cmsg_transport_private.h"
#include "cmsg_error.h"

/*
 * Create a UNIX socket connection.
 * Returns 0 on success or a negative integer on failure.
 */
static int32_t
cmsg_transport_unix_connect (cmsg_transport *transport, int timeout)
{
    int32_t ret;
    struct sockaddr_un *addr;
    uint32_t addrlen;

    transport->connection.sockets.client_socket = socket (transport->config.socket.family,
                                                          SOCK_STREAM, 0);

    if (transport->connection.sockets.client_socket < 0)
    {
        ret = -errno;
        CMSG_LOG_TRANSPORT_ERROR (transport, "Unable to create socket. Error:%s",
                                  strerror (errno));
        return ret;
    }

    addr = (struct sockaddr_un *) &transport->config.socket.sockaddr.un;
    addrlen = sizeof (transport->config.socket.sockaddr.un);

    if (connect (transport->connection.sockets.client_socket, (struct sockaddr *) addr,
                 addrlen) < 0)
    {
        ret = -errno;
        CMSG_LOG_TRANSPORT_ERROR (transport,
                                  "Failed to connect to remote host. Error:%s",
                                  strerror (errno));
        close (transport->connection.sockets.client_socket);
        transport->connection.sockets.client_socket = -1;

        return ret;
    }
    else
    {
        CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] successfully connected\n");
        return 0;
    }
}


static int32_t
cmsg_transport_unix_listen (cmsg_transport *transport)
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

    unlink (transport->config.socket.sockaddr.un.sun_path);
    addrlen = sizeof (transport->config.socket.sockaddr.un);
    ret = bind (listening_socket,
                (struct sockaddr *) &transport->config.socket.sockaddr.un, addrlen);
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

    transport->connection.sockets.listening_socket = listening_socket;

    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] listening on unix socket: %d\n", listening_socket);

    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] listening on: %s\n",
                transport->config.socket.sockaddr.un.sun_path);

    return 0;
}


/* Wrapper function to call "recv" on a UNIX socket */
int
cmsg_transport_unix_recv (cmsg_transport *transport, int sock, void *buff, int len,
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

    return recv (sock, buff, len, flags);
}


static int32_t
cmsg_transport_unix_server_accept (int32_t listen_socket, cmsg_transport *transport)
{
    uint32_t client_len;
    cmsg_transport client_transport;
    int sock;
    struct sockaddr *addr;

    if (listen_socket < 0)
    {
        CMSG_LOG_GEN_ERROR ("Unix server accept error. Invalid arguments.");
        return -1;
    }

    addr = (struct sockaddr *) &client_transport.config.socket.sockaddr.un;
    client_len = sizeof (client_transport.config.socket.sockaddr.un);

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
cmsg_transport_unix_client_recv (cmsg_transport *transport,
                                 const ProtobufCServiceDescriptor *descriptor,
                                 ProtobufCMessage **messagePtPt)
{
    return cmsg_transport_client_recv (transport, descriptor, messagePtPt);
}


static int32_t
cmsg_transport_unix_client_send (cmsg_transport *transport, void *buff, int length,
                                 int flag)
{
    return (send (transport->connection.sockets.client_socket, buff, length, flag));
}

static int32_t
cmsg_transport_unix_server_send (cmsg_transport *transport, void *buff, int length,
                                 int flag)
{
    return (send (transport->connection.sockets.client_socket, buff, length, flag));
}

/**
 * UNIX oneway servers do not send replies to received messages. This function therefore
 * returns 0.
 */
static int32_t
cmsg_transport_unix_oneway_server_send (cmsg_transport *transport, void *buff, int length,
                                        int flag)
{
    return 0;
}

static void
cmsg_transport_unix_client_close (cmsg_transport *transport)
{
    if (transport->connection.sockets.client_socket != -1)
    {
        CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] shutting down socket\n");
        shutdown (transport->connection.sockets.client_socket, SHUT_RDWR);

        CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] closing socket\n");
        close (transport->connection.sockets.client_socket);

        transport->connection.sockets.client_socket = -1;
    }
}

static void
cmsg_transport_unix_server_close (cmsg_transport *transport)
{
    return;
}

static int
cmsg_transport_unix_server_get_socket (cmsg_transport *transport)
{
    return transport->connection.sockets.listening_socket;
}


static int
cmsg_transport_unix_client_get_socket (cmsg_transport *transport)
{
    return transport->connection.sockets.client_socket;
}

static void
cmsg_transport_unix_client_destroy (cmsg_transport *transport)
{
    //placeholder to make sure destroy functions are called in the right order
}

static void
cmsg_transport_unix_server_destroy (cmsg_transport *transport)
{
    if (transport->connection.sockets.listening_socket != -1)
    {
        CMSG_DEBUG (CMSG_INFO, "[SERVER] Shutting down listening socket\n");
        shutdown (transport->connection.sockets.listening_socket, SHUT_RDWR);

        CMSG_DEBUG (CMSG_INFO, "[SERVER] Closing listening socket\n");
        close (transport->connection.sockets.listening_socket);
    }
}


/**
 * UNIX is never congested
 */
uint32_t
cmsg_transport_unix_is_congested (cmsg_transport *transport)
{
    return FALSE;
}


int32_t
cmsg_transport_unix_send_can_block_enable (cmsg_transport *transport,
                                           uint32_t send_can_block)
{
    transport->send_can_block = send_can_block;
    return 0;
}


static void
_cmsg_transport_unix_init_common (cmsg_transport *transport)
{
    transport->config.socket.family = PF_UNIX;
    transport->config.socket.sockaddr.generic.sa_family = PF_UNIX;
    transport->tport_funcs.recv_wrapper = cmsg_transport_unix_recv;
    transport->tport_funcs.connect = cmsg_transport_unix_connect;
    transport->tport_funcs.listen = cmsg_transport_unix_listen;
    transport->tport_funcs.server_accept = cmsg_transport_unix_server_accept;
    transport->tport_funcs.server_recv = cmsg_transport_server_recv;
    transport->tport_funcs.client_recv = cmsg_transport_unix_client_recv;
    transport->tport_funcs.client_send = cmsg_transport_unix_client_send;
    transport->tport_funcs.client_close = cmsg_transport_unix_client_close;
    transport->tport_funcs.server_close = cmsg_transport_unix_server_close;
    transport->tport_funcs.client_destroy = cmsg_transport_unix_client_destroy;
    transport->tport_funcs.server_destroy = cmsg_transport_unix_server_destroy;
    transport->tport_funcs.s_socket = cmsg_transport_unix_server_get_socket;
    transport->tport_funcs.c_socket = cmsg_transport_unix_client_get_socket;
    transport->tport_funcs.is_congested = cmsg_transport_unix_is_congested;
    transport->tport_funcs.send_can_block_enable =
        cmsg_transport_unix_send_can_block_enable;
    transport->tport_funcs.ipfree_bind_enable = NULL;
}

void
cmsg_transport_rpc_unix_init (cmsg_transport *transport)
{
    if (transport == NULL)
    {
        return;
    }

    _cmsg_transport_unix_init_common (transport);

    transport->tport_funcs.server_send = cmsg_transport_unix_server_send;

    CMSG_DEBUG (CMSG_INFO, "%s: done\n", __FUNCTION__);
}


void
cmsg_transport_oneway_unix_init (cmsg_transport *transport)
{
    if (transport == NULL)
    {
        return;
    }

    _cmsg_transport_unix_init_common (transport);

    transport->tport_funcs.server_send = cmsg_transport_unix_oneway_server_send;

    CMSG_DEBUG (CMSG_INFO, "%s: done\n", __FUNCTION__);
}

cmsg_transport *
cmsg_create_transport_unix (const ProtobufCServiceDescriptor *descriptor,
                            cmsg_transport_type transport_type)
{
    cmsg_transport *transport = NULL;
    char *sun_path;

    transport = cmsg_transport_new (transport_type);
    if (transport == NULL)
    {
        return NULL;
    }

    sun_path = cmsg_transport_unix_sun_path (descriptor);

    transport->config.socket.family = AF_UNIX;
    transport->config.socket.sockaddr.un.sun_family = AF_UNIX;
    strncpy (transport->config.socket.sockaddr.un.sun_path, sun_path,
             sizeof (transport->config.socket.sockaddr.un.sun_path) - 1);

    cmsg_transport_unix_sun_path_free (sun_path);

    return transport;
}

/**
 * Get the CMSG unix transport socket name from the CMSG service descriptor.
 *
 * @param descriptor - CMSG service descriptor to get the unix transport socket name from
 *
 * @return - String representing the unix transport socket name. The memory for this
 *           string must be freed by the caller.
 */
char *
cmsg_transport_unix_sun_path (const ProtobufCServiceDescriptor *descriptor)
{
    char *copy_str = NULL;
    char *iter;

    CMSG_ASPRINTF (&copy_str, "/tmp/%s", descriptor->name);

    /* Replace the '.' in the name with '_' */
    iter = copy_str;
    while (*iter)
    {
        if (*iter == '.')
        {
            *iter = '_';
        }
        iter++;
    }

    return copy_str;
}

void
cmsg_transport_unix_sun_path_free (char *sun_path)
{
    CMSG_FREE (sun_path);
}
