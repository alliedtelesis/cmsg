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
cmsg_transport_tipc_connect (cmsg_transport *transport, int timeout)
{
    int ret;

    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] cmsg_transport_tipc_connect\n");

    transport->socket = socket (transport->config.socket.family, SOCK_STREAM, 0);

    if (transport->socket < 0)
    {
        ret = -errno;
        CMSG_LOG_TRANSPORT_ERROR (transport, "Unable to create socket. Error:%s",
                                  strerror (errno));

        return ret;
    }

    if (timeout != CONNECT_TIMEOUT_DEFAULT)
    {
        int tipc_timeout = timeout;
        setsockopt (transport->socket, SOL_TIPC,
                    TIPC_CONN_TIMEOUT, &tipc_timeout, sizeof (int));
    }

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

    return recv (sock, buff, len, flags);
}


static int32_t
cmsg_transport_tipc_server_accept (int32_t listen_socket, cmsg_transport *transport)
{
    uint32_t client_len;
    cmsg_transport client_transport;
    int sock;

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
    return (send (transport->socket, buff, length, flag));
}

static void
cmsg_transport_tipc_client_close (cmsg_transport *transport)
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
cmsg_transport_tipc_server_close (cmsg_transport *transport)
{
    return;
}


static int
cmsg_transport_tipc_server_get_socket (cmsg_transport *transport)
{
    return transport->socket;
}


static int
cmsg_transport_tipc_client_get_socket (cmsg_transport *transport)
{
    return transport->socket;
}

static void
cmsg_transport_tipc_client_destroy (cmsg_transport *transport)
{
    //placeholder to make sure destroy functions are called in the right order
}

static void
cmsg_transport_tipc_server_destroy (cmsg_transport *transport)
{
    if (transport->socket != -1)
    {
        CMSG_DEBUG (CMSG_INFO, "[SERVER] Shutting down listening socket\n");
        shutdown (transport->socket, SHUT_RDWR);

        CMSG_DEBUG (CMSG_INFO, "[SERVER] Closing listening socket\n");
        close (transport->socket);
    }
}


/**
 * TIPC is never congested
 */
bool
cmsg_transport_tipc_is_congested (cmsg_transport *transport)
{
    return false;
}


int32_t
cmsg_transport_tipc_send_can_block_enable (cmsg_transport *transport,
                                           uint32_t send_can_block)
{
    transport->send_can_block = send_can_block;
    return 0;
}


