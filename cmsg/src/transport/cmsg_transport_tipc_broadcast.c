/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#include "cmsg_private.h"
#include "cmsg_transport.h"
#include "cmsg_transport_private.h"
#include "cmsg_error.h"


/**
 * Creates the connectionless socket used to send messages using tipc.
 * Returns 0 on success or a negative integer on failure.
 */
static int32_t
cmsg_transport_tipc_broadcast_connect (cmsg_transport *transport)
{
    int ret;

    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] cmsg_transport_tipc_broadcast_connect\n");

    transport->socket = socket (transport->config.socket.family, SOCK_RDM, 0);

    if (transport->socket < 0)
    {
        ret = -errno;
        CMSG_LOG_TRANSPORT_ERROR (transport, "Unable to create socket. Error:%s",
                                  strerror (errno));
        return ret;
    }

    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] successfully connected\n");

    return 0;
}


/**
 * Creates the connectionless socket used to receive tipc messages
 */
static int32_t
cmsg_transport_tipc_broadcast_listen (cmsg_transport *transport)
{
    int32_t listening_socket = -1;
    int32_t addrlen = 0;

    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] Creating listen socket\n");

    listening_socket = socket (transport->config.socket.family, SOCK_RDM, 0);
    if (listening_socket == -1)
    {
        CMSG_LOG_TRANSPORT_ERROR (transport, "Failed to create socket. Error:%s",
                                  strerror (errno));

        return -1;
    }

    //TODO: stk_tipc.c adds the addressing information here

    addrlen = sizeof (transport->config.socket.sockaddr.tipc);
    /* bind the socket address (publishes the TIPC port name) */
    if (bind (listening_socket,
              (struct sockaddr *) &transport->config.socket.sockaddr.tipc, addrlen) != 0)
    {
        CMSG_LOG_TRANSPORT_ERROR (transport, "TIPC port could not be created");
        return -1;
    }

    //TODO: Do we need a listen?
    transport->socket = listening_socket;
    //TODO: Add debug
    return 0;
}


/* Wrapper function to call "recv" on a TIPC broadcast socket */
int
cmsg_transport_tipc_broadcast_recv (cmsg_transport *transport, int sock, void *buff,
                                    int len, int flags)
{
    uint32_t addrlen = 0;
    int nbytes;
    struct timeval timeout = { 1, 0 };
    fd_set read_fds;
    int maxfd;

    FD_ZERO (&read_fds);
    FD_SET (sock, &read_fds);
    maxfd = sock;

    /* Do select() on the socket to prevent it to go to usleep instantaneously in the loop
     * if the data is not yet available.*/
    select (maxfd + 1, &read_fds, NULL, NULL, &timeout);

    addrlen = sizeof (struct sockaddr_tipc);

    nbytes = recvfrom (transport->socket, buff, len, flags,
                       (struct sockaddr *) &transport->config.socket.sockaddr.tipc,
                       &addrlen);
    return nbytes;
}


/**
 * TIPC broadcast clients do not receive a reply to their messages. This
 * function therefore returns NULL. It should not be called by the client, but
 * it prevents a null pointer exception from occurring if no function is
 * defined
 */
static cmsg_status_code
cmsg_transport_tipc_broadcast_client_recv (cmsg_transport *transport,
                                           const ProtobufCServiceDescriptor *descriptor,
                                           ProtobufCMessage **messagePtPt)
{
    *messagePtPt = NULL;
    return CMSG_STATUS_CODE_SUCCESS;
}


/**
 * Send the data in buff to the server specified in the clients transports
 * addressing structure. Does not block.
 */
static int32_t
cmsg_transport_tipc_broadcast_client_send (cmsg_transport *transport, void *buff,
                                           int length, int flag)
{
    int retries = 0;
    int saved_errno = 0;

    int result = sendto (transport->socket, buff, length,
                         MSG_DONTWAIT,
                         (struct sockaddr *) &transport->config.socket.sockaddr.tipc,
                         sizeof (struct sockaddr_tipc));

    if (result != length)
    {
        CMSG_LOG_DEBUG ("[TRANSPORT] Failed to send tipc broadcast, result=%d, errno=%d\n",
                        result, errno);

        while (result != length && retries < 25)
        {
            usleep (50000);
            retries++;

            result = sendto (transport->socket, buff, length,
                             MSG_DONTWAIT,
                             (struct sockaddr *) &transport->config.socket.sockaddr.tipc,
                             sizeof (struct sockaddr_tipc));

            saved_errno = errno;
        }
    }

    if (retries >= 25)
    {
        CMSG_LOG_TRANSPORT_ERROR (transport,
                                  "Failed to send tipc broadcast message. Exceeded %d retries. Last error: %s.",
                                  retries, strerror (saved_errno));
        errno = saved_errno;
    }
    else if (retries > 0)
    {
        CMSG_LOG_DEBUG ("[TRANSPORT] Succeeded sending tipc broadcast (retries=%d)\n",
                        retries);
    }

    return result;
}


/**
 * TIPC BC can be congested but we don't check for it
 */
bool
cmsg_transport_tipc_broadcast_is_congested (cmsg_transport *transport)
{
    return false;
}


int32_t
cmsg_transport_tipc_broadcast_ipfree_bind_enable (cmsg_transport *transport,
                                                  cmsg_bool_t use_ipfree_bind)
{
    /* not supported yet */
    return -1;
}


/**
 * Setup the transport structure with the appropriate function pointers for
 * TIPC broadcast, and transport family.
 */
void
cmsg_transport_tipc_broadcast_init (cmsg_transport *transport)
{
    if (transport == NULL)
    {
        return;
    }

    transport->config.socket.family = AF_TIPC;
    transport->config.socket.sockaddr.tipc.family = AF_TIPC;

    transport->tport_funcs.recv_wrapper = cmsg_transport_tipc_broadcast_recv;
    transport->tport_funcs.connect = cmsg_transport_tipc_broadcast_connect;
    transport->tport_funcs.listen = cmsg_transport_tipc_broadcast_listen;
    transport->tport_funcs.server_recv = cmsg_transport_server_recv;
    transport->tport_funcs.client_recv = cmsg_transport_tipc_broadcast_client_recv;
    transport->tport_funcs.client_send = cmsg_transport_tipc_broadcast_client_send;
    transport->tport_funcs.server_send = cmsg_transport_oneway_server_send;
    transport->tport_funcs.socket_close = cmsg_transport_socket_close;
    transport->tport_funcs.get_socket = cmsg_transport_get_socket;

    transport->tport_funcs.is_congested = cmsg_transport_tipc_broadcast_is_congested;
    transport->tport_funcs.ipfree_bind_enable =
        cmsg_transport_tipc_broadcast_ipfree_bind_enable;
    transport->tport_funcs.destroy = NULL;
    transport->tport_funcs.apply_send_timeout = cmsg_transport_apply_send_timeout;
    transport->tport_funcs.apply_recv_timeout = cmsg_transport_apply_recv_timeout;
}
