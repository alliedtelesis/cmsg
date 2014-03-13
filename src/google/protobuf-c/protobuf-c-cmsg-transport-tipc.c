#include "protobuf-c-cmsg-transport.h"
#include "protobuf-c-cmsg-client.h"
#include "protobuf-c-cmsg-server.h"


/**
 * Create a TIPC socket connection.
 * Returns 0 on success or a negative integer on failure.
 */
static int32_t
cmsg_transport_tipc_connect (cmsg_client *client)
{
    int ret;

    DEBUG (CMSG_INFO, "[TRANSPORT] cmsg_transport_tipc_connect\n");

    if (client == NULL)
        return 0;

    client->connection.socket = socket (client->_transport->config.socket.family,
                                        SOCK_STREAM, 0);

    if (client->connection.socket < 0)
    {
        ret = -errno;
        client->state = CMSG_CLIENT_STATE_FAILED;
        CMSG_LOG_DEBUG ("[TRANSPORT] error creating socket: %s", strerror (errno));

        return ret;
    }

    if (client->parent.object_type == CMSG_OBJ_TYPE_PUB)
    {
        int tipc_timeout = CMSG_TRANSPORT_TIPC_PUB_CONNECT_TIMEOUT;
        setsockopt (client->connection.socket, SOL_TIPC, TIPC_CONN_TIMEOUT, &tipc_timeout,
                    sizeof (int));
    }

    if (connect (client->connection.socket,
                 (struct sockaddr *) &client->_transport->config.socket.sockaddr.tipc,
                 sizeof (client->_transport->config.socket.sockaddr.tipc)) < 0)
    {
        ret = -errno;
        CMSG_LOG_DEBUG ("[TRANSPORT] error connecting to remote host (port %d inst %d): %s",
                        client->_transport->config.socket.sockaddr.tipc.addr.name.name.type,
                        client->_transport->config.socket.sockaddr.tipc.addr.name.name.instance,
                        strerror (errno));

        shutdown (client->connection.socket, SHUT_RDWR);
        close (client->connection.socket);
        client->connection.socket = -1;
        client->state = CMSG_CLIENT_STATE_FAILED;

        return ret;
    }
    else
    {
        client->state = CMSG_CLIENT_STATE_CONNECTED;
        DEBUG (CMSG_INFO, "[TRANSPORT] successfully connected\n");
        return 0;
    }
}



static int32_t
cmsg_transport_tipc_listen (cmsg_server *server)
{
    int32_t yes = 1;    // for setsockopt() SO_REUSEADDR, below
    int32_t listening_socket = -1;
    int32_t ret = 0;
    socklen_t addrlen = 0;
    cmsg_transport *transport = NULL;

    if (server == NULL)
        return 0;

    server->connection.sockets.listening_socket = 0;
    server->connection.sockets.client_socket = 0;

    transport = server->_transport;
    listening_socket = socket (transport->config.socket.family, SOCK_STREAM, 0);
    if (listening_socket == -1)
    {
        CMSG_LOG_ERROR ("[TRANSPORT] socket failed with: %s", strerror (errno));
        return -1;
    }

    ret = setsockopt (listening_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof (int32_t));
    if (ret == -1)
    {
        CMSG_LOG_ERROR ("[TRANSPORT] setsockopt failed with: %s", strerror (errno));
        close (listening_socket);
        return -1;
    }

    addrlen = sizeof (transport->config.socket.sockaddr.tipc);

    ret = bind (listening_socket,
                (struct sockaddr *) &transport->config.socket.sockaddr.tipc, addrlen);
    if (ret < 0)
    {
        CMSG_LOG_ERROR ("[TRANSPORT] bind failed with: %s", strerror (errno));
        close (listening_socket);
        return -1;
    }

    ret = listen (listening_socket, 10);
    if (ret < 0)
    {
        CMSG_LOG_ERROR ("[TRANSPORT] listen failed with: %s", strerror (errno));
        close (listening_socket);
        return -1;
    }

    server->connection.sockets.listening_socket = listening_socket;

    DEBUG (CMSG_INFO, "[TRANSPORT] listening on tipc socket: %d\n", listening_socket);

    DEBUG (CMSG_INFO,
           "[TRANSPORT] listening on tipc type: %d\n",
           server->_transport->config.socket.sockaddr.tipc.addr.name.name.type);

    DEBUG (CMSG_INFO,
           "[TRANSPORT] listening on tipc instance: %d\n",
           server->_transport->config.socket.sockaddr.tipc.addr.name.name.instance);

    DEBUG (CMSG_INFO,
           "[TRANSPORT] listening on tipc domain: %d\n",
           server->_transport->config.socket.sockaddr.tipc.addr.name.domain);

    DEBUG (CMSG_INFO,
           "[TRANSPORT] listening on tipc scope: %d\n",
           server->_transport->config.socket.sockaddr.tipc.scope);

    return 0;
}


