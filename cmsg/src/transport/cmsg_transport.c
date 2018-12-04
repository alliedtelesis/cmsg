/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#include "cmsg_private.h"
#include "cmsg_transport.h"
#include "cmsg_transport_private.h"
#include "cmsg_error.h"
#include <arpa/inet.h>

/**
 * Get the transport ID to use in the CMSG counters application
 * name. This simply returns the transport ID of the transport except
 * in the case of unix transports where we always return "unix". This
 * is to ensure we don't run out of counterd applications as unix
 * transports use the PID of the process in their transport ID. If
 * there are a large amount of transient processes that use CMSG then
 * we sooner or later run out of counterd applications.
 *
 * @param transport - Transport to get the transport ID from.
 *
 * @returns the string to use as the transport ID in a counterd
 *          application name.
 */
const char *
cmsg_transport_counter_app_tport_id (cmsg_transport *transport)
{
    if (transport->type == CMSG_TRANSPORT_RPC_UNIX ||
        transport->type == CMSG_TRANSPORT_ONEWAY_UNIX)
    {
        return ".unix";
    }

    return transport->tport_id;
}


/*
 * Given a transport, the "unique" id string of that
 * transport is constructed and written to the transport
 * id.
 *
 * In:      tport - transport
 *          parent_obj_id - object id of the parent using the transport
 * Return:  No return value.
 */
void
cmsg_transport_write_id (cmsg_transport *tport, const char *parent_obj_id)
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
    case CMSG_TRANSPORT_BROADCAST:
        {
            snprintf (tport->tport_id, CMSG_MAX_TPORT_ID_LEN, ".tipcb");
            break;
        }

    case CMSG_TRANSPORT_ONEWAY_USERDEFINED:
        {
            strncpy (tport->tport_id, ".udt", CMSG_MAX_TPORT_ID_LEN);
            break;
        }

    case CMSG_TRANSPORT_LOOPBACK:
        {
            strncpy (tport->tport_id, ".lpb", CMSG_MAX_TPORT_ID_LEN);
            break;
        }

    case CMSG_TRANSPORT_RPC_UNIX:
    case CMSG_TRANSPORT_ONEWAY_UNIX:
        {
            snprintf (tport->tport_id, CMSG_MAX_TPORT_ID_LEN, "%s",
                      tport->config.socket.sockaddr.un.sun_path);
            break;
        }

    default:
        strncpy (tport->tport_id, ".unknown_transport", CMSG_MAX_TPORT_ID_LEN);
    }

    strncpy (tport->parent_obj_id, parent_obj_id, CMSG_MAX_OBJ_ID_LEN);

    return;
}


cmsg_transport *
cmsg_transport_new (cmsg_transport_type type)
{
    cmsg_transport *transport = NULL;
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
    case CMSG_TRANSPORT_BROADCAST:
        cmsg_transport_tipc_broadcast_init (transport);
        break;
    case CMSG_TRANSPORT_ONEWAY_USERDEFINED:
    case CMSG_TRANSPORT_RPC_USERDEFINED:
        cmsg_transport_udt_init (transport);
        break;

    case CMSG_TRANSPORT_LOOPBACK:
        cmsg_transport_loopback_init (transport);
        break;

    case CMSG_TRANSPORT_ONEWAY_UNIX:
        cmsg_transport_oneway_unix_init (transport);
        break;
    case CMSG_TRANSPORT_RPC_UNIX:
        cmsg_transport_rpc_unix_init (transport);
        break;

    default:
        CMSG_LOG_GEN_ERROR ("Transport type not supported. Type:%d", transport->type);
        CMSG_FREE (transport);
        transport = NULL;
    }

    if (transport)
    {
        transport->socket = -1;
    }

    return transport;
}

int32_t
cmsg_transport_destroy (cmsg_transport *transport)
{
    if (transport)
    {
        CMSG_FREE (transport);
        return 0;
    }
    else
    {
        return 1;
    }
}

