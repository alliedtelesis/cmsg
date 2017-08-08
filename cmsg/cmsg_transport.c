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
                                            cmsg_server *server, int peek,
                                            cmsg_header *peeked_header);


/**
 * This function reads a uint32_t from an input array pointed to by *in
 *
 * @param in - is a pointer to a pointer to the location to read the value
 * @param value - is a pointer to variable to written from the input array
 */
void
cmsg_transport_crypto_get32 (uint8_t *in, uint32_t *value)
{
    *value = in[0] << 24;
    *value |= in[1] << 16;
    *value |= in[2] << 8;
    *value |= in[3];
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
#ifdef HAVE_VCSTACK
    case CMSG_TRANSPORT_CPG:
        {
            // Potential for truncation of the CPG name
            // if maxlen < CPG_MAX_NAME_LENGTH (128).
            snprintf (tport->tport_id, CMSG_MAX_TPORT_ID_LEN, ".cpg[%s]",
                      tport->config.cpg.group_name.value);
            break;
        }
#endif /* HAVE_VCSTACK */
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
#ifdef HAVE_VCSTACK
    case CMSG_TRANSPORT_CPG:
        cmsg_transport_cpg_init (transport);
        break;
#endif /* HAVE_VCSTACK */
    case CMSG_TRANSPORT_BROADCAST:
        cmsg_transport_tipc_broadcast_init (transport);
        break;
    case CMSG_TRANSPORT_ONEWAY_USERDEFINED:
        cmsg_transport_oneway_udt_init (transport);
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
        transport->client_send_tries = 0;
        transport->connection.sockets.client_socket = -1;
        transport->connection.sockets.listening_socket = -1;

        if (pthread_mutex_init (&transport->connection_mutex, NULL) != 0)
        {
            CMSG_LOG_GEN_ERROR ("Init failed for transport connection_mutex.");
            CMSG_FREE (transport);
            return NULL;
        }
    }

    return transport;
}

int32_t
cmsg_transport_destroy (cmsg_transport *transport)
{
    if (transport)
    {
        pthread_mutex_destroy (&transport->connection_mutex);
        CMSG_FREE (transport);
        return 0;
    }
    else
    {
        return 1;
    }
}

/* Poll for the header data, give up if we timeout. This is used to avoid blocking forever
 * on the receive if the data is never sent or is partially sent.
 */
cmsg_status_code
cmsg_transport_peek_for_header (cmsg_recv_func recv_wrapper, void *recv_wrapper_data,
                                cmsg_transport *transport, int32_t socket, int32_t maxLoop,
                                cmsg_header *header_received)
{
    cmsg_status_code ret = CMSG_STATUS_CODE_SUCCESS;
    int count = 0;
    int nbytes = 0;

    struct timeval timeout = { 1, 0 };
    fd_set read_fds;
    int maxfd;

    FD_ZERO (&read_fds);
    FD_SET (socket, &read_fds);
    maxfd = socket;

    /* Do select() on the socket to prevent it to go to usleep instantaneously in the loop
     * if the data is not yet available.*/
    select (maxfd + 1, &read_fds, NULL, NULL, &timeout);

    /* Peek until data arrives, this allows us to timeout and recover if no data arrives. */
    while ((count < maxLoop) && (nbytes != (int) sizeof (cmsg_header)))
    {
        nbytes = recv_wrapper (recv_wrapper_data, header_received,
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
        usleep (1000);
        count++;
    }

    if (count >= maxLoop)
    {
        // Report the failure and try to recover
        CMSG_LOG_TRANSPORT_ERROR (transport,
                                  "Receive timed out socket %d nbytes was %d last error %s",
                                  socket, nbytes, strerror (errno));

        ret = CMSG_STATUS_CODE_SERVICE_FAILED;
    }
    else if (count >= maxLoop / 2)
    {
        // This should not really happen, log it
        CMSG_LOG_TRANSPORT_ERROR (transport, "Receive looped %d times", count);
    }

    return ret;
}

/**
 * On the wire the packet stream consists of a special header and then the encrypted data.
 * The special header has a magic value and the length of the encrypted data. The
 * header information is in network byte order.
 */
static int32_t
cmsg_transport_crypto_header_peek (void *handle, uint32_t *msg_length)
{
    int nbytes = 0;
    uint8_t sec_header[8];
    uint32_t magic = 0;
    int32_t ret = CMSG_RET_OK;
    int *sock = (int *) handle;

    nbytes = recv (*sock, sec_header, sizeof (sec_header), MSG_PEEK);
    if (nbytes == sizeof (sec_header))
    {
        cmsg_transport_crypto_get32 (sec_header, &magic);
        cmsg_transport_crypto_get32 (&sec_header[4], msg_length);
        if (magic != CMSG_CRYPTO_MAGIC || *msg_length == 0)
        {
            CMSG_LOG_GEN_ERROR ("Receive error. Invalid crypto header.");
            ret = CMSG_RET_ERR;
        }
    }
    else if (nbytes == 0)
    {
        /* Normal socket shutdown case. */
        ret = CMSG_RET_CLOSED;
    }
    else
    {
        CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] recv socket %d error: %s\n",
                    sock, strerror (errno));
        ret = CMSG_RET_ERR;
    }

    return ret;
}