/* Wrapper function to call "recv" on a TIPC socket */
int
cmsg_transport_tipc_recv (void *handle, void *buff, int len, int flags)
{
    int *sock = (int *) handle;

    return recv (*sock, buff, len, flags);
}


static int32_t
cmsg_transport_tipc_server_recv (int32_t server_socket, cmsg_server *server)
{
    int32_t ret = 0;

    if (!server || server_socket < 0)
    {
        CMSG_LOG_ERROR ("[TRANSPORT] bad parameter server %p socket %d",
                        server, server_socket);
        return -1;
    }
    DEBUG (CMSG_INFO, "[TRANSPORT] socket %d\n", server_socket);


    /* Remember the client socket to use when send reply */
    server->connection.sockets.client_socket = server_socket;

    ret = cmsg_transport_server_recv (cmsg_transport_tipc_recv, (void *) &server_socket,
                                      server);

    return ret;
}

static int32_t
cmsg_transport_tipc_server_accept (int32_t listen_socket, cmsg_server *server)
{
    uint32_t client_len;
    cmsg_transport client_transport;
    int sock;

    if (!server || listen_socket < 0)
    {
        return -1;
    }

    client_len = sizeof (client_transport.config.socket.sockaddr.tipc);
    sock = accept (listen_socket,
                   (struct sockaddr *) &client_transport.config.socket.sockaddr.tipc,
                   &client_len);

    if (sock < 0)
    {
        DEBUG (CMSG_ERROR, "[TRANSPORT] accept failed\n");
        DEBUG (CMSG_INFO, "[TRANSPORT] sock = %d\n", sock);

        return -1;
    }

    return sock;
}


