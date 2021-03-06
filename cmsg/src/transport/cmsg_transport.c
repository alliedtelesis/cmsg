/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#include "cmsg_private.h"
#include "cmsg_transport_private.h"
#include "cmsg_error.h"
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

/* Limit the size of message read */
#define CMSG_RECV_ALL_CHUNK_SIZE  (16 * 1024)

/**
 * An abstraction of the 'connect' system call that allows a timeout
 * value to be used. Adapted from "Unix Network Programming".
 *
 * @param sockfd - The socket to connect.
 * @param addr - The address to connect to.
 * @param addrlen - The size of the 'addr' argument.
 * @param timeout - The timeout value in seconds (or zero to use the default
 *                  timeout value for the socket type).
 */
int
connect_nb (int sockfd, const struct sockaddr *addr, socklen_t addrlen, int timeout)
{
    int flags;
    int n;
    int error;
    socklen_t len;
    fd_set rset;
    fd_set wset;
    struct timeval tval;

    error = 0;
    len = sizeof (error);

    flags = fcntl (sockfd, F_GETFL, 0);
    fcntl (sockfd, F_SETFL, flags | O_NONBLOCK);

    n = connect (sockfd, addr, addrlen);
    if (n < 0)
    {
        if (errno != EINPROGRESS)
        {
            fcntl (sockfd, F_SETFL, flags);
            return -1;
        }
    }

    if (n == 0)
    {
        /* connect completed immediately */
        fcntl (sockfd, F_SETFL, flags);
        return 0;
    }

    FD_ZERO (&rset);
    FD_SET (sockfd, &rset);
    wset = rset;
    tval.tv_sec = timeout;
    tval.tv_usec = 0;

    n = select (sockfd + 1, &rset, &wset, NULL, timeout ? &tval : NULL);
    if (n == 0)
    {
        fcntl (sockfd, F_SETFL, flags);
        errno = ETIMEDOUT;
        return -1;
    }

    if (FD_ISSET (sockfd, &rset) || FD_ISSET (sockfd, &wset))
    {
        if (getsockopt (sockfd, SOL_SOCKET, SO_ERROR, &error, &len) < 0)
        {
            fcntl (sockfd, F_SETFL, flags);
            return -1;
        }
    }
    else
    {
        fcntl (sockfd, F_SETFL, flags);
        return -1;
    }

    fcntl (sockfd, F_SETFL, flags);

    if (error)
    {
        errno = error;
        return -1;
    }

    return 0;
}

/**
 * An abstraction of the 'send' system call that ensures all of the requested
 * data is sent, even if the call is interrupted (EINTR). Note that the 'send'
 * call can be interrupted in two different ways:
 *  1. The call is interrupted before any data is sent. In this case the call
 *     returns -1 with EINTR set.
 *  2. The call is interrupted after at least 1 byte has been sent. In this case
 *     the call returns the total number of bytes sent before being interrupted.
 *
 * This function assumes that the socket is in blocking mode.
 *
 * @param sockfd - The blocking socket to send on.
 * @param buf - The data to send on the socket.
 * @param len - The number of bytes to send.
 * @param flags - The flags to use with the send call.
 */
ssize_t
cmsg_transport_socket_send (int sockfd, const void *buf, size_t len, int flags)
{
    ssize_t ret;
    ssize_t sent_bytes = 0;
    const uint8_t *data = (const uint8_t *) buf;

    while (sent_bytes < len)
    {
        ret = TEMP_FAILURE_RETRY (send (sockfd, data + sent_bytes, len - sent_bytes,
                                        flags));
        if (ret == -1)
        {
            return -1;
        }
        sent_bytes += ret;
    }

    return sent_bytes;
}