/**
 * Poll for the header data and give up if we timeout. This is used to avoid
 * blocking forever on the receive if the data is never sent or is partially sent.
 *
 * @param recv_wrapper - The transport specific receive function to use with a socket.
 * @param transport - The transport that is doing the peeking.
 * @param socket - The socket to peek the data off.
 * @param seconds_to_wait - The number of seconds to wait before timing out and giving up.
 * @param header_received - Pointer to a header structure to return the peeked header in.
 *
 * @returns CMSG_STATUS_CODE_SUCCESS on success, the related cmsg_status_code otherwise.
 */
cmsg_status_code
cmsg_transport_peek_for_header (cmsg_recv_func recv_wrapper, cmsg_transport *transport,
                                int32_t socket, time_t seconds_to_wait,
                                cmsg_header *header_received)
{
    cmsg_status_code ret = CMSG_STATUS_CODE_SUCCESS;
    int nbytes = 0;
    bool timed_out = false;
    time_t seconds_waited = 0;
    struct timeval start;
    struct timeval current;

    gettimeofday (&start, NULL);

    /* Peek until data arrives. This allows us to timeout and recover if no data arrives. */
    while (!timed_out)
    {
        nbytes = recv_wrapper (transport, socket, header_received,
                               sizeof (cmsg_header), MSG_PEEK | MSG_DONTWAIT);
        if (nbytes == (int) sizeof (cmsg_header))
        {
            break;
        }
        else
        {
            if (nbytes == 0)
            {
                return CMSG_STATUS_CODE_CONNECTION_CLOSED;
            }
            else if (nbytes == -1)
            {
                if (errno == ECONNRESET)
                {
                    CMSG_DEBUG (CMSG_INFO,
                                "[TRANSPORT] receive failed %d %s", nbytes,
                                strerror (errno));
                    return CMSG_STATUS_CODE_SERVER_CONNRESET;
                }
                else if (errno == EINTR)
                {
                    // We were interrupted, this is transient so just try again without a delay.
                    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] receive interrupted %d %s",
                                nbytes, strerror (errno));
                    continue;
                }
                else if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    // This is normal, sometimes the data is not ready, just wait and try again.
                    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] receive data not ready");
                }
                else
                {
                    // This was unexpected, try again after a delay.
                    CMSG_LOG_TRANSPORT_ERROR (transport, "Receive failed %d %s",
                                              nbytes, strerror (errno));
                }
            }
        }

        gettimeofday (&current, NULL);
        seconds_waited = current.tv_sec - start.tv_sec;
        if (seconds_waited > seconds_to_wait)
        {
            timed_out = true;
        }
        else
        {
            /* The recv_wrapper function may not implement any blocking
             * (e.g. a select call). Therefore do a small sleep here to
             * avoid continuously keeping the CPU busy. */
            usleep (1000);
        }
    }

    if (timed_out)
    {
        // Report the failure and try to recover
        CMSG_LOG_TRANSPORT_ERROR (transport,
                                  "Receive timed out socket %d nbytes was %d last error %s",
                                  socket, nbytes, strerror (errno));

        ret = CMSG_STATUS_CODE_SERVICE_FAILED;
    }
    else if (seconds_waited >= seconds_to_wait / 2)
    {
        // This should not really happen, log it
        CMSG_LOG_TRANSPORT_ERROR (transport, "Receive took %u seconds",
                                  (uint32_t) seconds_waited);
    }

    return ret;
}

/**
 * Receive the message from the server.
 *
 * @param recv - The transport dependent receive function wrapper.
 * @param socket - The socket to read from.
 * @param transport - The CMSG transport to receive the message with.
 * @param peeked_header - The previously peeked header.
 * @param recv_buffer - Pointer to a buffer to store the received message.
 * @param processed_header - Pointer to store the processed CMSG header.
 * @param nbytes - Pointer to store the number of bytes received.
 */