static cmsg_status_code
cmsg_transport_tipc_client_recv (cmsg_client *client, ProtobufCMessage **messagePtPt)
{
    int nbytes = 0;
    uint32_t dyn_len = 0;
    cmsg_header header_received;
    cmsg_header header_converted;
    uint8_t *recv_buffer = 0;
    uint8_t *buffer = 0;
    uint8_t buf_static[512];
    const ProtobufCMessageDescriptor *desc;
    uint32_t extra_header_size;
    cmsg_server_request server_request;

    *messagePtPt = NULL;

    if (!client)
    {
        return CMSG_STATUS_CODE_SERVICE_FAILED;
    }

    CMSG_PROF_TIME_TIC (&client->prof);

    nbytes = recv (client->connection.socket,
                   &header_received, sizeof (cmsg_header), MSG_WAITALL);
    CMSG_PROF_TIME_LOG_ADD_TIME (&client->prof, "receive",
                                 cmsg_prof_time_toc (&client->prof));
    CMSG_PROF_TIME_TIC (&client->prof);

    if (nbytes == (int) sizeof (cmsg_header))
    {
        if (cmsg_header_process (&header_received, &header_converted) != CMSG_RET_OK)
        {
            // Couldn't process the header for some reason
            CMSG_LOG_ERROR ("[TRANSPORT] server receive couldn't process msg header");
            CMSG_PROF_TIME_LOG_ADD_TIME (&client->prof, "unpack",
                                         cmsg_prof_time_toc (&client->prof));
            return CMSG_STATUS_CODE_SERVICE_FAILED;
        }

        DEBUG (CMSG_INFO, "[TRANSPORT] received response header\n");

        // read the message

        // There is no more data to read so exit.
        if (header_converted.message_length == 0)
        {
            // May have been queued, dropped or there was no message returned
            DEBUG (CMSG_INFO,
                   "[TRANSPORT] received response without data. server status %d\n",
                   header_converted.status_code);
            CMSG_PROF_TIME_LOG_ADD_TIME (&client->prof, "unpack",
                                         cmsg_prof_time_toc (&client->prof));
            return header_converted.status_code;
        }

        // Take into account that someone may have changed the size of the header
        // and we don't know about it, make sure we receive all the information.
        dyn_len = header_converted.message_length +
            header_converted.header_length - sizeof (cmsg_header);

        if (dyn_len > sizeof (buf_static))
        {
            recv_buffer = (uint8_t *) CMSG_CALLOC (1, dyn_len);
        }
        else
        {
            recv_buffer = (uint8_t *) buf_static;
            memset (recv_buffer, 0, sizeof (buf_static));
        }

        //just recv the rest of the data to clear the socket
        nbytes = recv (client->connection.socket, recv_buffer, dyn_len, MSG_WAITALL);

        if (nbytes == (int) dyn_len)
        {
            extra_header_size = header_converted.header_length - sizeof (cmsg_header);
            // Set buffer to take into account a larger header than we expected
            buffer = recv_buffer;

            cmsg_tlv_header_process (buffer, &server_request, extra_header_size,
                                     client->descriptor);

            buffer = buffer + extra_header_size;
            DEBUG (CMSG_INFO, "[TRANSPORT] received response data\n");
            cmsg_buffer_print (buffer, dyn_len);

            //todo: call cmsg_client_response_message_processor
            ProtobufCMessage *message = 0;
            ProtobufCAllocator *allocator = (ProtobufCAllocator *) client->allocator;

            DEBUG (CMSG_INFO, "[TRANSPORT] unpacking response message\n");

            desc = client->descriptor->methods[server_request.method_index].output;
            message = protobuf_c_message_unpack (desc, allocator,
                                                 header_converted.message_length,
                                                 buffer);

            // Free the allocated buffer
            if (recv_buffer != (void *) buf_static)
            {
                if (recv_buffer)
                {
                    CMSG_FREE (recv_buffer);
                    recv_buffer = 0;
                }
            }

            // Msg not unpacked correctly
            if (message == NULL)
            {
                CMSG_LOG_ERROR ("[TRANSPORT] error unpacking response message\n");
                CMSG_PROF_TIME_LOG_ADD_TIME (&client->prof, "unpack",
                                             cmsg_prof_time_toc (&client->prof));
                return CMSG_STATUS_CODE_SERVICE_FAILED;
            }
            *messagePtPt = message;
            CMSG_PROF_TIME_LOG_ADD_TIME (&client->prof, "unpack",
                                         cmsg_prof_time_toc (&client->prof));
            return CMSG_STATUS_CODE_SUCCESS;
        }
        else
        {
            CMSG_LOG_ERROR ("[TRANSPORT] recv socket %d no data, dyn_len %d",
                            client->connection.socket, dyn_len);

        }
        if (recv_buffer != (void *) buf_static)
        {
            if (recv_buffer)
            {
                CMSG_FREE (recv_buffer);
                recv_buffer = 0;
            }
        }
    }
    else if (nbytes > 0)
    {
        /* Didn't receive all of the CMSG header.
         */
        CMSG_LOG_ERROR ("[TRANSPORT] recv socket %d bad header nbytes %d\n",
                        client->connection.socket, nbytes);

        // TEMP to keep things going
        recv_buffer = (uint8_t *) CMSG_CALLOC (1, nbytes);
        nbytes = recv (client->connection.socket, recv_buffer, nbytes, MSG_WAITALL);
        CMSG_FREE (recv_buffer);
        recv_buffer = 0;
    }
    else if (nbytes == 0)
    {
        //Normal socket shutdown case. Return other than TRANSPORT_OK to
        //have socket removed from select set.
    }
    else
    {
        if (errno == ECONNRESET)
        {
            DEBUG (CMSG_INFO, "[TRANSPORT] recv socket %d error: %s\n",
                   client->connection.socket, strerror (errno));
            return CMSG_STATUS_CODE_SERVER_CONNRESET;
        }
        else
        {
            CMSG_LOG_ERROR ("[TRANSPORT] recv socket %d error: %s\n",
                            client->connection.socket, strerror (errno));
        }
    }

    CMSG_PROF_TIME_LOG_ADD_TIME (&client->prof, "unpack",
                                 cmsg_prof_time_toc (&client->prof));
    return CMSG_STATUS_CODE_SERVICE_FAILED;
}


static int32_t
cmsg_transport_tipc_client_send (cmsg_client *client, void *buff, int length, int flag)
{
    return (send (client->connection.socket, buff, length, flag));
}

static int32_t
cmsg_transport_tipc_rpc_server_send (cmsg_server *server, void *buff, int length, int flag)
{
    return (send (server->connection.sockets.client_socket, buff, length, flag));
}

/**
 * TIPC oneway servers do not send replies to received messages. This function therefore
 * returns 0.
 */
static int32_t
cmsg_transport_tipc_oneway_server_send (cmsg_server *server, void *buff, int length, int flag)
{
    return 0;
}

static void
cmsg_transport_tipc_client_close (cmsg_client *client)
{
    if (client->connection.socket != -1)
    {
        DEBUG (CMSG_INFO, "[TRANSPORT] shutting down socket\n");
        shutdown (client->connection.socket, SHUT_RDWR);

        DEBUG (CMSG_INFO, "[TRANSPORT] closing socket\n");
        close (client->connection.socket);

        client->connection.socket = -1;
    }

}

