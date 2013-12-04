#include "protobuf-c-cmsg-transport.h"
#include "protobuf-c-cmsg-server.h"

extern void cmsg_transport_oneway_udt_init (cmsg_transport *transport);


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

    default:
        DEBUG (CMSG_ERROR, "[TRANSPORT] transport type not supported\n");
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
        return 1;
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

    DEBUG (CMSG_INFO,
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

        if (cmsg_header_process (&header_received, &header_converted) != CMSG_RET_OK)
        {
            // Couldn't process the header for some reason
            CMSG_LOG_ERROR ("[TRANSPORT] server receive couldn't process msg header");
            return CMSG_RET_ERR;
        }

        // Header is good so make use of it.
        server_request.msg_type = header_converted.msg_type;
        server_request.message_length = header_converted.message_length;
        server_request.method_index = header_converted.method_index;

        if (peek)
        {
            // packet size is determined by header_length + message_length.
            // header_length may be greater than sizeof (cmsg_header)
            dyn_len = header_converted.message_length + header_converted.header_length;
        }
        else
        {
            // Make sure any extra header is received.
            dyn_len = header_converted.message_length
                + header_converted.header_length - sizeof (cmsg_header);
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
            nbytes = recv (handle, buffer, dyn_len, 0);
            buffer_data = buffer + header_converted.header_length;
        }
        else
        {
            //just recv more data when the packed message length is greater zero
            if (header_converted.message_length)
                nbytes = recv (handle, buffer, dyn_len, MSG_WAITALL);
            else
                nbytes = 0;
            buffer_data = buffer;
        }

        // Process any message that has no more length or we have received what
        // we expected to from the socket
        if (header_converted.message_length == 0 || nbytes == (int) dyn_len)
        {
            DEBUG (CMSG_INFO, "[TRANSPORT] received data\n");

            cmsg_buffer_print (buffer_data, dyn_len);
            server->server_request = &server_request;

            if (server->message_processor (server, buffer_data) != CMSG_RET_OK)
                CMSG_LOG_ERROR ("[TRANSPORT] message processing returned an error");
        }
        else
        {
            CMSG_LOG_ERROR ("[TRANSPORT] recv socket %d no data",
                            server->connection.sockets.client_socket);

            ret = CMSG_RET_ERR;
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
        CMSG_LOG_ERROR ("[TRANSPORT] recv socket %d bad header nbytes %d",
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
        ret = CMSG_RET_ERR;
    }
    else
    {
        if (errno != ECONNRESET)
        {
            CMSG_LOG_ERROR ("[TRANSPORT] recv socket %d error: %s",
                            server->connection.sockets.client_socket, strerror (errno));
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