static int32_t
cmsg_transport_server_recv_process (uint8_t *buffer_data, cmsg_server *server,
                                    uint32_t extra_header_size, uint32_t dyn_len,
                                    int nbytes, cmsg_header *header_converted)
{
    cmsg_server_request server_request;
    int32_t ret;

    // Header is good so make use of it.
    server_request.msg_type = header_converted->msg_type;
    server_request.message_length = header_converted->message_length;
    server_request.method_index = UNDEFINED_METHOD;
    memset (&(server_request.method_name_recvd), 0, CMSG_SERVER_REQUEST_MAX_NAME_LENGTH);

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
        if (header_converted->message_length == 0 || nbytes == (int) dyn_len)
        {
            CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] received data\n");
            cmsg_buffer_print (buffer_data, dyn_len);
            server->server_request = &server_request;
            if (server->message_processor (server, buffer_data) != CMSG_RET_OK)
            {
                CMSG_LOG_TRANSPORT_ERROR (server->_transport,
                                          "Server message processing returned an error.");
            }

        }
        else
        {
            CMSG_LOG_TRANSPORT_ERROR (server->_transport, "No data on recv socket %d.",
                                      server->_transport->connection.sockets.client_socket);

            ret = CMSG_RET_ERR;
        }
    }

    return ret;
}

/* Receive message from server and process it. If 'peek' is set, then a header
 * is read first with MSG_PEEK and then read all together (header + data). */
