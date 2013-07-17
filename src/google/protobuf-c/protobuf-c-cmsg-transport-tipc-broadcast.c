#include "protobuf-c-cmsg-transport.h"
#include "protobuf-c-cmsg-client.h"
#include "protobuf-c-cmsg-server.h"


/**
 * Creates the connectionless socket used to send messages using tipc.
 */
static int32_t
cmsg_transport_tipc_broadcast_connect (cmsg_client *client)
{
    DEBUG (CMSG_INFO, "[TRANSPORT] cmsg_transport_tipc_broadcast_connect\n");

    if (client == NULL)
      return 0;

    client->connection.socket = socket (client->transport->connection_info.sockaddr.family, SOCK_RDM, 0);

    if (client->connection.socket < 0)
    {
      client->state = CMSG_CLIENT_STATE_FAILED;
      DEBUG (CMSG_ERROR, "[TRANSPORT] error creating socket: %s\n", strerror (errno));
      return 0;
    }

    client->state = CMSG_CLIENT_STATE_CONNECTED;
    DEBUG (CMSG_INFO, "[TRANSPORT] successfully connected\n");

    return 0;
}


/**
 * Creates the connectionless socket used to receive tipc messages
 */
static int32_t
cmsg_transport_tipc_broadcast_listen (cmsg_server* server)
{
  int32_t listening_socket = -1;
  int32_t addrlen = 0;
  cmsg_transport *transport = NULL;

  if (server == NULL)
    return 0;

  DEBUG (CMSG_INFO,"[TRANSPORT] Creating listen socket\n");
  server->connection.sockets.listening_socket = 0;
  transport = server->transport;

  listening_socket = socket (transport->connection_info.sockaddr.family, SOCK_RDM, 0);
  if (listening_socket == -1 )
  {
    DEBUG (CMSG_ERROR,
           "[TRANSPORT] socket failed with: %s\n",
           strerror (errno));

    return -1;
  }

  //TODO: stk_tipc.c adds the addressing information here

  addrlen  = sizeof (transport->connection_info.sockaddr.addr.generic);
  /* bind the socket address (publishes the TIPC port name) */
  if (bind (listening_socket, &transport->connection_info.sockaddr.addr.generic, addrlen) != 0)
  {
    DEBUG (CMSG_ERROR, "[TRANSPORT] TIPC port could not be created\n");
    return -1;
  }

  //TODO: Do we need a listen?
  server->connection.sockets.listening_socket = listening_socket;
  //TODO: Add debug
  return 0;
}


/**
 * Receive a message sent by the client. This function peeks at the message
 * header on the socket, and uses the message length to read off the combined
 * message header and data. The data is then passed to the server for processing
 */
static int32_t
cmsg_transport_tipc_broadcast_server_recv (int32_t socket, cmsg_server* server)
{
  int32_t nbytes;
  int32_t dyn_len;
  int32_t ret = 0;
  int32_t addrlen = 0;
  cmsg_header_request header_received;
  cmsg_header_request header_converted;
  cmsg_server_request server_request;
  uint8_t* buffer = 0;
  uint8_t buf_static[512];
  cmsg_transport *transport = NULL;

  if (!server || socket < 0)
  {
    return -1;
  }

  addrlen = sizeof (struct sockaddr_tipc);
  transport = server->transport;

  nbytes = recvfrom (server->connection.sockets.listening_socket,
                     &header_received,
                     sizeof (cmsg_header_request),
                     MSG_PEEK,
                     (struct sockaddr *) &transport->connection_info.sockaddr.addr.tipc,
                     &addrlen);

  DEBUG (CMSG_INFO,
         "[TRANSPORT] Peeked at message, received %d bytes\n",
         nbytes);

  if (nbytes == sizeof (cmsg_header_request))
  {
    //we have little endian on the wire
    header_converted.method_index = cmsg_common_uint32_from_le (header_received.method_index);
    header_converted.message_length = cmsg_common_uint32_from_le (header_received.message_length);
    header_converted.request_id = header_received.request_id;

    server_request.message_length = cmsg_common_uint32_from_le (header_received.message_length);
    server_request.method_index = cmsg_common_uint32_from_le (header_received.method_index);
    server_request.request_id = header_received.request_id;

    DEBUG (CMSG_INFO, "[TRANSPORT] received header\n");
    cmsg_buffer_print ((void*) &header_received,
                             sizeof (cmsg_header_request));

    DEBUG (CMSG_INFO,
           "[TRANSPORT] method_index   host: %d, wire: %d\n",
           header_converted.method_index,
           header_received.method_index);

    DEBUG (CMSG_INFO,
           "[TRANSPORT] message_length host: %d, wire: %d\n",
           header_converted.message_length,
           header_received.message_length);

    DEBUG (CMSG_INFO,
           "[TRANSPORT] request_id     host: %d, wire: %d\n",
           header_converted.request_id,
           header_received.request_id);

    // read the message
    dyn_len = header_converted.message_length + sizeof (cmsg_header_request);
    DEBUG (CMSG_INFO, "[TRANSPORT] Data length: %d\n", dyn_len);
    if (dyn_len > sizeof (buf_static))
    {
      buffer = malloc (dyn_len);
    }
    else
    {
      buffer = (void*)buf_static;
    }

    nbytes = recvfrom (server->connection.sockets.listening_socket,
                       buffer,
                       dyn_len,
                       0,
                       (struct sockaddr *) &transport->connection_info.sockaddr.addr.tipc,
                       &addrlen);

    if (nbytes == dyn_len)
    {
      DEBUG (CMSG_INFO, "[TRANSPORT] received data\n");
      cmsg_buffer_print (buffer, dyn_len);
      server->server_request = &server_request;

      //TODO: Check sender id to see if valid sender
      //TODO: Do some virtual link checking
      if (server->message_processor (server, buffer + sizeof (cmsg_header_request)))
      {
        DEBUG (CMSG_ERROR,
               "[TRANSPORT] message processing returned an error\n");
      }
    }
    else
    {
      DEBUG (CMSG_INFO,
             "[TRANSPORT] recv socket %d no data\n",
             server->connection.sockets.client_socket);

      ret = -1;
    }

    if (buffer != (void*)buf_static)
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
           server->connection.sockets.listening_socket, nbytes);

    // TEMP to keep things going
    buffer = malloc (nbytes);
    nbytes = recvfrom (server->connection.sockets.listening_socket,
                       &buffer,
                       nbytes,
                       0,
                       (struct sockaddr *) &transport->connection_info.sockaddr.addr.tipc,
                       &addrlen);
    free (buffer);
    buffer = 0;
    ret = 0;
  }
  else if (nbytes == 0)
  {
    //Normal socket shutdown case. Return other than TRANSPORT_OK to
    //have socket removed from select set.
    ret = 0;
  }
  else
  {
    //Error while peeking at socket data.
    if (errno != ECONNRESET)
    {
      DEBUG (CMSG_ERROR,
             "[TRANSPORT] recv socket %d error: %s\n",
             server->connection.sockets.client_socket, strerror (errno));
    }
    ret = 0;
  }
  return ret;
}