static int32_t
_cmsg_transport_server_recv (cmsg_recv_func recv_wrapper, int socket,
                             cmsg_transport *transport, cmsg_header *peeked_header,
                             uint8_t **recv_buffer, cmsg_header *processed_header,
                             int *nbytes)
{
    uint32_t dyn_len = 0;

    CMSG_ASSERT_RETURN_VAL (peeked_header != NULL, CMSG_RET_ERR);

    if (cmsg_header_process (peeked_header, processed_header) != CMSG_RET_OK)
    {
        // Couldn't process the header for some reason
        CMSG_LOG_TRANSPORT_ERROR (transport,
                                  "Unable to process message header for during receive.");
        return CMSG_RET_ERR;
    }

    // packet size is determined by header_length + message_length.
    // header_length may be greater than sizeof (cmsg_header)
    dyn_len = processed_header->message_length + processed_header->header_length;

    if (dyn_len > CMSG_RECV_BUFFER_SZ)
    {
        *recv_buffer = (uint8_t *) CMSG_CALLOC (1, dyn_len);
        if (*recv_buffer == NULL)
        {
            CMSG_LOG_TRANSPORT_ERROR (transport,
                                      "Failed to allocate memory for received message");
            return CMSG_RET_ERR;
        }
    }

    // read the message
    *nbytes = recv_wrapper (transport, socket, *recv_buffer, dyn_len, MSG_WAITALL);

    return CMSG_RET_OK;
}