static int32_t
_cmsg_transport_server_recv (cmsg_recv_func recv, void *handle, cmsg_server *server,
                             int peek, cmsg_header *peeked_header)
{
    int32_t ret = CMSG_RET_OK;
    int nbytes = 0;
    uint32_t dyn_len = 0;
    cmsg_header header_received;
    cmsg_header header_converted;
    uint8_t *recv_buffer = NULL;
    uint8_t buf_static[512];
    uint8_t *buffer_data;
    uint32_t extra_header_size = 0;
    cmsg_transport *transport = server->_transport;

    if (peeked_header)
    {
        memcpy (&header_received, peeked_header, sizeof (cmsg_header));
    }
    else if (peek)
    {
        nbytes = recv (handle, &header_received, sizeof (cmsg_header), MSG_PEEK);
    }
    else
    {
        nbytes = recv (handle, &header_received, sizeof (cmsg_header), MSG_WAITALL);
    }

    if (nbytes == sizeof (cmsg_header) || peeked_header)
    {
        if (cmsg_header_process (&header_received, &header_converted) != CMSG_RET_OK)
        {
            // Couldn't process the header for some reason
            CMSG_LOG_TRANSPORT_ERROR (transport,
                                      "Unable to process message header for during receive. Bytes: %d",
                                      nbytes);
            return CMSG_RET_ERR;
        }

        extra_header_size = header_converted.header_length - sizeof (cmsg_header);

        if (peek || peeked_header)
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
            recv_buffer = (uint8_t *) CMSG_CALLOC (1, dyn_len);
        }
        else
        {
            recv_buffer = (uint8_t *) buf_static;
            memset (recv_buffer, 0, sizeof (buf_static));
        }

        // read the message
        if (peek || peeked_header)
        {
            nbytes = recv (handle, recv_buffer, dyn_len, MSG_WAITALL);
            buffer_data = recv_buffer + sizeof (cmsg_header);
        }
        else
        {
            //Even if no packed data, TLV header should be read.
            if (header_converted.message_length + extra_header_size)
            {
                nbytes = recv (handle, recv_buffer, dyn_len, MSG_WAITALL);
            }
            else
            {
                nbytes = 0;
            }
            buffer_data = recv_buffer;
        }

        ret = cmsg_transport_server_recv_process (buffer_data, server, extra_header_size,
                                                  dyn_len, nbytes, &header_converted);
        if (recv_buffer != buf_static)
        {
            CMSG_FREE (recv_buffer);
            recv_buffer = NULL;
        }

    }
    else if (nbytes > 0)
    {
        CMSG_LOG_TRANSPORT_ERROR (transport,
                                  "Bad header on recv socket %d. Number: %d",
                                  transport->connection.sockets.client_socket, nbytes);

        // TEMP to keep things going
        recv_buffer = (uint8_t *) CMSG_CALLOC (1, nbytes);
        nbytes = recv (handle, recv_buffer, nbytes, MSG_WAITALL);
        CMSG_FREE (recv_buffer);
        recv_buffer = NULL;
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
            CMSG_LOG_TRANSPORT_ERROR (transport,
                                      "Receive error for socket %d. Error: %s.",
                                      transport->connection.sockets.client_socket,
                                      strerror (errno));
        }

        ret = CMSG_RET_ERR;
    }

    return ret;
}

static int32_t
cmsg_transport_server_recv_crypto_msg (cmsg_server *server, int32_t msg_length,
                                       uint8_t *buffer)
{
    int decoded_bytes = 0;
    uint32_t dyn_len = 0;
    cmsg_header *header_received;
    cmsg_header header_converted;
    uint8_t *decoded_data;
    uint8_t *buffer_data;
    uint8_t buf_static[512];
    uint32_t extra_header_size = 0;
    int32_t ret = CMSG_RET_OK;
    int sock = server->_transport->connection.sockets.client_socket;

    if (msg_length < sizeof (buf_static))
    {
        decoded_data = buf_static;
    }
    else
    {
        decoded_data = (uint8_t *) CMSG_CALLOC (1, msg_length);
        if (decoded_data == NULL)
        {
            return CMSG_RET_ERR;
        }
    }

    decoded_bytes =
        server->_transport->config.socket.crypto.decrypt (sock, buffer, msg_length,
                                                          decoded_data, msg_length);
    if (decoded_bytes == 0)
    {
        if (decoded_data != buf_static)
        {
            CMSG_FREE (decoded_data);
        }
        return CMSG_RET_OK;
    }

    if (decoded_bytes >= (int) sizeof (cmsg_header))
    {
        header_received = (cmsg_header *) decoded_data;

        if (cmsg_header_process (header_received, &header_converted) != CMSG_RET_OK)
        {
            // Couldn't process the header for some reason
            CMSG_LOG_TRANSPORT_ERROR (server->_transport,
                                      "Unable to process message header for server recv. Bytes: %d",
                                      decoded_bytes);
            if (decoded_data != buf_static)
            {
                CMSG_FREE (decoded_data);
            }
            return CMSG_RET_ERR;
        }

        extra_header_size = header_converted.header_length - sizeof (cmsg_header);

        // Make sure any extra header is received.
        dyn_len = header_converted.message_length + extra_header_size;
        if (dyn_len + sizeof (cmsg_header) > decoded_bytes)
        {
            // Couldn't process message. The message is too large
            CMSG_LOG_TRANSPORT_ERROR (server->_transport,
                                      "Unable to process message for server recv. Bytes: %zu > %d",
                                      dyn_len + sizeof (cmsg_header), decoded_bytes);
            if (decoded_data != buf_static)
            {
                CMSG_FREE (decoded_data);
            }
            return CMSG_RET_ERR;
        }
        buffer_data = decoded_data + sizeof (cmsg_header);

        ret = cmsg_transport_server_recv_process (buffer_data, server, extra_header_size,
                                                  dyn_len, dyn_len, &header_converted);
    }

    if (decoded_data != buf_static)
    {
        CMSG_FREE (decoded_data);
    }
    return ret;
}

