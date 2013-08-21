#include "protobuf-c-cmsg-transport.h"
#include "protobuf-c-cmsg-server.h"


cmsg_transport *
cmsg_transport_new (cmsg_transport_type type)
{
    cmsg_transport *transport = 0;
    transport = malloc (sizeof (cmsg_transport));
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
            free (transport);
            transport = 0;
    }

    return transport;
}

int32_t
cmsg_transport_destroy (cmsg_transport *transport)
{
    if (transport)
    {
        free (transport);
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
    int32_t nbytes = 0;
    int32_t dyn_len = 0;
    cmsg_header_request header_received;
    cmsg_header_request header_converted;
    cmsg_server_request server_request;
    uint8_t *buffer = 0;
    uint8_t buf_static[512];
    uint8_t *buffer_data;

    DEBUG (CMSG_INFO,
           "[TRANSPORT] server->accecpted_client_socket %d\n",
           server->connection.sockets.client_socket);

    if (peek)
    {
        nbytes = recv (handle, &header_received, sizeof (cmsg_header_request), MSG_PEEK);
    }
    else
    {
        nbytes = recv (handle, &header_received, sizeof (cmsg_header_request), MSG_WAITALL);
    }

    if (nbytes == sizeof (cmsg_header_request))
    {
        //we have little endian on the wire
        header_converted.method_index =
            cmsg_common_uint32_from_le (header_received.method_index);
        header_converted.message_length =
            cmsg_common_uint32_from_le (header_received.message_length);
        header_converted.request_id = header_received.request_id;

        server_request.message_length =
            cmsg_common_uint32_from_le (header_received.message_length);
        server_request.method_index =
            cmsg_common_uint32_from_le (header_received.method_index);
        server_request.request_id = header_received.request_id;

        DEBUG (CMSG_INFO, "[TRANSPORT] received header\n");
        cmsg_buffer_print ((void *) &header_received, sizeof (cmsg_header_request));

        DEBUG (CMSG_INFO,
               "[TRANSPORT] method_index   host: %d, wire: %d\n",
               header_converted.method_index, header_received.method_index);

        DEBUG (CMSG_INFO,
               "[TRANSPORT] message_length host: %d, wire: %d\n",
               header_converted.message_length, header_received.message_length);

        DEBUG (CMSG_INFO,
               "[TRANSPORT] request_id     host: %d, wire: %d\n",
               header_converted.request_id, header_received.request_id);

        if (peek)
        {
            dyn_len = header_converted.message_length + sizeof (cmsg_header_request);
        }
        else
        {
            dyn_len = header_converted.message_length;
        }

        if (dyn_len > sizeof buf_static)
        {
            buffer = malloc (dyn_len);
        }
        else
        {
            buffer = (void *) buf_static;
        }

        // read the message
        if (peek)
        {
            nbytes = recv (handle, buffer, dyn_len, 0);
            buffer_data = buffer + sizeof (cmsg_header_request);
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

        if (nbytes == dyn_len)
        {
            DEBUG (CMSG_INFO, "[TRANSPORT] received data\n");

            cmsg_buffer_print (buffer_data, dyn_len);
            server->server_request = &server_request;

            if (server->message_processor (server, buffer_data))
                DEBUG (CMSG_ERROR, "[TRANSPORT] message processing returned an error\n");
        }
        else
        {
            DEBUG (CMSG_INFO,
                   "[TRANSPORT] recv socket %d no data\n",
                   server->connection.sockets.client_socket);

            ret = CMSG_RET_ERR;
        }
        if (buffer != buf_static)
        {
            if (buffer)
            {
                free (buffer);
                buffer = 0;
            }
        }
    }
    else if (nbytes > 0)
    {
        DEBUG (CMSG_ERROR,
               "[TRANSPORT] recv socket %d bad header nbytes %d\n",
               server->connection.sockets.client_socket, nbytes);

        // TEMP to keep things going
        buffer = malloc (nbytes);
        nbytes = recv (handle, buffer, nbytes, MSG_WAITALL);
        free (buffer);
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
            DEBUG (CMSG_ERROR,
                   "[TRANSPORT] recv socket %d error: %s\n",
                   server->connection.sockets.client_socket, strerror (errno));
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
