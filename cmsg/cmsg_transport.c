/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#include "cmsg_private.h"
#include "cmsg_transport.h"
#include "cmsg_server.h"
#include "cmsg_error.h"
#include <arpa/inet.h>

extern void cmsg_transport_oneway_udt_init (cmsg_transport *transport);

static int32_t _cmsg_transport_server_recv (cmsg_recv_func recv, void *handle,
                                            cmsg_server *server, int peek);


/*
 * Given a transport, the "unique" id string of that
 * transport is constructed and written to the transport
 * id.
 *
 * In:      tport - transport
 * Return:  No return value.
 */
void
cmsg_transport_write_id (cmsg_transport *tport)
{
    if (tport == NULL)
    {
        return;
    }

    switch (tport->type)
    {
    case CMSG_TRANSPORT_RPC_TCP:
    case CMSG_TRANSPORT_ONEWAY_TCP:
        {
            char ip[INET6_ADDRSTRLEN] = { };
            uint16_t port;
            const char *fmt;
            if (tport->config.socket.family == PF_INET6)
            {
                int ip_len = 0;
                port = ntohs (tport->config.socket.sockaddr.in6.sin6_port);
                inet_ntop (tport->config.socket.sockaddr.generic.sa_family,
                           &(tport->config.socket.sockaddr.in6.sin6_addr), ip,
                           INET6_ADDRSTRLEN);
                // ipv6 addresses are enclosed in [] in URLs due to ambiguity of :s.
                fmt = ".tcp[[%s]:%d]";
            }
            else
            {
                port = ntohs (tport->config.socket.sockaddr.in.sin_port);
                inet_ntop (tport->config.socket.sockaddr.generic.sa_family,
                           &(tport->config.socket.sockaddr.in.sin_addr), ip,
                           INET6_ADDRSTRLEN);
                fmt = ".tcp[%s:%d]";
            }
            snprintf (tport->tport_id, CMSG_MAX_TPORT_ID_LEN, fmt, ip, port);
            break;
        }
    case CMSG_TRANSPORT_RPC_TIPC:
    case CMSG_TRANSPORT_ONEWAY_TIPC:
        {
            snprintf (tport->tport_id, CMSG_MAX_TPORT_ID_LEN, ".tipc[%02d]",
                      tport->config.socket.sockaddr.tipc.addr.name.name.instance);
            break;
        }
#ifdef HAVE_VCSTACK
    case CMSG_TRANSPORT_CPG:
        {
            // Potential for truncation of the CPG name
            // if maxlen < CPG_MAX_NAME_LENGTH (128).
            snprintf (tport->tport_id, CMSG_MAX_TPORT_ID_LEN, ".cpg[%s]",
                      tport->config.cpg.group_name.value);
            break;
        }
    case CMSG_TRANSPORT_BROADCAST:
        {
            snprintf (tport->tport_id, CMSG_MAX_TPORT_ID_LEN, ".tipcb");
            break;
        }
#endif
    case CMSG_TRANSPORT_ONEWAY_USERDEFINED:
        {
            strncpy (tport->tport_id, ".udt", CMSG_MAX_TPORT_ID_LEN);
            break;
        }

    case CMSG_TRANSPORT_LOOPBACK_ONEWAY:
        {
            strncpy (tport->tport_id, ".lpb", CMSG_MAX_TPORT_ID_LEN);
            break;
        }
    default:
        strncpy (tport->tport_id, ".unknown_transport", CMSG_MAX_TPORT_ID_LEN);
    }

    return;
}