static int32_t
_cmsg_transport_server_crypto_recv (cmsg_recv_func recv, void *handle, cmsg_server *server)
{
    int32_t ret = CMSG_RET_OK;
    int nbytes = 0;
    uint8_t *buffer = NULL;
    uint8_t buf_static[512];
    uint32_t msg_length = 0;

    CMSG_DEBUG (CMSG_INFO,
                "[TRANSPORT] server->accecpted_client_socket %d\n",
                server->_transport->connection.sockets.client_socket);

    ret = cmsg_transport_crypto_header_peek (handle, &msg_length);
    if (ret != CMSG_RET_OK)
    {
        if (ret == CMSG_RET_CLOSED)
        {
            /* close the socket so that the associated SA gets freed */
            cmsg_server_close_wrapper (server);
        }
        return ret;
    }

    if (msg_length < sizeof (buf_static))
    {
        buffer = buf_static;
    }
    else
    {
        buffer = (uint8_t *) CMSG_CALLOC (1, msg_length);
        if (buffer == NULL)
        {
            CMSG_LOG_TRANSPORT_ERROR (server->_transport,
                                      "Failed to allocate buffer memory (%d)", msg_length);
            return CMSG_RET_ERR;
        }
    }

    nbytes = recv (handle, buffer, msg_length, MSG_WAITALL);
    if (nbytes == msg_length)
    {
        ret = cmsg_transport_server_recv_crypto_msg (server, msg_length, buffer);
    }
    else if (nbytes > 0)
    {
        CMSG_LOG_TRANSPORT_ERROR (server->_transport,
                                  "Bad msg on recv socket %d. Number: %d - %d",
                                  server->_transport->connection.sockets.client_socket,
                                  nbytes, msg_length);
        ret = CMSG_RET_ERR;
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
            CMSG_LOG_TRANSPORT_ERROR (server->_transport,
                                      "Receive error for socket %d. Error: %s.",
                                      server->_transport->connection.sockets.client_socket,
                                      strerror (errno));
        }

        ret = CMSG_RET_ERR;
    }

    if (buffer != buf_static)
    {
        CMSG_FREE (buffer);
    }

    return ret;
}

/* Receive message from server and process it */
int32_t
cmsg_transport_server_recv (cmsg_recv_func recv, void *handle, cmsg_server *server,
                            cmsg_header *header_received)
{
    if (server->_transport->use_crypto)
    {
        return _cmsg_transport_server_crypto_recv (recv, handle, server);
    }
    return _cmsg_transport_server_recv (recv, handle, server, FALSE, header_received);
}


/* Receive message from server (by peeking the header first) and process it */
int32_t
cmsg_transport_server_recv_with_peek (cmsg_recv_func recv, void *handle,
                                      cmsg_server *server)
{
    return _cmsg_transport_server_recv (recv, handle, server, TRUE, NULL);
}