static void
cmsg_transport_tipc_server_close (cmsg_server *server)
{
    DEBUG (CMSG_INFO, "[SERVER] shutting down socket\n");
    shutdown (server->connection.sockets.client_socket, SHUT_RDWR);

    DEBUG (CMSG_INFO, "[SERVER] closing socket\n");
    close (server->connection.sockets.client_socket);
}


static int
cmsg_transport_tipc_server_get_socket (cmsg_server *server)
{
    return server->connection.sockets.listening_socket;
}


static int
cmsg_transport_tipc_client_get_socket (cmsg_client *client)
{
    return client->connection.socket;
}

static void
cmsg_transport_tipc_client_destroy (cmsg_client *cmsg_client)
{
    //placeholder to make sure destroy functions are called in the right order
}

static void
cmsg_transport_tipc_server_destroy (cmsg_server *server)
{
    DEBUG (CMSG_INFO, "[SERVER] Shutting down listening socket\n");
    shutdown (server->connection.sockets.listening_socket, SHUT_RDWR);

    DEBUG (CMSG_INFO, "[SERVER] Closing listening socket\n");
    close (server->connection.sockets.listening_socket);
}


/**
 * TIPC is never congested
 */
uint32_t
cmsg_transport_tipc_is_congested (cmsg_client *client)
{
    return FALSE;
}


int32_t
cmsg_transport_tipc_send_called_multi_threads_enable (cmsg_transport *transport,
                                                      uint32_t enable)
{
    // Don't support sending from multiple threads
    return -1;
}


int32_t
cmsg_transport_tipc_send_can_block_enable (cmsg_transport *transport,
                                           uint32_t send_can_block)
{
    transport->send_can_block = send_can_block;
    return 0;
}


void
cmsg_transport_tipc_init (cmsg_transport *transport)
{
    if (transport == NULL)
        return;

    transport->config.socket.family = PF_TIPC;
    transport->config.socket.sockaddr.generic.sa_family = PF_TIPC;
    transport->connect = cmsg_transport_tipc_connect;
    transport->listen = cmsg_transport_tipc_listen;
    transport->server_accept = cmsg_transport_tipc_server_accept;
    transport->server_recv = cmsg_transport_tipc_server_recv;
    transport->client_recv = cmsg_transport_tipc_client_recv;
    transport->client_send = cmsg_transport_tipc_client_send;
    transport->server_send = cmsg_transport_tipc_rpc_server_send;
    transport->closure = cmsg_server_closure_rpc;
    transport->invoke = cmsg_client_invoke_rpc;
    transport->client_close = cmsg_transport_tipc_client_close;
    transport->server_close = cmsg_transport_tipc_server_close;

    transport->s_socket = cmsg_transport_tipc_server_get_socket;
    transport->c_socket = cmsg_transport_tipc_client_get_socket;

    transport->client_destroy = cmsg_transport_tipc_client_destroy;
    transport->server_destroy = cmsg_transport_tipc_server_destroy;

    transport->is_congested = cmsg_transport_tipc_is_congested;
    transport->send_called_multi_threads_enable = cmsg_transport_tipc_send_called_multi_threads_enable;
    transport->send_called_multi_enabled = FALSE;
    transport->send_can_block_enable = cmsg_transport_tipc_send_can_block_enable;

    DEBUG (CMSG_INFO, "%s: done\n", __FUNCTION__);
}