cmsg_transport *
cmsg_transport_new (cmsg_transport_type type)
{
    cmsg_transport *transport = 0;
    transport = (cmsg_transport *) CMSG_CALLOC (1, sizeof (cmsg_transport));
    memset (transport, 0, sizeof (cmsg_transport));

    transport->type = type;

    switch (type)
    {
    case CMSG_TRANSPORT_RPC_TCP:
        cmsg_transport_tcp_init (transport);
        break;
    case CMSG_TRANSPORT_ONEWAY_TCP:
        cmsg_transport_oneway_tcp_init (transport);
        break;
    case CMSG_TRANSPORT_RPC_TIPC:
        cmsg_transport_tipc_init (transport);
        break;
    case CMSG_TRANSPORT_ONEWAY_TIPC:
        cmsg_transport_oneway_tipc_init (transport);
        break;
#ifdef HAVE_VCSTACK
    case CMSG_TRANSPORT_CPG:
        cmsg_transport_cpg_init (transport);
        break;
    case CMSG_TRANSPORT_BROADCAST:
        cmsg_transport_tipc_broadcast_init (transport);
        break;
#endif
    case CMSG_TRANSPORT_ONEWAY_USERDEFINED:
        cmsg_transport_oneway_udt_init (transport);
        break;

    case CMSG_TRANSPORT_LOOPBACK_ONEWAY:
        cmsg_transport_oneway_loopback_init (transport);
        break;

    default:
        CMSG_LOG_GEN_ERROR ("Transport type not supported. Type:%d", transport->type);
        CMSG_FREE (transport);
        transport = 0;
    }

    transport->client_send_tries = 0;

    return transport;
}

int32_t
cmsg_transport_destroy (cmsg_transport *transport)
{
    if (transport)
    {
        CMSG_FREE (transport);
        transport = 0;
        return 0;
    }
    else
    {
        return 1;
    }
}


/* Receive message from server and process it. If 'peek' is set, then a header
 * is read first with MSG_PEEK and then read all together (header + data). */
static int32_t
_cmsg_transport_server_recv (cmsg_recv_func recv, void *handle, cmsg_server *server,
                             int peek)
{
    int32_t ret = CMSG_RET_OK;
    int nbytes = 0;
    uint32_t dyn_len = 0;
    cmsg_header header_received;
    cmsg_header header_converted;
    cmsg_server_request server_request;
    uint8_t *buffer = 0;
    uint8_t buf_static[512];
    uint8_t *buffer_data;
    uint32_t extra_header_size = 0;

    CMSG_DEBUG (CMSG_INFO,
                "[TRANSPORT] server->accecpted_client_socket %d\n",
                server->connection.sockets.client_socket);

    if (peek)
    {
        nbytes = recv (handle, &header_received, sizeof (cmsg_header), MSG_PEEK);
    }
    else
    {
        nbytes = recv (handle, &header_received, sizeof (cmsg_header), MSG_WAITALL);
    }
    if (nbytes == (int) sizeof (cmsg_header))
    {
        CMSG_PROF_TIME_TIC (&server->prof);

        if (cmsg_header_process (&header_received, &header_converted) != CMSG_RET_OK)
        {
            // Couldn't process the header for some reason
            CMSG_LOG_SERVER_ERROR (server,
                                   "Unable to process message header for server recv. Bytes: %d",
                                   nbytes);
            return CMSG_RET_ERR;
        }

        // Header is good so make use of it.
        server_request.msg_type = header_converted.msg_type;
        server_request.message_length = header_converted.message_length;
        server_request.method_index = UNDEFINED_METHOD;
        memset (&(server_request.method_name_recvd), 0,
                CMSG_SERVER_REQUEST_MAX_NAME_LENGTH);

        extra_header_size = header_converted.header_length - sizeof (cmsg_header);

        if (peek)
        {
            // packet size is determined by header_length + message_length.
            // header_length may be greater than sizeof (cmsg_header)
            dyn_len = header_converted.message_length + header_converted.header_length;
        }
        else
        {
            // Make sure any extra header is received.
            dyn_len = header_converted.message_length + extra_header_size;
        }

        if (dyn_len > sizeof (buf_static))
        {
            buffer = (uint8_t *) CMSG_CALLOC (1, dyn_len);
        }
        else
        {
            buffer = (uint8_t *) buf_static;
            memset (buffer, 0, sizeof (buf_static));
        }

        // read the message
        if (peek)
        {
            nbytes = recv (handle, buffer, dyn_len, MSG_WAITALL);
            buffer_data = buffer + sizeof (cmsg_header);
        }
        else
        {
            //Even if no packed data, TLV header should be read.
            if (header_converted.message_length + extra_header_size)
            {
                nbytes = recv (handle, buffer, dyn_len, MSG_WAITALL);
            }
            else
            {
                nbytes = 0;
            }
            buffer_data = buffer;
        }

        CMSG_PROF_TIME_LOG_ADD_TIME (&server->prof, "receive",
                                     cmsg_prof_time_toc (&server->prof));

        ret = cmsg_tlv_header_process (buffer_data, &server_request, extra_header_size,
                                       server->service->descriptor);

        if (ret != CMSG_RET_OK)
        {
            if (ret == CMSG_RET_METHOD_NOT_FOUND)
            {
                cmsg_server_empty_method_reply_send (server,
                                                     CMSG_STATUS_CODE_SERVER_METHOD_NOT_FOUND,
                                                     UNDEFINED_METHOD);
            }
        }
        else
        {

            buffer_data = buffer_data + extra_header_size;
            // Process any message that has no more length or we have received what
            // we expected to from the socket
            if (header_converted.message_length == 0 || nbytes == (int) dyn_len)
            {
                CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] received data\n");
                cmsg_buffer_print (buffer_data, dyn_len);
                server->server_request = &server_request;
                if (server->message_processor (server, buffer_data) != CMSG_RET_OK)
                {
                    CMSG_LOG_SERVER_ERROR (server,
                                           "Server message processing returned an error.");
                }

            }
            else
            {
                CMSG_LOG_SERVER_ERROR (server, "No data on recv socket %d.",
                                       server->connection.sockets.client_socket);

                ret = CMSG_RET_ERR;
            }
        }
        if (buffer != buf_static)
        {
            if (buffer)
            {
                CMSG_FREE (buffer);
                buffer = 0;
            }
        }

    }
    else if (nbytes > 0)
    {
        CMSG_LOG_SERVER_ERROR (server, "Bad header on recv socket %d. Number: %d",
                               server->connection.sockets.client_socket, nbytes);

        // TEMP to keep things going
        buffer = (uint8_t *) CMSG_CALLOC (1, nbytes);
        nbytes = recv (handle, buffer, nbytes, MSG_WAITALL);
        CMSG_FREE (buffer);
        buffer = 0;
        ret = CMSG_RET_OK;
    }
    else if (nbytes == 0)
    {
        //Normal socket shutdown case. Return other than TRANSPORT_OK to
        //have socket removed from select set.
        ret = CMSG_RET_CLOSED;
    }
    else
    {
        if (errno != ECONNRESET)
        {
            CMSG_LOG_SERVER_ERROR (server, "Receive error for socket %d. Error: %s.",
                                   server->connection.sockets.client_socket,
                                   strerror (errno));
        }

        ret = CMSG_RET_ERR;
    }

    return ret;
}