int32_t
cmsg_transport_tipc_ipfree_bind_enable (cmsg_transport *transport,
                                        cmsg_bool_t use_ipfree_bind)
{
    /* not supported yet */
    return -1;
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
    transport->tport_funcs.client_close = cmsg_transport_tipc_client_close;
    transport->tport_funcs.server_close = cmsg_transport_tipc_server_close;
    transport->tport_funcs.s_socket = cmsg_transport_tipc_server_get_socket;
    transport->tport_funcs.c_socket = cmsg_transport_tipc_client_get_socket;
    transport->tport_funcs.client_destroy = cmsg_transport_tipc_client_destroy;
    transport->tport_funcs.server_destroy = cmsg_transport_tipc_server_destroy;
    transport->tport_funcs.is_congested = cmsg_transport_tipc_is_congested;
    transport->tport_funcs.send_can_block_enable =
        cmsg_transport_tipc_send_can_block_enable;
    transport->tport_funcs.ipfree_bind_enable = cmsg_transport_tipc_ipfree_bind_enable;
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


/**
 * Initialise the connection to the TIPC Topology Service.
 *
 * @return the file descriptor opened to receive topology event or -1 on failure.
 */
int
cmsg_tipc_topology_service_connect (void)
{
    struct sockaddr_tipc topo_server;
    size_t addr_len;
    int sock;

    addr_len = sizeof (topo_server);
    memset (&topo_server, 0, addr_len);

    /* setup the TIPC topology server's address {1,1} */
    topo_server.family = AF_TIPC;
    topo_server.addrtype = TIPC_ADDR_NAME;
    topo_server.addr.name.name.type = TIPC_TOP_SRV;
    topo_server.addr.name.name.instance = TIPC_TOP_SRV;

    /* create a socket to connect with */
    sock = socket (AF_TIPC, SOCK_SEQPACKET, 0);
    if (sock < 0)
    {
        CMSG_LOG_GEN_ERROR ("TIPC topology connect socket failure. Error:%s",
                            strerror (errno));
        return -1;
    }

    /* Connect to the TIPC topology server */
    if (connect (sock, (struct sockaddr *) &topo_server, addr_len) < 0)
    {
        CMSG_LOG_GEN_ERROR ("TIPC topology connect failure. Errno:%s", strerror (errno));
        close (sock);
        return -1;
    }

    return sock;
}


/**
 * Perform a TIPC Topology Subscription
 * The callback is stored in the subscription usr_handle so that when the event occurs
 * the appropriate callback can be made for the subscription.  This means the application
 * doesn't have to store a subscription.
 * @param sock a socket opened for TIPC Topology Service
 * @param server_name the server name to monitor
 * @param lower lower instance ID (stack ID) to monitor
 * @param upper upper instance ID (stack ID) to monitor
 * @return CMSG error code, CMSG_RET_OK for success, CMSG_RET_ERR for failure
 */
int
cmsg_tipc_topology_do_subscription (int sock, const char *server_name, uint32_t lower,
                                    uint32_t upper, cmsg_tipc_topology_callback callback)
{
    struct tipc_subscr subscr;
    size_t sub_len;
    int port;
    int ret;

    sub_len = sizeof (subscr);
    memset (&subscr, 0, sub_len);

    /* Check the parameters are valid */
    if (server_name == NULL)
    {
        CMSG_LOG_GEN_ERROR
            ("TIPC topology do subscription has no server name specified. Server name:%s, [%d,%d]",
             server_name, lower, upper);
        return CMSG_RET_ERR;
    }

    if (sock <= 0)
    {
        CMSG_LOG_GEN_ERROR
            ("TIPC topology do subscription has no socket specified. Server name:%s, [%d,%d]",
             server_name, lower, upper);
        return CMSG_RET_ERR;
    }

    port = cmsg_service_port_get (server_name, "tipc");
    if (port <= 0)
    {
        CMSG_LOG_GEN_ERROR
            ("TIPC topology do subscription couldn't determine port. Server name:%s, [%d,%d]",
             server_name, lower, upper);
        return CMSG_RET_ERR;
    }

    /*  Create TIPC topology subscription. This will listen for new publications of port
     *  names (each unit has a different port-name instance based on its stack member-ID) */
    subscr.timeout = TIPC_WAIT_FOREVER; /* don't timeout */
    subscr.seq.type = port;
    subscr.seq.lower = lower;           /* min member-ID */
    subscr.seq.upper = upper;           /* max member-ID */
    subscr.filter = TIPC_SUB_PORTS;     /* all publish/withdraws */
    memcpy (subscr.usr_handle, &callback, sizeof (cmsg_tipc_topology_callback));

    ret = send (sock, &subscr, sub_len, 0);
    if (ret < 0 || (uint32_t) ret != sub_len)
    {
        CMSG_LOG_GEN_ERROR
            ("TIPC topology do subscription send failure. Server name:%s, [%d,%d]. Error:%s",
             server_name, lower, upper, strerror (errno));
        return CMSG_RET_ERR;
    }

    CMSG_DEBUG (CMSG_INFO, "TIPC topo %s : successful (port=%u, sock=%d)", server_name,
                port, sock);
    return CMSG_RET_OK;
}


/**
 * Connects to the TIPC Topology Service and subscribes to the given server
 *
 * @return socket on success, -1 on failure
 */
int
cmsg_tipc_topology_connect_subscribe (const char *server_name, uint32_t lower,
                                      uint32_t upper, cmsg_tipc_topology_callback callback)
{
    int sock;
    int ret;

    sock = cmsg_tipc_topology_service_connect ();
    if (sock <= 0)
    {
        return -1;
    }

    ret = cmsg_tipc_topology_do_subscription (sock, server_name, lower, upper, callback);

    if (ret != CMSG_RET_OK)
    {
        close (sock);
        return -1;
    }

    return sock;
}


/**
 * Read TIPC Topology Service events
 * @param sock a socket opened for TIPC Topology Service
 * @param callback callback function to be called with each event received
 * @return 0 on normal operation or -1 on failure
 */
int
cmsg_tipc_topology_subscription_read (int sock, void *user_cb_data)
{
    struct tipc_event event;
    struct tipc_subscr *subscr;
    cmsg_tipc_topology_callback callback;
    int eventOk = 1;
    int ret;

    /* Read all awaiting TIPC topology events */
    while ((ret = recv (sock, &event, sizeof (event), MSG_DONTWAIT)) == sizeof (event))
    {
        /* Check the topology subscription event is valid */
        if (event.event != TIPC_PUBLISHED && event.event != TIPC_WITHDRAWN)
        {
            CMSG_DEBUG (CMSG_INFO, "TIPC topo : unknown topology event %d", event.event);
            eventOk = 0;
        }
        /* Check port instance advertised correlates to a single valid node ID */
        else if (event.found_lower != event.found_upper)
        {
            CMSG_DEBUG (CMSG_INFO, "TIPC topo : unknown node range %d-%d",
                        event.found_lower, event.found_upper);
            eventOk = 0;
        }

        /* Call the callback function to process the event */
        if (eventOk)
        {
            subscr = &event.s;
            memcpy (&callback, subscr->usr_handle, sizeof (cmsg_tipc_topology_callback));
            if (callback != NULL)
            {
                callback (&event, user_cb_data);
            }
        }
    }

    if (ret != sizeof (event) && errno != EAGAIN)
    {
        CMSG_LOG_GEN_ERROR ("TIPC topology subscription read failure. Error:%s",
                            strerror (errno));
        return CMSG_RET_ERR;
    }

    return CMSG_RET_OK;
}


/**
 * Print to tracelog a message describing the tipc event that is passed in to the
 * function. This includes if the event is published/withdrawn, and address information
 * about the connection that has changed,
 */
void
cmsg_tipc_topology_tracelog_tipc_event (const char *tracelog_string,
                                        const char *event_str, struct tipc_event *event)
{
    char display_string[150] = { 0 };
    uint32_t char_count = 0;

    char_count = sprintf (display_string, "%s Event: ", event_str);

    switch (event->event)
    {
    case TIPC_PUBLISHED:
        char_count += sprintf (&(display_string[char_count]), "Published: ");
        break;
    case TIPC_WITHDRAWN:
        char_count += sprintf (&(display_string[char_count]), "Withdrawn: ");
        break;
    case TIPC_SUBSCR_TIMEOUT:
        char_count += sprintf (&(display_string[char_count]), "Timeout: ");
        break;
    default:
        char_count += sprintf (&(display_string[char_count]), "Unknown, evt = %i ",
                               event->event);
        break;
    }

    char_count += sprintf (&(display_string[char_count]), " <%u,%u,%u> port id <%x:%u>",
                           event->s.seq.type, event->found_lower, event->found_upper,
                           event->port.node, event->port.ref);

    tracelog (tracelog_string, "%s", display_string);

    tracelog (tracelog_string, "Original Subscription:<%u,%u,%u>, timeout %u, user ref: "
              "%x%x%x%x%x%x%x%x",
              event->s.seq.type, event->s.seq.lower, event->s.seq.upper,
              event->s.timeout,
              ((uint8_t *) event->s.usr_handle)[0],
              ((uint8_t *) event->s.usr_handle)[1],
              ((uint8_t *) event->s.usr_handle)[2],
              ((uint8_t *) event->s.usr_handle)[3],
              ((uint8_t *) event->s.usr_handle)[4],
              ((uint8_t *) event->s.usr_handle)[5],
              ((uint8_t *) event->s.usr_handle)[6], ((uint8_t *) event->s.usr_handle)[7]);

    if (event->s.seq.type == 0)
    {
        tracelog (tracelog_string, " ...For node %x", event->found_lower);
    }
}