void
cmsg_transport_oneway_tipc_init (cmsg_transport *transport)
{
    if (transport == NULL)
        return;

    transport->config.socket.family = PF_TIPC;
    transport->config.socket.sockaddr.generic.sa_family = PF_TIPC;

    transport->connect = cmsg_transport_tipc_connect;
    transport->listen = cmsg_transport_tipc_listen;
    transport->server_accept = cmsg_transport_tipc_server_accept;
    transport->server_recv = cmsg_transport_tipc_server_recv;
    transport->client_recv = cmsg_transport_tipc_client_recv;
    transport->client_send = cmsg_transport_tipc_client_send;
    transport->server_send = cmsg_transport_tipc_oneway_server_send;
    transport->closure = cmsg_server_closure_oneway;
    transport->invoke = cmsg_client_invoke_oneway;
    transport->client_close = cmsg_transport_tipc_client_close;
    transport->server_close = cmsg_transport_tipc_server_close;

    transport->s_socket = cmsg_transport_tipc_server_get_socket;
    transport->c_socket = cmsg_transport_tipc_client_get_socket;

    transport->client_destroy = cmsg_transport_tipc_client_destroy;
    transport->server_destroy = cmsg_transport_tipc_server_destroy;

    transport->is_congested = cmsg_transport_tipc_is_congested;
    transport->send_called_multi_threads_enable = cmsg_transport_tipc_send_called_multi_threads_enable;
    transport->send_called_multi_enabled = FALSE;
    transport->send_can_block_enable = cmsg_transport_tipc_send_can_block_enable;

    DEBUG (CMSG_INFO, "%s: done\n", __FUNCTION__);
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
        CMSG_LOG_ERROR ("Unknown TIPC service %s", server_name);
        return NULL;
    }

    transport = cmsg_transport_new (transport_type);
    if (transport == NULL)
    {
        CMSG_LOG_ERROR ("No TIPC transport for %d", member_id);
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
        CMSG_LOG_ERROR ("TIPC topo : socket failure (errno=%d)", errno);
        return -1;
    }

    /* Connect to the TIPC topology server */
    if (connect (sock, (struct sockaddr *) &topo_server, addr_len) < 0)
    {
        CMSG_LOG_ERROR ("TIPC topo : connect failure (errno=%d)", errno);
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
cmsg_tipc_topology_do_subscription (int sock, const char *server_name, uint32_t lower, uint32_t upper,
                                    cmsg_tipc_topology_callback callback)
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
        CMSG_LOG_ERROR ("TIPC topo : no server name specified");
        return CMSG_RET_ERR;
    }

    if (sock <= 0)
    {
        CMSG_LOG_ERROR ("TIPC topo %s : no socket specified", server_name);
        return CMSG_RET_ERR;
    }

    port = cmsg_service_port_get (server_name, "tipc");
    if (port <= 0)
    {
        CMSG_LOG_ERROR ("TIPC topo %s : couldn't determine port", server_name);
        return CMSG_RET_ERR;
    }

    /*  Create TIPC topology subscription. This will listen for new publications of port
     *  names (each unit has a different port-name instance based on its stack member-ID) */
    subscr.timeout = TIPC_WAIT_FOREVER; /* don't timeout */
    subscr.seq.type = port;
    subscr.seq.lower = lower;   /* min member-ID */
    subscr.seq.upper = upper;   /* max member-ID */
    subscr.filter = TIPC_SUB_PORTS;     /* all publish/withdraws */
    memcpy (subscr.usr_handle, &callback, sizeof (cmsg_tipc_topology_callback));

    ret = send (sock, &subscr, sub_len, 0);
    if (ret < 0 || (uint32_t)ret != sub_len)
    {
        CMSG_LOG_ERROR ("TIPC topo %s : send failure (errno=%d)", server_name, errno);
        return CMSG_RET_ERR;
    }

    DEBUG (CMSG_INFO, "TIPC topo %s : successful (port=%u, sock=%d)", server_name,
           port, sock);
    return CMSG_RET_OK;
}


/**
 * Connects to the TIPC Topology Service and subscribes to the given server
 *
 * @return socket on success, -1 on failure
 */
int
cmsg_tipc_topology_connect_subscribe (const char *server_name, uint32_t lower, uint32_t upper,
                                    cmsg_tipc_topology_callback callback)
{
    int sock;
    int ret;

    sock = cmsg_tipc_topology_service_connect ();
    if (sock <= 0 )
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
cmsg_tipc_topology_subscription_read (int sock)
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
            DEBUG (CMSG_INFO, "TIPC topo : unknown topology event %d", event.event);
            eventOk = 0;
        }
        /* Check port instance advertised correlates to a single valid node ID */
        else if (event.found_lower != event.found_upper)
        {
            DEBUG (CMSG_INFO, "TIPC topo : unknown node range %d-%d", event.found_lower,
                   event.found_upper);
            eventOk = 0;
        }

        /* Call the callback function to process the event */
        if (eventOk)
        {
            subscr = &event.s;
            memcpy (&callback, subscr->usr_handle, sizeof (cmsg_tipc_topology_callback));
            if (callback != NULL)
            {
                callback (&event);
            }
        }
    }

    if (ret != sizeof (event) && errno != EAGAIN)
    {
        CMSG_LOG_ERROR ("TIPC topo : Failed to receive event (errno=%d)", errno);
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
              ((uint8_t *) event->s.usr_handle)[6],
              ((uint8_t *) event->s.usr_handle)[7]);

    if (event->s.seq.type == 0)
    {
        tracelog (tracelog_string, " ...For node %x", event->found_lower);
    }
}