static cmsg_status_code
_cmsg_transport_client_recv (cmsg_recv_func recv, void *handle, cmsg_transport *transport,
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

    *messagePtPt = NULL;

    nbytes = recv (handle, &header_received, sizeof (cmsg_header), MSG_WAITALL);

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
                cmsg_client_close_wrapper (transport);
                CMSG_LOG_TRANSPORT_ERROR (transport,
                                          "Couldn't allocate memory for server reply (TLV + message), closed the socket");
                return CMSG_STATUS_CODE_SERVICE_FAILED;
            }
        }
        else
        {
            recv_buffer = (uint8_t *) buf_static;
            memset (recv_buffer, 0, sizeof (buf_static));
        }

        //just recv the rest of the data to clear the socket
        nbytes = recv (handle, recv_buffer, dyn_len, MSG_WAITALL);

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
                                      transport->connection.sockets.client_socket,
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
                                  transport->connection.sockets.client_socket, nbytes);

        // TEMP to keep things going
        recv_buffer = (uint8_t *) CMSG_CALLOC (1, nbytes);
        nbytes = recv (handle, recv_buffer, nbytes, MSG_WAITALL);
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
                        transport->connection.sockets.client_socket, strerror (errno));
            return CMSG_STATUS_CODE_SERVER_CONNRESET;
        }
        else
        {
            CMSG_LOG_TRANSPORT_ERROR (transport, "Recv error. Socket:%d Error:%s",
                                      transport->connection.sockets.client_socket,
                                      strerror (errno));
        }
    }

    return CMSG_STATUS_CODE_SERVICE_FAILED;
}

static cmsg_status_code
_cmsg_transport_client_recv_crypto_msg (cmsg_transport *transport,
                                        const ProtobufCServiceDescriptor *descriptor,
                                        int32_t msg_length, uint8_t *buffer,
                                        ProtobufCMessage **messagePtPt)
{
    uint32_t dyn_len = 0;
    cmsg_header *header_received;
    cmsg_header header_converted;
    uint8_t *msg_data = NULL;
    uint8_t *decoded_data;
    uint8_t buf_static[512];
    int decoded_bytes = 0;
    const ProtobufCMessageDescriptor *desc;
    uint32_t extra_header_size;
    cmsg_server_request server_request;
    *messagePtPt = NULL;
    cmsg_status_code code = CMSG_STATUS_CODE_SUCCESS;
    int sock = transport->connection.sockets.client_socket;

    if (msg_length < sizeof (buf_static))
    {
        decoded_data = buf_static;
    }
    else
    {
        decoded_data = (uint8_t *) CMSG_CALLOC (1, msg_length);
        if (decoded_data == NULL)
        {
            CMSG_LOG_TRANSPORT_ERROR (transport,
                                      "Client failed to allocate buffer on socket %d",
                                      transport->connection.sockets.client_socket);
            return CMSG_STATUS_CODE_SERVICE_FAILED;
        }
    }

    decoded_bytes =
        transport->config.socket.crypto.decrypt (sock, buffer, msg_length,
                                                 decoded_data, msg_length);
    if (decoded_bytes >= (int) sizeof (cmsg_header))
    {
        header_received = (cmsg_header *) decoded_data;
        if (cmsg_header_process (header_received, &header_converted) != CMSG_RET_OK)
        {
            // Couldn't process the header for some reason
            CMSG_LOG_TRANSPORT_ERROR (transport,
                                      "Unable to process message header for client receive. Bytes:%d",
                                      decoded_bytes);
            if (decoded_data != buf_static)
            {
                CMSG_FREE (decoded_data);
            }
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
            if (decoded_data != buf_static)
            {
                CMSG_FREE (decoded_data);
            }
            return header_converted.status_code;
        }

        if (dyn_len + sizeof (cmsg_header) > msg_length)
        {
            cmsg_client_close_wrapper (transport);
            CMSG_LOG_TRANSPORT_ERROR (transport,
                                      "Received message is too large, closed the socket");
            if (decoded_data != buf_static)
            {
                CMSG_FREE (decoded_data);
            }
            return CMSG_STATUS_CODE_SERVICE_FAILED;
        }

        if (decoded_bytes - sizeof (cmsg_header) == (int) dyn_len)
        {
            extra_header_size = header_converted.header_length - sizeof (cmsg_header);
            // Set msg_data to take into account a larger header than we expected
            msg_data = decoded_data + sizeof (cmsg_header);

            cmsg_tlv_header_process (msg_data, &server_request, extra_header_size,
                                     descriptor);

            msg_data = msg_data + extra_header_size;
            CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] received response data\n");
            cmsg_buffer_print (msg_data, dyn_len);

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
                                                     msg_data);

                // Free the allocated buffer
                if (decoded_data != buf_static)
                {
                    CMSG_FREE (decoded_data);
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
                                      transport->connection.sockets.client_socket,
                                      dyn_len, msg_length, errno, strerror (errno));

        }
    }

    if (decoded_data != buf_static)
    {
        CMSG_FREE (decoded_data);
    }

    return code;
}