/* Receive message from server and process it */
int32_t
cmsg_transport_server_recv (cmsg_recv_func recv, void *handle, cmsg_server *server)
{
    return _cmsg_transport_server_recv (recv, handle, server, FALSE);
}


/* Receive message from server (by peeking the header first) and process it */
int32_t
cmsg_transport_server_recv_with_peek (cmsg_recv_func recv, void *handle,
                                      cmsg_server *server)
{
    return _cmsg_transport_server_recv (recv, handle, server, TRUE);
}


/**
 *  Configure the transport to be safe when using the send functionality from multiple threads.
 *
 */
int32_t
cmsg_transport_send_called_multi_threads_enable (cmsg_transport *transport,
                                                 uint32_t enable_multi_threaded_send_safe)
{

    return transport->send_called_multi_threads_enable (transport,
                                                        enable_multi_threaded_send_safe);
}

/**
 * Configure the transport to allow blocking if send cannot send it straight away
 *
 */
int32_t
cmsg_transport_send_can_block_enable (cmsg_transport *transport, uint32_t send_can_block)
{
    return transport->send_can_block_enable (transport, send_can_block);
}

/**
 * Configure the transport to allow listening socket to bind on non-existent, non-local
 * IPv6 addresses. This is specifically added to resolve IPv6 DAD race conditions.
 *
 */
int32_t
cmsg_transport_ipfree_bind_enable (cmsg_transport *transport,
                                   cmsg_bool_t ipfree_bind_enable)
{
    return transport->ipfree_bind_enable (transport, ipfree_bind_enable);
}