/**
 * An abstraction of the 'recv' system call that ensures all of the requested
 * data is received, even if the call is interrupted (EINTR). Note that the 'recv'
 * call can be interrupted in two different ways:
 *  1. The call is interrupted before any data is received. In this case the call
 *     returns -1 with EINTR set.
 *  2. The call is interrupted after at least 1 byte has been received. In this case
 *     the call returns the total number of bytes received before being interrupted.
 *
 * This function assumes that the socket is in blocking mode.
 *
 * @param sockfd - The blocking socket to receive on.
 * @param buf - The buffer to receive the data into.
 * @param len - The number of bytes to receive.
 * @param flags - The flags to use with the recv call.
 */
ssize_t
cmsg_transport_socket_recv (int sockfd, void *buf, size_t len, int flags)
{
    ssize_t ret;
    size_t chunk_size;
    ssize_t received_bytes = 0;
    ssize_t bytes_left = 0;
    uint8_t *data = (uint8_t *) buf;

    /* If non-blocking behaviour is requested then we don't try to handle
     * being interrupted as we assume the caller is expecting to handle
     * the case where the full length of data is not read. */
    if (flags & MSG_DONTWAIT)
    {
        return recv (sockfd, buf, len, flags);
    }

    while (received_bytes < len)
    {
        /**
         * MSG_WAITALL will cause the recv call to block until all data has been
         * received, but it appears that in cases where the size of the data to
         * be received is close to or larger than the size of sockets receive
         * buffer it may be possible for the connection to deadlock as the
         * receiver is waiting for the sender to send more, and the sender is
         * waiting for the receiver to receive more.
         *
         * To avoid this situation, the data is read in chunks that are
         * significantly smaller than the receive buffer.
         */

        /* Do not read past the end of the message */
        bytes_left = len - received_bytes;
        chunk_size = MIN (bytes_left, CMSG_RECV_ALL_CHUNK_SIZE);

        ret = TEMP_FAILURE_RETRY (recv (sockfd, data + received_bytes, chunk_size, flags));
        if (ret < 0)
        {
            /* error */
            return ret;
        }
        else if (ret == 0)
        {
            /* shutdown */
            return received_bytes;
        }

        received_bytes += ret;
    }

    return received_bytes;
}

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
            if (tport->config.socket.family == PF_INET6)
            {
                port = ntohs (tport->config.socket.sockaddr.in6.sin6_port);
                inet_ntop (tport->config.socket.sockaddr.generic.sa_family,
                           &(tport->config.socket.sockaddr.in6.sin6_addr), ip,
                           INET6_ADDRSTRLEN);
                // ipv6 addresses are enclosed in [] in URLs due to ambiguity of :s.
                snprintf (tport->tport_id, CMSG_MAX_TPORT_ID_LEN, ".tcp[[%s]:%u]", ip,
                          port);
            }
            else
            {
                port = ntohs (tport->config.socket.sockaddr.in.sin_port);
                inet_ntop (tport->config.socket.sockaddr.generic.sa_family,
                           &(tport->config.socket.sockaddr.in.sin_addr), ip,
                           INET6_ADDRSTRLEN);
                snprintf (tport->tport_id, CMSG_MAX_TPORT_ID_LEN, ".tcp[%s:%u]", ip, port);
            }
            break;
        }
    case CMSG_TRANSPORT_BROADCAST:
        {
            snprintf (tport->tport_id, CMSG_MAX_TPORT_ID_LEN, ".tipcb");
            break;
        }

    case CMSG_TRANSPORT_LOOPBACK:
        {
            strncpy (tport->tport_id, ".lpb", CMSG_MAX_TPORT_ID_LEN);
            break;
        }

    case CMSG_TRANSPORT_FORWARDING:
        {
            strncpy (tport->tport_id, ".fwd", CMSG_MAX_TPORT_ID_LEN);
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
    transport->connect_timeout = CONNECT_TIMEOUT_DEFAULT;
    transport->send_timeout = SEND_TIMEOUT_DEFAULT;
    transport->receive_timeout = RECV_TIMEOUT_DEFAULT;
    transport->receive_peek_timeout = RECV_HEADER_PEEK_TIMEOUT_DEFAULT;

    switch (type)
    {
    case CMSG_TRANSPORT_RPC_TCP:
        cmsg_transport_tcp_init (transport);
        break;
    case CMSG_TRANSPORT_ONEWAY_TCP:
        cmsg_transport_oneway_tcp_init (transport);
        break;
    case CMSG_TRANSPORT_BROADCAST:
        cmsg_transport_tipc_broadcast_init (transport);
        break;

    case CMSG_TRANSPORT_LOOPBACK:
        cmsg_transport_loopback_init (transport);
        break;

    case CMSG_TRANSPORT_FORWARDING:
        cmsg_transport_forwarding_init (transport);
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

void
cmsg_transport_destroy (cmsg_transport *transport)
{
    if (transport)
    {
        if (transport->tport_funcs.destroy)
        {
            transport->tport_funcs.destroy (transport);
        }
        CMSG_FREE (transport);
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
 * @param header_size - The size of the header to be peeked.
 *
 * @returns CMSG_PEEK_CODE_SUCCESS on success, the related cmsg_peek_code otherwise.
 */
cmsg_peek_code
cmsg_transport_peek_for_header (cmsg_recv_func recv_wrapper, cmsg_transport *transport,
                                int32_t socket, time_t seconds_to_wait,
                                void *header_received, int header_size)
{
    cmsg_peek_code ret = CMSG_PEEK_CODE_SUCCESS;
    int nbytes = 0;
    bool timed_out = false;
    time_t seconds_waited = 0;
    struct timespec start;
    struct timespec current;

    clock_gettime (CLOCK_MONOTONIC_RAW, &start);

    /* Peek until data arrives. This allows us to timeout and recover if no data arrives. */
    while (!timed_out)
    {
        nbytes = recv_wrapper (transport, socket, header_received,
                               header_size, MSG_PEEK | MSG_DONTWAIT);
        if (nbytes == header_size)
        {
            break;
        }
        else
        {
            if (nbytes == 0)
            {
                return CMSG_PEEK_CODE_CONNECTION_CLOSED;
            }
            else if (nbytes == -1)
            {
                if (errno == ECONNRESET || errno == ECONNABORTED)
                {
                    CMSG_DEBUG (CMSG_INFO,
                                "[TRANSPORT] receive failed %d %s", nbytes,
                                strerror (errno));
                    return CMSG_PEEK_CODE_CONNECTION_RESET;
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

        clock_gettime (CLOCK_MONOTONIC_RAW, &current);
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

        ret = CMSG_PEEK_CODE_TIMEOUT;
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

/**
 * Convert a cmsg_peek_code value to a cmsg_status_code value.
 *
 * @param peek_code - The peek code to convert.
 *
 * @returns The converted status code.
 */
cmsg_status_code
cmsg_transport_peek_to_status_code (cmsg_peek_code peek_code)
{
    cmsg_status_code status_code;

    switch (peek_code)
    {
    case CMSG_PEEK_CODE_CONNECTION_CLOSED:
        status_code = CMSG_STATUS_CODE_CONNECTION_CLOSED;
        break;
    case CMSG_PEEK_CODE_CONNECTION_RESET:
        status_code = CMSG_STATUS_CODE_SERVER_CONNRESET;
        break;
    case CMSG_PEEK_CODE_TIMEOUT:
        status_code = CMSG_STATUS_CODE_SERVICE_FAILED;
        break;
    case CMSG_PEEK_CODE_SUCCESS:
    default:
        status_code = CMSG_STATUS_CODE_SUCCESS;
        break;
    }

    return status_code;
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
    cmsg_server_request server_request = { };
    int socket = transport->socket;
    cmsg_peek_code ret;
    time_t receive_timeout = transport->receive_peek_timeout;

    *messagePtPt = NULL;

    ret = cmsg_transport_peek_for_header (transport->tport_funcs.recv_wrapper, transport,
                                          socket, receive_timeout, &header_received,
                                          sizeof (header_received));
    if (ret != CMSG_PEEK_CODE_SUCCESS)
    {
        return cmsg_transport_peek_to_status_code (ret);
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
                transport->tport_funcs.socket_close (transport);
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

            if (cmsg_tlv_header_process (buffer, &server_request, extra_header_size,
                                         descriptor) != CMSG_RET_OK)
            {
                // Free the allocated buffer
                if (recv_buffer != buf_static)
                {
                    CMSG_FREE (recv_buffer);
                    recv_buffer = NULL;
                }
                return CMSG_STATUS_CODE_SERVICE_FAILED;
            }

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

int32_t
cmsg_transport_server_recv (int32_t server_socket, cmsg_transport *transport,
                            uint8_t **recv_buffer, cmsg_header *processed_header,
                            int *nbytes)
{
    int32_t ret = CMSG_RET_ERR;
    cmsg_peek_code peek_status;
    cmsg_header header_received;
    time_t receive_timeout = transport->receive_peek_timeout;

    CMSG_ASSERT_RETURN_VAL (transport != NULL, CMSG_RET_ERR);

    peek_status = cmsg_transport_peek_for_header (transport->tport_funcs.recv_wrapper,
                                                  transport,
                                                  server_socket, receive_timeout,
                                                  &header_received,
                                                  sizeof (header_received));
    if (peek_status == CMSG_PEEK_CODE_SUCCESS)
    {
        ret = _cmsg_transport_server_recv (transport->tport_funcs.recv_wrapper,
                                           server_socket, transport, &header_received,
                                           recv_buffer, processed_header, nbytes);
    }
    else if (peek_status == CMSG_PEEK_CODE_CONNECTION_CLOSED ||
             peek_status == CMSG_PEEK_CODE_CONNECTION_RESET)
    {
        ret = CMSG_RET_CLOSED;
    }

    return ret;
}

int32_t
cmsg_transport_rpc_server_send (int socket, cmsg_transport *transport, void *buff,
                                int length, int flag)
{
    return (cmsg_transport_socket_send (socket, buff, length, flag));
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

int32_t
cmsg_transport_connect (cmsg_transport *transport)
{
    int ret = CMSG_RET_OK;

    if (transport->tport_funcs.connect)
    {
        ret = transport->tport_funcs.connect (transport);
        if (ret == CMSG_RET_OK)
        {
            transport->tport_funcs.apply_send_timeout (transport, transport->socket);
            transport->tport_funcs.apply_recv_timeout (transport, transport->socket);
        }
    }

    return ret;
}

int32_t
cmsg_transport_accept (cmsg_transport *transport)
{
    int sock = -1;

    if (transport->tport_funcs.server_accept)
    {
        sock = transport->tport_funcs.server_accept (transport);
        if (sock != -1)
        {
            transport->tport_funcs.apply_send_timeout (transport, sock);
            transport->tport_funcs.apply_recv_timeout (transport, sock);
        }
    }

    return sock;
}

int32_t
cmsg_transport_set_connect_timeout (cmsg_transport *transport, uint32_t timeout)
{
    if (transport->type == CMSG_TRANSPORT_RPC_UNIX ||
        transport->type == CMSG_TRANSPORT_ONEWAY_UNIX)
    {
        /* Setting a connect timeout for unix transports is not supported. */
        return -1;
    }

    transport->connect_timeout = timeout;

    return 0;
}

int32_t
cmsg_transport_set_send_timeout (cmsg_transport *transport, uint32_t timeout)
{
    transport->send_timeout = timeout;

    return transport->tport_funcs.apply_send_timeout (transport, transport->socket);
}

int32_t
cmsg_transport_set_recv_peek_timeout (cmsg_transport *transport, uint32_t timeout)
{
    transport->receive_peek_timeout = timeout;

    return 0;
}

int32_t
cmsg_transport_apply_send_timeout (cmsg_transport *transport, int sockfd)
{
    struct timeval tv;

    if (sockfd != -1)
    {
        tv.tv_sec = transport->send_timeout;
        tv.tv_usec = 0;

        if (setsockopt (sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof (tv)) < 0)
        {
            CMSG_DEBUG (CMSG_INFO, "Failed to set send timeout (errno=%d)\n", errno);
            return -1;
        }
    }

    return 0;
}

int32_t
cmsg_transport_apply_recv_timeout (cmsg_transport *transport, int sockfd)
{
    struct timeval tv;

    if (sockfd != -1)
    {
        tv.tv_sec = transport->receive_timeout;
        tv.tv_usec = 0;

        if (setsockopt (sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof (tv)) < 0)
        {
            CMSG_DEBUG (CMSG_INFO, "Failed to set recv timeout (errno=%d)\n", errno);
            return -1;
        }
    }

    return 0;
}

bool
cmsg_transport_compare (const cmsg_transport *one, const cmsg_transport *two)
{
    if (one->type == two->type)
    {
        switch (one->type)
        {
        case CMSG_TRANSPORT_RPC_TCP:
        case CMSG_TRANSPORT_ONEWAY_TCP:
            if ((one->config.socket.family == two->config.socket.family) &&
                ((one->config.socket.family == AF_INET &&
                  one->config.socket.sockaddr.in.sin_addr.s_addr ==
                  two->config.socket.sockaddr.in.sin_addr.s_addr) ||
                 (one->config.socket.family == AF_INET6 &&
                  memcmp (&one->config.socket.sockaddr.in6.sin6_addr,
                          &two->config.socket.sockaddr.in6.sin6_addr,
                          sizeof (struct in6_addr)) == 0)) &&
                (one->config.socket.sockaddr.in.sin_port ==
                 two->config.socket.sockaddr.in.sin_port))
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

/**
 * Create a 'cmsg_tcp_transport_info' message for the given tcp transport.
 *
 * @param transport - The tcp transport to build the message for.
 *
 * @returns A pointer to the message on success, NULL on failure.
 */
cmsg_tcp_transport_info *
cmsg_transport_tcp_info_create (const cmsg_transport *transport)
{
    cmsg_tcp_transport_info *tcp_info = NULL;
    bool ipv4;
    uint32_t addr_len;
    uint8_t *addr = NULL;
    uint32_t port_len;
    uint8_t *port = NULL;

    tcp_info = CMSG_MALLOC (sizeof (cmsg_tcp_transport_info));
    if (!tcp_info)
    {
        return NULL;
    }

    cmsg_tcp_transport_info_init (tcp_info);

    port_len = sizeof (uint16_t);
    port = CMSG_MALLOC (port_len);

    ipv4 = (transport->config.socket.family != PF_INET6);
    if (ipv4)
    {
        addr_len = sizeof (transport->config.socket.sockaddr.in.sin_addr.s_addr);
        addr = CMSG_MALLOC (addr_len);
        memcpy (addr, &transport->config.socket.sockaddr.in.sin_addr.s_addr, addr_len);
        memcpy (port, &transport->config.socket.sockaddr.in.sin_port, port_len);
    }
    else
    {
        addr_len = sizeof (transport->config.socket.sockaddr.in6.sin6_addr.s6_addr);
        addr = CMSG_MALLOC (addr_len);
        memcpy (addr, &transport->config.socket.sockaddr.in6.sin6_addr.s6_addr, addr_len);
        memcpy (port, &transport->config.socket.sockaddr.in6.sin6_port, port_len);
    }

    CMSG_SET_FIELD_VALUE (tcp_info, ipv4, ipv4);
    CMSG_SET_FIELD_BYTES (tcp_info, addr, addr, addr_len);
    CMSG_SET_FIELD_BYTES (tcp_info, port, port, port_len);
    if (!ipv4)
    {
        CMSG_SET_FIELD_VALUE (tcp_info, scope_id,
                              transport->config.socket.sockaddr.in6.sin6_scope_id);
    }
    if (transport->config.socket.vrf_bind_dev[0])
    {
        CMSG_SET_FIELD_PTR (tcp_info, vrf_bind_dev,
                            CMSG_STRDUP (transport->config.socket.vrf_bind_dev));
    }
    return tcp_info;
}

/**
 * Create a 'cmsg_unix_transport_info' message for the given unix transport.
 *
 * @param transport - The unix transport to build the message for.
 *
 * @returns A pointer to the message on success, NULL on failure.
 */
cmsg_unix_transport_info *
cmsg_transport_unix_info_create (const cmsg_transport *transport)
{
    cmsg_unix_transport_info *unix_info = NULL;
    char *unix_path = NULL;

    unix_path = CMSG_STRDUP (transport->config.socket.sockaddr.un.sun_path);
    if (!unix_path)
    {
        return NULL;
    }

    unix_info = CMSG_MALLOC (sizeof (cmsg_unix_transport_info));
    if (!unix_info)
    {
        CMSG_FREE (unix_path);
        return NULL;
    }
    cmsg_unix_transport_info_init (unix_info);

    CMSG_SET_FIELD_PTR (unix_info, path, unix_path);

    return unix_info;
}

/**
 * Create a 'cmsg_transport_info' message for the given transport.
 *
 * @param transport - The transport to build the message for.
 *
 * @returns A pointer to the message on success, NULL on failure.
 *          This message should be freed using 'cmsg_transport_info_free'.
 */
cmsg_transport_info *
cmsg_transport_info_create (const cmsg_transport *transport)
{
    cmsg_transport_info *transport_info = NULL;
    cmsg_tcp_transport_info *tcp_info = NULL;
    cmsg_unix_transport_info *unix_info = NULL;

    if (transport->type != CMSG_TRANSPORT_RPC_TCP &&
        transport->type != CMSG_TRANSPORT_RPC_UNIX &&
        transport->type != CMSG_TRANSPORT_ONEWAY_UNIX &&
        transport->type != CMSG_TRANSPORT_ONEWAY_TCP)
    {
        return NULL;
    }

    transport_info = CMSG_MALLOC (sizeof (cmsg_transport_info));
    if (!transport_info)
    {
        return NULL;
    }
    cmsg_transport_info_init (transport_info);

    if (transport->type == CMSG_TRANSPORT_RPC_TCP ||
        transport->type == CMSG_TRANSPORT_ONEWAY_TCP)
    {
        tcp_info = cmsg_transport_tcp_info_create (transport);
        if (tcp_info)
        {
            CMSG_SET_FIELD_VALUE (transport_info, type, CMSG_TRANSPORT_INFO_TYPE_TCP);
            CMSG_SET_FIELD_VALUE (transport_info, one_way,
                                  transport->type == CMSG_TRANSPORT_ONEWAY_TCP);
            CMSG_SET_FIELD_ONEOF (transport_info, tcp_info, tcp_info,
                                  data, CMSG_TRANSPORT_INFO_DATA_TCP_INFO);
        }
        else
        {
            CMSG_FREE (transport_info);
            transport_info = NULL;
        }
    }
    else if (transport->type == CMSG_TRANSPORT_RPC_UNIX ||
             transport->type == CMSG_TRANSPORT_ONEWAY_UNIX)
    {
        unix_info = cmsg_transport_unix_info_create (transport);
        if (unix_info)
        {
            CMSG_SET_FIELD_VALUE (transport_info, type, CMSG_TRANSPORT_INFO_TYPE_UNIX);
            CMSG_SET_FIELD_VALUE (transport_info, one_way,
                                  transport->type == CMSG_TRANSPORT_ONEWAY_UNIX);
            CMSG_SET_FIELD_ONEOF (transport_info, unix_info, unix_info,
                                  data, CMSG_TRANSPORT_INFO_DATA_UNIX_INFO);
        }
        else
        {
            CMSG_FREE (transport_info);
            transport_info = NULL;
        }
    }

    return transport_info;
}

/**
 * Free a 'cmsg_transport_info' message created by a call to
 * 'cmsg_transport_info_create'.
 *
 * @param info - The message to free.
 */
void
cmsg_transport_info_free (cmsg_transport_info *transport_info)
{
    if (transport_info->type == CMSG_TRANSPORT_INFO_TYPE_UNIX)
    {
        CMSG_FREE (transport_info->unix_info->path);
        CMSG_FREE (transport_info->unix_info);
    }
    else if (transport_info->type == CMSG_TRANSPORT_INFO_TYPE_TCP)
    {
        CMSG_FREE (transport_info->tcp_info->port.data);
        CMSG_FREE (transport_info->tcp_info->addr.data);
        CMSG_FREE (transport_info->tcp_info->vrf_bind_dev);
        CMSG_FREE (transport_info->tcp_info);
    }
    CMSG_FREE (transport_info);
}

/**
 * Create a 'cmsg_transport' structure based on the input 'cmsg_transport_info'
 * message.
 *
 * @param transport_info - The 'cmsg_transport_info' message to build the transport
 *                         for.
 *
 * @returns A pointer to the transport on success, NULL on failure.
 */
cmsg_transport *
cmsg_transport_info_to_transport (const cmsg_transport_info *transport_info)
{
    cmsg_transport *transport = NULL;

    if (transport_info->type == CMSG_TRANSPORT_INFO_TYPE_UNIX)
    {
        if (transport_info->one_way)
        {
            transport = cmsg_transport_new (CMSG_TRANSPORT_ONEWAY_UNIX);
        }
        else
        {
            transport = cmsg_transport_new (CMSG_TRANSPORT_RPC_UNIX);
        }

        transport->config.socket.family = AF_UNIX;
        transport->config.socket.sockaddr.un.sun_family = AF_UNIX;
        snprintf (transport->config.socket.sockaddr.un.sun_path,
                  sizeof (transport->config.socket.sockaddr.un.sun_path) - 1,
                  transport_info->unix_info->path);
    }
    else if (transport_info->type == CMSG_TRANSPORT_INFO_TYPE_TCP)
    {
        uint8_t *port = transport_info->tcp_info->port.data;
        uint32_t port_len = transport_info->tcp_info->port.len;
        uint8_t *addr = transport_info->tcp_info->addr.data;
        uint32_t addr_len = transport_info->tcp_info->addr.len;

        if (transport_info->one_way)
        {
            transport = cmsg_transport_new (CMSG_TRANSPORT_ONEWAY_TCP);
        }
        else
        {
            transport = cmsg_transport_new (CMSG_TRANSPORT_RPC_TCP);
        }

        if (transport_info->tcp_info->ipv4)
        {
            transport->config.socket.family = PF_INET;
            transport->config.socket.sockaddr.generic.sa_family = PF_INET;
            transport->config.socket.sockaddr.in.sin_family = AF_INET;
            memcpy (&transport->config.socket.sockaddr.in.sin_port, port, port_len);
            memcpy (&transport->config.socket.sockaddr.in.sin_addr.s_addr, addr, addr_len);
        }
        else
        {
            transport->config.socket.family = PF_INET6;
            transport->config.socket.sockaddr.generic.sa_family = PF_INET6;
            memcpy (&transport->config.socket.sockaddr.in6.sin6_port, port, port_len);
            memcpy (&transport->config.socket.sockaddr.in6.sin6_addr.s6_addr, addr,
                    addr_len);
            transport->config.socket.sockaddr.in6.sin6_scope_id =
                transport_info->tcp_info->scope_id;
        }
        if (transport_info->tcp_info->vrf_bind_dev)
        {
            strncpy (transport->config.socket.vrf_bind_dev,
                     transport_info->tcp_info->vrf_bind_dev, CMSG_BIND_DEV_NAME_MAX);
            transport->config.socket.vrf_bind_dev[CMSG_BIND_DEV_NAME_MAX - 1] = '\0';
        }
    }

    return transport;
}

/**
 * Compares two 'cmsg_transport_info' structures for equality.
 *
 * @param transport_info_a - The first structure to compare.
 * @param transport_info_b - The second structure to compare.
 *
 * @returns true if they are equal, false otherwise.
 */
bool
cmsg_transport_info_compare (const cmsg_transport_info *transport_info_a,
                             const cmsg_transport_info *transport_info_b)
{
    if (transport_info_a->type != transport_info_b->type ||
        transport_info_a->one_way != transport_info_b->one_way)
    {
        return false;
    }

    if (transport_info_a->type == CMSG_TRANSPORT_INFO_TYPE_TCP)
    {
        cmsg_tcp_transport_info *tcp_info_a = transport_info_a->tcp_info;
        cmsg_tcp_transport_info *tcp_info_b = transport_info_b->tcp_info;

        if (tcp_info_a->ipv4 == tcp_info_b->ipv4 &&
            !memcmp (tcp_info_a->port.data, tcp_info_b->port.data, tcp_info_a->port.len) &&
            !memcmp (tcp_info_a->addr.data, tcp_info_b->addr.data, tcp_info_a->addr.len))
        {
            return true;
        }
        return false;
    }

    if (transport_info_a->type == CMSG_TRANSPORT_INFO_TYPE_UNIX)
    {
        cmsg_unix_transport_info *unix_info_a = transport_info_a->unix_info;
        cmsg_unix_transport_info *unix_info_b = transport_info_b->unix_info;

        return (strcmp (unix_info_a->path, unix_info_b->path) == 0);
    }

    return false;
}

/**
 * Returns a copy of the given cmsg transport.
 *
 * @param transport - The cmsg transport to return a copy of.
 *
 * @returns A pointer to the copied transport on success, NULL otherwise.
 */
cmsg_transport *
cmsg_transport_copy (const cmsg_transport *transport)
{
    cmsg_transport *transport_copy = NULL;

    transport_copy = (cmsg_transport *) CMSG_CALLOC (1, sizeof (cmsg_transport));
    if (transport_copy)
    {
        memcpy (transport_copy, transport, sizeof (cmsg_transport));
    }

    return transport_copy;
}

/**
 * Returns a copy of the given 'cmsg_transport_info' message.
 *
 * @param transport_info - The  'cmsg_transport_info' message to return a copy of.
 *
 * @returns A pointer to the copied message on success, NULL otherwise.
 */
cmsg_transport_info *
cmsg_transport_info_copy (const cmsg_transport_info *transport_info)
{
    cmsg_transport *transport = NULL;
    cmsg_transport_info *copied_info = NULL;

    /* Manually deep copying the message here would be more efficient however
     * it is much simpler to just convert to a transport and then convert back
     * to a new 'cmsg_transport_info' message. */

    transport = cmsg_transport_info_to_transport (transport_info);
    copied_info = cmsg_transport_info_create (transport);
    cmsg_transport_destroy (transport);

    return copied_info;
}

/**
 * Return the IPv4 address for this transport. It is up to the
 * user to assure that the transport used is of TCP type.
 *
 * @param transport - The transport to return the IPv4 address for.
 *
 * @returns The IPv4 address.
 */
struct in_addr
cmsg_transport_ipv4_address_get (const cmsg_transport *transport)
{
    return transport->config.socket.sockaddr.in.sin_addr;
}