static cmsg_status_code
_cmsg_transport_client_crypto_recv (cmsg_recv_func recv, void *handle,
                                    cmsg_transport *transport,
                                    const ProtobufCServiceDescriptor *descriptor,
                                    ProtobufCMessage **messagePtPt)
{
    int nbytes = 0;
    uint8_t *buffer;
    uint8_t buf_static[512];
    uint32_t msg_length = 0;
    int32_t ret;
    cmsg_status_code code = CMSG_STATUS_CODE_SUCCESS;

    *messagePtPt = NULL;

    ret = cmsg_transport_crypto_header_peek (handle, &msg_length);
    if (ret != CMSG_RET_OK)
    {
        if (ret == CMSG_RET_CLOSED)
        {
            return CMSG_STATUS_CODE_CONNECTION_CLOSED;
        }
        return CMSG_STATUS_CODE_SERVICE_FAILED;
    }

    if (msg_length < sizeof (buf_static))
    {
        buffer = buf_static;
    }
    else
    {
        buffer = (uint8_t *) CMSG_CALLOC (1, msg_length);
        if (buffer == NULL)
        {
            CMSG_LOG_TRANSPORT_ERROR (transport,
                                      "Client failed to allocate buffer on socket %d",
                                      transport->connection.sockets.client_socket);
            return CMSG_STATUS_CODE_SERVICE_FAILED;
        }
    }

    nbytes = recv (handle, buffer, msg_length, MSG_WAITALL);

    if (nbytes == msg_length)
    {
        code = _cmsg_transport_client_recv_crypto_msg (transport, descriptor, msg_length,
                                                       buffer, messagePtPt);
    }
    else if (nbytes > 0)
    {
        /* Didn't receive all of the CMSG header. */
        CMSG_LOG_TRANSPORT_ERROR (transport,
                                  "Bad header length for recv. Socket:%d nbytes:%d",
                                  transport->connection.sockets.client_socket, nbytes);
        ret = CMSG_STATUS_CODE_SERVICE_FAILED;
    }
    else if (nbytes == 0)
    {
        //Normal socket shutdown case. Return other than TRANSPORT_OK to
        //have socket removed from select set.
        ret = CMSG_STATUS_CODE_CONNECTION_CLOSED;
    }
    else
    {
        if (errno == ECONNRESET)
        {
            CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] recv socket %d error: %s\n",
                        transport->connection.sockets.client_socket, strerror (errno));
            code = CMSG_STATUS_CODE_SERVER_CONNRESET;
        }
        else
        {
            CMSG_LOG_TRANSPORT_ERROR (transport, "Recv error. Socket:%d Error:%s",
                                      transport->connection.sockets.client_socket,
                                      strerror (errno));
            code = CMSG_STATUS_CODE_SERVICE_FAILED;
        }
    }

    if (buffer != buf_static)
    {
        CMSG_FREE (buffer);
    }

    return code;
}

/* Receive message from a client and process it */
int32_t
cmsg_transport_client_recv (cmsg_recv_func recv, void *handle, cmsg_transport *transport,
                            const ProtobufCServiceDescriptor *descriptor,
                            ProtobufCMessage **messagePtPt)
{
    if (transport->use_crypto)
    {
        return _cmsg_transport_client_crypto_recv (recv, handle, transport,
                                                   descriptor, messagePtPt);
    }
    return _cmsg_transport_client_recv (recv, handle, transport, descriptor, messagePtPt);
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

void
cmsg_transport_enable_crypto (cmsg_transport *transport, cmsg_socket *config)
{
    transport->config.socket.crypto.encrypt = config->crypto.encrypt;
    transport->config.socket.crypto.decrypt = config->crypto.decrypt;
    transport->config.socket.crypto.close = config->crypto.close;
    transport->config.socket.crypto.accept = config->crypto.accept;
    transport->config.socket.crypto.connect = config->crypto.connect;
    transport->use_crypto = TRUE;
}