/* Receive message from a client and process it */
cmsg_status_code
cmsg_transport_client_recv (cmsg_transport *transport,
                            const ProtobufCServiceDescriptor *descriptor,
                            ProtobufCMessage **messagePtPt)
{
    int nbytes = 0;
    uint32_t dyn_len = 0;
    cmsg_header header_received;
    cmsg_header header_converted;
    uint8_t *recv_buffer = NULL;
    uint8_t *buffer = NULL;
    uint8_t buf_static[512];
    const ProtobufCMessageDescriptor *desc;
    uint32_t extra_header_size;
    cmsg_server_request server_request;
    int socket = transport->socket;
    cmsg_status_code ret;
    time_t receive_timeout;

    /* Use the client receive timeout if set. Otherwise use the default. */
    if (transport->receive_timeout != 0)
    {
        receive_timeout = transport->receive_timeout;
    }
    else
    {
        receive_timeout = MAX_CLIENT_PEEK_LOOP;
    }

    *messagePtPt = NULL;

    ret = cmsg_transport_peek_for_header (transport->tport_funcs.recv_wrapper, transport,
                                          socket, receive_timeout, &header_received);
    if (ret != CMSG_STATUS_CODE_SUCCESS)
    {
        return ret;
    }

    nbytes = transport->tport_funcs.recv_wrapper (transport, socket, &header_received,
                                                  sizeof (cmsg_header), MSG_WAITALL);


    if (nbytes == sizeof (cmsg_header))
    {
        if (cmsg_header_process (&header_received, &header_converted) != CMSG_RET_OK)
        {
            // Couldn't process the header for some reason
            CMSG_LOG_TRANSPORT_ERROR (transport,
                                      "Unable to process message header for during receive. Bytes: %d",
                                      nbytes);
            return CMSG_STATUS_CODE_SERVICE_FAILED;
        }

        CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] received response header\n");

        // read the message

        // Take into account that someone may have changed the size of the header
        // and we don't know about it, make sure we receive all the information.
        // Any TLV is taken into account in the header length.
        dyn_len = header_converted.message_length +
            header_converted.header_length - sizeof (cmsg_header);

        // There is no more data to read so exit.
        if (dyn_len == 0)
        {
            // May have been queued, dropped or there was no message returned
            CMSG_DEBUG (CMSG_INFO,
                        "[TRANSPORT] received response without data. server status %d\n",
                        header_converted.status_code);
            return header_converted.status_code;
        }

        if (dyn_len > sizeof (buf_static))
        {
            recv_buffer = (uint8_t *) CMSG_CALLOC (1, dyn_len);
            if (recv_buffer == NULL)
            {
                /* Didn't allocate memory for recv buffer.  This is an error.
                 * Shut the socket down, it will reopen on the next api call.
                 * Record and return an error. */
                transport->tport_funcs.client_close (transport);
                CMSG_LOG_TRANSPORT_ERROR (transport,
                                          "Failed to allocate memory for received message");
                return CMSG_STATUS_CODE_SERVICE_FAILED;
            }
        }
        else
        {
            recv_buffer = (uint8_t *) buf_static;
            memset (recv_buffer, 0, sizeof (buf_static));
        }

        //just recv the rest of the data to clear the socket
        nbytes = transport->tport_funcs.recv_wrapper (transport, socket, recv_buffer,
                                                      dyn_len, MSG_WAITALL);

        if (nbytes == (int) dyn_len)
        {
            extra_header_size = header_converted.header_length - sizeof (cmsg_header);
            // Set buffer to take into account a larger header than we expected
            buffer = recv_buffer;

            cmsg_tlv_header_process (buffer, &server_request, extra_header_size,
                                     descriptor);

            buffer = buffer + extra_header_size;
            CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] received response data\n");
            cmsg_buffer_print (buffer, dyn_len);

            /* Message is only returned if the server returned Success,
             */
            if (header_converted.status_code == CMSG_STATUS_CODE_SUCCESS)
            {
                ProtobufCMessage *message = NULL;
                ProtobufCAllocator *allocator = &cmsg_memory_allocator;

                CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] unpacking response message\n");

                desc = descriptor->methods[server_request.method_index].output;
                message = protobuf_c_message_unpack (desc, allocator,
                                                     header_converted.message_length,
                                                     buffer);

                // Free the allocated buffer
                if (recv_buffer != buf_static)
                {
                    CMSG_FREE (recv_buffer);
                    recv_buffer = NULL;
                }

                // Msg not unpacked correctly
                if (message == NULL)
                {
                    CMSG_LOG_TRANSPORT_ERROR (transport,
                                              "Error unpacking response message. Msg length:%d",
                                              header_converted.message_length);
                    return CMSG_STATUS_CODE_SERVICE_FAILED;
                }
                *messagePtPt = message;
            }

            // Make sure we return the status from the server
            return header_converted.status_code;
        }
        else
        {
            CMSG_LOG_TRANSPORT_ERROR (transport,
                                      "No data for recv. socket:%d, dyn_len:%d, actual len:%d strerr %d:%s",
                                      transport->socket,
                                      dyn_len, nbytes, errno, strerror (errno));

        }
        if (recv_buffer != buf_static)
        {
            CMSG_FREE (recv_buffer);
            recv_buffer = NULL;
        }
    }
    else if (nbytes > 0)
    {
        /* Didn't receive all of the CMSG header.
         */
        CMSG_LOG_TRANSPORT_ERROR (transport,
                                  "Bad header length for recv. Socket:%d nbytes:%d",
                                  transport->socket, nbytes);

        // TEMP to keep things going
        recv_buffer = (uint8_t *) CMSG_CALLOC (1, nbytes);
        nbytes = transport->tport_funcs.recv_wrapper (transport, socket, recv_buffer,
                                                      nbytes, MSG_WAITALL);
        CMSG_FREE (recv_buffer);
        recv_buffer = NULL;
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
            CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] recv socket %d error: %s\n",
                        transport->socket, strerror (errno));
            return CMSG_STATUS_CODE_SERVER_CONNRESET;
        }
        else
        {
            CMSG_LOG_TRANSPORT_ERROR (transport, "Recv error. Socket:%d Error:%s",
                                      transport->socket, strerror (errno));
        }
    }

    return CMSG_STATUS_CODE_SERVICE_FAILED;
}


/**
 * Configure the transport to allow blocking if send cannot send it straight away
 *
 */
int32_t
cmsg_transport_send_can_block_enable (cmsg_transport *transport, uint32_t send_can_block)
{
    return transport->tport_funcs.send_can_block_enable (transport, send_can_block);
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
    return transport->tport_funcs.ipfree_bind_enable (transport, ipfree_bind_enable);
}

