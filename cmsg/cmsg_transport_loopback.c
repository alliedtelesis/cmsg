/**
 * Describes the Loopback transport information.
 *
 * Loopback transport is used in cases where the application stills wants to call the
 * api functions but doesn't want the information to go over a transport.  Cases of use
 * for this are on products that do not support VCStack and therefore to keep the
 * application generic just the initialisation can be changed to allow the same api
 * and impls to be used.
 *
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#include "cmsg_transport.h"
#include "cmsg_client.h"
#include "cmsg_server.h"
#include "cmsg_error.h"


/******************************************************************************
 ******************** Client **************************************************
 *****************************************************************************/
/**
 * API always connects and therefore this should succeed.
 *
 * Returns 0 on success or a negative integer on failure.
 */
static int32_t
cmsg_transport_loopback_connect (cmsg_client *client)
{
    return 0;
}

/**
 * Nothing to send so return -1, shouldn't get here so it needs to be an error.
 */
static int32_t
cmsg_transport_loopback_client_send (cmsg_client *client, void *buff, int length, int flag)
{
    return -1;
}

/**
 * Close the socket on the client.
 */
static void
cmsg_transport_loopback_client_close (cmsg_transport *transport)
{
    if (transport->connection.sockets.client_socket != -1)
    {
        CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] shutting down socket\n");
        shutdown (transport->connection.sockets.client_socket, SHUT_RDWR);

        CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] closing socket\n");
        close (transport->connection.sockets.client_socket);

        transport->connection.sockets.client_socket = -1;
    }

    return;
}

/**
 * Return the client's socket.
 */
static int
cmsg_transport_loopback_client_get_socket (cmsg_transport *transport)
{
    return transport->connection.sockets.client_socket;
}

/**
 * Nothing to destroy
 */
static void
cmsg_transport_loopback_client_destroy (cmsg_transport *transport)
{
    /* destroy the server associated with the loopback client */
    if (transport && transport->config.loopback_server)
    {
        cmsg_server_destroy (transport->config.loopback_server);
        transport->config.loopback_server = NULL;
    }
}

/**
 * Loopback can't be congested so return FALSE
 */
uint32_t
cmsg_transport_loopback_is_congested (cmsg_transport *transport)
{
    return FALSE;
}

/**
 * This isn't supported on loopback at present, shouldn't be called so it
 * needs to be an error.
 */
int32_t
cmsg_transport_loopback_send_called_multi_threads_enable (cmsg_transport *transport,
                                                          uint32_t enable)
{
    // Don't support sending from multiple threads
    return -1;
}

/**
 * Sets the flag but it does nothing for this transport.
 */
int32_t
cmsg_transport_loopback_send_can_block_enable (cmsg_transport *transport,
                                               uint32_t send_can_block)
{
    transport->send_can_block = send_can_block;
    return 0;
}


int32_t
cmsg_transport_loopback_ipfree_bind_enable (cmsg_transport *transport,
                                            cmsg_bool_t use_ipfree_bind)
{
    /* not supported yet */
    return -1;
}


/******************************************************************************
 ******************** Server **************************************************
 *****************************************************************************/
/**
 * Nothing to listen to, so return 0 as the server always attempts to listen.
 */
static int32_t
cmsg_transport_loopback_listen (cmsg_server *server)
{
    return 0;
}

/**
 * Nothing to do - return -1 to make sure loopbacks can't do this.
 *
 */
static int32_t
cmsg_transport_loopback_server_recv (int32_t server_socket, cmsg_server *server)
{
    return -1;
}

/**
 * Server writes the response onto the pipe that client can read it off.
 */
static int32_t
cmsg_transport_loopback_server_send (cmsg_server *server, void *buff, int length, int flag)
{
    return write (server->_transport->connection.sockets.client_socket, buff, length);
}

/**
 * Close the socket on the server side.
 */
static void
cmsg_transport_loopback_server_close (cmsg_transport *transport)
{
    CMSG_DEBUG (CMSG_INFO, "[SERVER] shutting down socket\n");
    shutdown (transport->connection.sockets.client_socket, SHUT_RDWR);

    CMSG_DEBUG (CMSG_INFO, "[SERVER] closing socket\n");
    close (transport->connection.sockets.client_socket);

    return;
}

/**
 * We don't want the application listening to a socket (and there is none)
 * so return -1 so it will error.
 */
static int
cmsg_transport_loopback_server_get_socket (cmsg_server *server)
{
    return -1;
}

/**
 * Destroy server
 */
static void
cmsg_transport_loopback_server_destroy (cmsg_transport *transport)
{
    /* the loopback server is just used internally by the client. When we destroy the
     * server (when the client gets destroyed), make sure we close the pipe file descriptor */
    cmsg_transport_loopback_server_close (transport);
}