/**
 * TIPC broadcast clients do not receive a reply to their messages. This
 * function therefore returns NULL. It should not be called by the client, but
 * it prevents a null pointer exception from occurring if no function is
 * defined
 */
static ProtobufCMessage*
cmsg_transport_tipc_broadcast_client_recv (cmsg_client* client)
{
    return NULL;
}


/**
 * Send the data in buff to the server specified in the clients transports
 * addressing structure. Does not block.
 */
static  int32_t
cmsg_transport_tipc_broadcast_client_send (cmsg_client *client, void *buff, int length, int flag)
{
    return (sendto (client->connection.socket,
                    buff,
                    length,
                    MSG_DONTWAIT,
                    (struct sockaddr *) &client->transport->connection_info.sockaddr.addr.tipc,
                    sizeof (struct sockaddr_tipc)));
}


/**
 * TIPC broadcast servers do not send replies to received messages. This
 * function therefore returns 0. It should not be called by the server, but
 * it prevents a null pointer exception from occurring if no function is
 * defined
 */
static  int32_t
cmsg_transport_tipc_broadcast_server_send (cmsg_server *server, void *buff, int length, int flag)
{
    return 0;
}


/**
 * Close the clients socket after a message has been sent.
 */
static void
cmsg_transport_tipc_broadcast_client_close (cmsg_client* client)
{
    DEBUG (CMSG_INFO, "[TRANSPORT] shutting down socket\n");
    shutdown (client->connection.socket, 2);

    DEBUG (CMSG_INFO, "[TRANSPORT] closing socket\n");
    close (client->connection.socket);
}


/**
 * This function is called by the server to close the socket that the server
 * has used to receive a message from a client. TIPC broadcast does not use a
 * dedicated socket to do this, instead it receives messages on its listening
 * socket. Therefore this function does nothing when called.
 */
static void
cmsg_transport_tipc_broadcast_server_close (cmsg_server* server)
{
    return;
}


/**
 * Return the servers listening socket
 */
static int
cmsg_transport_tipc_broadcast_server_get_socket (cmsg_server* server)
{
  return server->connection.sockets.listening_socket;
}


/**
 * Return the socket the client will use to send messages
 */
static int
cmsg_transport_tipc_broadcast_client_get_socket (cmsg_client* client)
{
  return client->connection.socket;
}


/**
 * Close the servers listening socket
 */
static void
cmsg_transport_tipc_broadcast_server_destroy (cmsg_server* server)
{
    DEBUG (CMSG_INFO, "[SERVER] Shutting down listening socket\n");
    shutdown (server->connection.sockets.listening_socket, 2);

    DEBUG (CMSG_INFO, "[SERVER] Closing listening socket\n");
    close (server->connection.sockets.listening_socket);
}


/**
 * Setup the transport structure with the appropriate function pointers for
 * TIPC broadcast, and transport family.
 */
void
cmsg_transport_tipc_broadcast_init (cmsg_transport *transport)
{
    if (transport == NULL)
        return;

    transport->connection_info.sockaddr.family = AF_TIPC;
    transport->connection_info.sockaddr.addr.tipc.family = AF_TIPC;

    transport->connect = cmsg_transport_tipc_broadcast_connect;
    transport->listen = cmsg_transport_tipc_broadcast_listen;
    transport->server_recv = cmsg_transport_tipc_broadcast_server_recv;
    transport->client_recv = cmsg_transport_tipc_broadcast_client_recv;
    transport->client_send = cmsg_transport_tipc_broadcast_client_send;
    transport->server_send = cmsg_transport_tipc_broadcast_server_send;
    transport->client_close = cmsg_transport_tipc_broadcast_client_close;
    transport->server_close = cmsg_transport_tipc_broadcast_server_close;
    transport->s_socket = cmsg_transport_tipc_broadcast_server_get_socket;
    transport->c_socket = cmsg_transport_tipc_broadcast_client_get_socket;
    transport->server_destroy = cmsg_transport_tipc_broadcast_server_destroy;

    transport->closure = cmsg_server_closure_oneway;
    transport->invoke = cmsg_client_invoke_oneway;
}