int32_t
cmsg_transport_server_recv (int32_t server_socket, cmsg_transport *transport,
                            uint8_t **recv_buffer, cmsg_header *processed_header,
                            int *nbytes)
{
    int32_t ret = CMSG_RET_ERR;
    cmsg_status_code peek_status;
    cmsg_header header_received;

    CMSG_ASSERT_RETURN_VAL (transport != NULL, CMSG_RET_ERR);

    peek_status = cmsg_transport_peek_for_header (transport->tport_funcs.recv_wrapper,
                                                  transport,
                                                  server_socket, MAX_SERVER_PEEK_LOOP,
                                                  &header_received);
    if (peek_status == CMSG_STATUS_CODE_SUCCESS)
    {
        ret = _cmsg_transport_server_recv (transport->tport_funcs.recv_wrapper,
                                           server_socket, transport, &header_received,
                                           recv_buffer, processed_header, nbytes);
    }
    else if (peek_status == CMSG_STATUS_CODE_CONNECTION_CLOSED)
    {
        ret = CMSG_RET_CLOSED;
    }

    return ret;
}

int32_t
cmsg_transport_rpc_server_send (int socket, cmsg_transport *transport, void *buff,
                                int length, int flag)
{
    return (send (socket, buff, length, flag));
}

/**
 * Oneway servers do not send replies to received messages. This function therefore
 * returns 0.
 */
int32_t
cmsg_transport_oneway_server_send (int socket, cmsg_transport *transport, void *buff,
                                   int length, int flag)
{
    return 0;
}

int
cmsg_transport_get_socket (cmsg_transport *transport)
{
    return transport->socket;
}

void
cmsg_transport_socket_close (cmsg_transport *transport)
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

bool
cmsg_transport_compare (cmsg_transport *one, cmsg_transport *two)
{
    if (one->type == two->type)
    {
        switch (one->type)
        {
        case CMSG_TRANSPORT_RPC_TCP:
        case CMSG_TRANSPORT_ONEWAY_TCP:
            if ((one->config.socket.family == two->config.socket.family) &&
                (one->config.socket.sockaddr.in.sin_addr.s_addr ==
                 two->config.socket.sockaddr.in.sin_addr.s_addr) &&
                (one->config.socket.sockaddr.in.sin_port ==
                 two->config.socket.sockaddr.in.sin_port))
            {
                return true;
            }
            break;
        case CMSG_TRANSPORT_RPC_TIPC:
        case CMSG_TRANSPORT_ONEWAY_TIPC:
            if ((one->config.socket.family == two->config.socket.family) &&
                (one->config.socket.sockaddr.tipc.family ==
                 two->config.socket.sockaddr.tipc.family) &&
                (one->config.socket.sockaddr.tipc.addrtype ==
                 two->config.socket.sockaddr.tipc.addrtype) &&
                (one->config.socket.sockaddr.tipc.addr.name.domain ==
                 two->config.socket.sockaddr.tipc.addr.name.domain) &&
                (one->config.socket.sockaddr.tipc.addr.name.name.instance ==
                 two->config.socket.sockaddr.tipc.addr.name.name.instance) &&
                (one->config.socket.sockaddr.tipc.addr.name.name.type ==
                 two->config.socket.sockaddr.tipc.addr.name.name.type) &&
                (one->config.socket.sockaddr.tipc.scope ==
                 two->config.socket.sockaddr.tipc.scope))
            {
                return true;
            }
            break;
        case CMSG_TRANSPORT_RPC_UNIX:
        case CMSG_TRANSPORT_ONEWAY_UNIX:
            if ((one->config.socket.family == two->config.socket.family) &&
                (one->config.socket.sockaddr.un.sun_family ==
                 two->config.socket.sockaddr.un.sun_family) &&
                (strcmp (one->config.socket.sockaddr.un.sun_path,
                         two->config.socket.sockaddr.un.sun_path) == 0))
            {
                return true;
            }
            break;
        default:
            return false;
        }
    }

    return false;
}