cmsg_status_code
cmsg_transport_loopback_client_recv (cmsg_client *client, ProtobufCMessage **messagePtPt)
{
    int nbytes = 0;
    uint32_t dyn_len = 0;
    cmsg_header header_received;
    cmsg_header header_converted;
    uint8_t *recv_buffer = 0;
    uint8_t buf_static[512];
    ProtobufCMessage *message = NULL;
    ProtobufCAllocator *allocator = client->allocator;
    const ProtobufCMessageDescriptor *desc;
    uint32_t extra_header_size;
    cmsg_server_request server_request;

    *messagePtPt = NULL;

    if (!client)
    {
        return CMSG_STATUS_CODE_SUCCESS;
    }

    nbytes =
        read (client->_transport->connection.sockets.client_socket, &header_received,
              sizeof (cmsg_header));
    if (nbytes == (int) sizeof (cmsg_header))
    {
        if (cmsg_header_process (&header_received, &header_converted) != CMSG_RET_OK)
        {
            // Couldn't process the header for some reason
            CMSG_LOG_TRANSPORT_ERROR (client->_transport,
                                      "Unable to process message header for client receive. Bytes:%d",
                                      nbytes);
            return CMSG_STATUS_CODE_SERVICE_FAILED;
        }

        CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] received response header\n");

        // read the message

        // There is no more data to read so exit.
        if (header_converted.message_length == 0)
        {
            // May have been queued, dropped or there was no message returned
            CMSG_DEBUG (CMSG_INFO,
                        "[TRANSPORT] received response without data. server status %d\n",
                        header_converted.status_code);
            return header_converted.status_code;
        }
        extra_header_size = header_converted.header_length - sizeof (cmsg_header);

        // Take into account that someone may have changed the size of the header
        // and we don't know about it, make sure we receive all the information.
        dyn_len = header_converted.message_length + extra_header_size;
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
        nbytes =
            read (client->_transport->connection.sockets.client_socket, recv_buffer,
                  dyn_len);

        if (nbytes == (int) dyn_len)
        {

            cmsg_tlv_header_process (recv_buffer, &server_request, extra_header_size,
                                     client->descriptor);

            recv_buffer = recv_buffer + extra_header_size;
            CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] received response data\n");

            cmsg_buffer_print (recv_buffer, dyn_len);

            CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] unpacking response message\n");

            desc = client->descriptor->methods[server_request.method_index].output;
            message =
                protobuf_c_message_unpack (desc, allocator,
                                           header_converted.message_length, recv_buffer);

            if (message == NULL)
            {
                CMSG_LOG_TRANSPORT_ERROR (client->_transport,
                                          "Error unpacking response message. Bytes:%d",
                                          header_converted.message_length);
                return CMSG_STATUS_CODE_SERVICE_FAILED;
            }
            *messagePtPt = message;
            return CMSG_STATUS_CODE_SUCCESS;
        }
        else
        {
            CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] recv socket %d no data\n",
                        client->_transport->connection.sockets.client_socket);
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
                        client->_transport->connection.sockets.client_socket,
                        strerror (errno));
            return CMSG_STATUS_CODE_SERVER_CONNRESET;
        }
        else
        {
            CMSG_LOG_TRANSPORT_ERROR (client->_transport,
                                      "Receive error for socket %d. Error: %s",
                                      client->_transport->connection.sockets.client_socket,
                                      strerror (errno));
        }
    }

    return CMSG_STATUS_CODE_SERVICE_FAILED;
}

void
cmsg_transport_loopback_init (cmsg_transport *transport)
{
    if (transport == NULL)
    {
        return;
    }

    transport->connect = cmsg_transport_loopback_connect;
    transport->listen = cmsg_transport_loopback_listen;
    transport->server_accept = NULL;
    transport->server_recv = cmsg_transport_loopback_server_recv;
    transport->client_recv = cmsg_transport_loopback_client_recv;
    transport->client_send = cmsg_transport_loopback_client_send;
    transport->server_send = cmsg_transport_loopback_server_send;
    transport->closure = cmsg_server_closure_rpc;
    transport->invoke_send = cmsg_client_invoke_send_direct;
    transport->invoke_recv = cmsg_client_invoke_recv_direct;
    transport->client_close = cmsg_transport_loopback_client_close;
    transport->server_close = cmsg_transport_loopback_server_close;

    transport->s_socket = cmsg_transport_loopback_server_get_socket;
    transport->c_socket = cmsg_transport_loopback_client_get_socket;

    transport->client_destroy = cmsg_transport_loopback_client_destroy;
    transport->server_destroy = cmsg_transport_loopback_server_destroy;

    transport->is_congested = cmsg_transport_loopback_is_congested;
    transport->send_called_multi_threads_enable =
        cmsg_transport_loopback_send_called_multi_threads_enable;
    transport->send_called_multi_enabled = FALSE;
    transport->send_can_block_enable = cmsg_transport_loopback_send_can_block_enable;
    transport->ipfree_bind_enable = cmsg_transport_loopback_ipfree_bind_enable;

    CMSG_DEBUG (CMSG_INFO, "%s: done\n", __FUNCTION__);
}
