#include "protobuf-c-cmsg-transport.h"
#include "protobuf-c-cmsg-client.h"
#include "protobuf-c-cmsg-server.h"


//This func can be used for ONEWAY_TIPC as well
static int32_t
cmsg_transport_tipc_connect (cmsg_client *client)
{
  if (client == NULL)
    return 0;

  client->socket = socket (client->transport->family, SOCK_STREAM, 0);

  if (client->socket < 0)
  {
    DEBUG ("error creating socket: %s\n", strerror (errno));
    return 0;
  }
  if (connect (client->socket, (struct sockaddr*)&client->transport->sockaddr.tipc, sizeof (client->transport->sockaddr.tipc)) < 0)
  {
    if (errno == EINPROGRESS)
    {
      //TODO
    }
    close (client->socket);
    client->socket = 0;
    DEBUG ("error connecting to remote host: %s\n", strerror (errno));
    return 0;
  }
  else
  {
    client->state = CMSG_CLIENT_STATE_CONNECTED;
    DEBUG (" successfully connected\n");
    return 0;
  }
}



static int32_t
cmsg_transport_tipc_listen (cmsg_server* server)
{
  int32_t yes = 1; // for setsockopt() SO_REUSEADDR, below
  int32_t listening_socket = -1;
  int32_t ret = 0;
  socklen_t addrlen  = sizeof (cmsg_socket_address);
  cmsg_transport *transport = NULL;

  if (server == NULL)
    return 0;

  transport = server->transport;
  listening_socket = socket (transport->family, SOCK_STREAM, 0);
  if (listening_socket == -1 )
  {
    DEBUG ("[SERVER] socket failed with: %s\n", strerror(errno));
    return -1;
  }

  ret = setsockopt (listening_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int32_t));
  if (ret == -1)
  {
    DEBUG ("[SERVER] setsockopt failed with: %s\n", strerror(errno));
    close (listening_socket);
    return -1;
  }

  ret = bind (listening_socket, &transport->sockaddr.generic, addrlen);
  if (ret < 0)
  {
    DEBUG ("[SERVER] bind failed with: %s\n", strerror(errno));
    close (listening_socket);
    return -1;
  }

  ret = listen (listening_socket, 10);
  if (ret < 0)
  {
    DEBUG ("[SERVER] listen failed with: %s\n", strerror(errno));
    close (listening_socket);
    return -1;
  }

  server->listening_socket = listening_socket;

  DEBUG ("[SERVER] listening on tipc socket: %d\n", listening_socket);
  DEBUG ("[SERVER] listening on tipc type: %d\n", server->transport->sockaddr.tipc.addr.name.name.type);
  DEBUG ("[SERVER] listening on tipc instance: %d\n", server->transport->sockaddr.tipc.addr.name.name.instance);
  DEBUG ("[SERVER] listening on tipc domain: %d\n", server->transport->sockaddr.tipc.addr.name.domain);
  DEBUG ("[SERVER] listening on tipc scope: %d\n", server->transport->sockaddr.tipc.scope);
  return 0;
}



static int32_t
cmsg_transport_tipc_server_recv (int32_t socket, cmsg_server* server)
{
  int32_t client_len;
  cmsg_transport client_transport;
  int32_t nbytes;
  int32_t dyn_len;
  int32_t ret = 0;
  cmsg_header_request header_received;
  cmsg_header_request header_converted;
  cmsg_server_request server_request;
  uint8_t* buffer = 0;
  uint8_t buf_static[512];

  if (!server || socket < 0)
  {
    return -1;
  }

  client_len = sizeof (client_transport.sockaddr.in);
  server->client_socket = accept (socket,
                                 (struct sockaddr *)&client_transport.sockaddr.in,
                                 &client_len);

  if (server->client_socket <= 0)
  {
    DEBUG ("accept failed\n");
    DEBUG ("server->client_socket = %d\n", server->client_socket);
    return -1;
  }

  DEBUG (" server->accecpted_client_socket %d\n", server->client_socket);

  nbytes = recv (server->client_socket, &header_received, sizeof (cmsg_header_request), MSG_WAITALL);
  if (nbytes == sizeof (cmsg_header_request))
  {
    //we have little endian on the wire
    header_converted.method_index = cmsg_common_uint32_from_le(header_received.method_index);
    header_converted.message_length = cmsg_common_uint32_from_le(header_received.message_length);
    header_converted.request_id = header_received.request_id;

    server_request.message_length = cmsg_common_uint32_from_le(header_received.message_length);
    server_request.method_index = cmsg_common_uint32_from_le(header_received.method_index);
    server_request.request_id = header_received.request_id;
    server_request.client_socket = server->client_socket;

    DEBUG ("received header\n");
    cmsg_debug_buffer_print ((void*)&header_received, sizeof (cmsg_header_request));

    DEBUG ("method_index   host: %d, wire: %d\n", header_converted.method_index, header_received.method_index);
    DEBUG ("message_length host: %d, wire: %d\n", header_converted.message_length, header_received.message_length);
    DEBUG ("request_id     host: %d, wire: %d\n", header_converted.request_id, header_received.request_id);

    // read the message
    dyn_len = header_converted.message_length;
    if (dyn_len > sizeof buf_static)
    {
      buffer = malloc (dyn_len);
    }
    else
    {
      buffer = (void*)buf_static;
    }
    nbytes = recv (server->client_socket, buffer, dyn_len, MSG_WAITALL);
    if (nbytes == dyn_len)
    {

      DEBUG ("received data\n");
      cmsg_debug_buffer_print(buffer, dyn_len);
      server->server_request = &server_request;

      if (cmsg_server_message_processor (server, buffer))
        DEBUG ("message processing returned an error\n");
    }
    else
    {
      DEBUG ("recv socket %d no data\n", server->client_socket);
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
    DEBUG ("recv socket %d bad header nbytes %d\n", server->client_socket, nbytes );
    // TEMP to keep things going
    buffer = malloc (nbytes);
    nbytes = recv (server->client_socket, buffer , nbytes, MSG_WAITALL);
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
      DEBUG ("recv socket %d error: %s\n", server->client_socket, strerror(errno) );
    }
    ret = 0;
  }
  return ret;
}


static ProtobufCMessage*
cmsg_transport_tipc_client_recv (cmsg_client* client)
{
  int32_t nbytes;
  int32_t dyn_len;
  ProtobufCMessage* ret = NULL;
  cmsg_header_response header_received;
  cmsg_header_response header_converted;
  uint8_t* buffer = 0;
  uint8_t buf_static[512];

  if (!client)
  {
    return 0;
  }

  nbytes = recv (client->socket, &header_received, sizeof (cmsg_header_response), MSG_WAITALL);
  if (nbytes == sizeof (cmsg_header_response))
  {
    //we have little endian on the wire
    header_converted.status_code = cmsg_common_uint32_from_le (header_received.status_code);
    header_converted.method_index = cmsg_common_uint32_from_le (header_received.method_index);
    header_converted.message_length = cmsg_common_uint32_from_le (header_received.message_length);
    header_converted.request_id = header_received.request_id;

    DEBUG (" received response header\n");
    cmsg_debug_buffer_print ((void*)&header_received, sizeof (cmsg_header_response));

    DEBUG (" status_code    host: %d, wire: %d\n", header_converted.status_code, header_received.status_code);
    DEBUG (" method_index   host: %d, wire: %d\n", header_converted.method_index, header_received.method_index);
    DEBUG (" message_length host: %d, wire: %d\n", header_converted.message_length, header_received.message_length);
    DEBUG (" request_id     host: %d, wire: %d\n", header_converted.request_id, header_received.request_id);

    if (header_converted.status_code != CMSG_STATUS_CODE_SUCCESS)
    {
      DEBUG (" server could not process message correctly\n");
      DEBUG (" todo: handle this case better\n");
    }

    // read the message
    dyn_len = header_converted.message_length;

    if (dyn_len > sizeof buf_static)
    {
      buffer = malloc (dyn_len);
    }
    else
    {
      buffer = (void*)buf_static;
    }
    nbytes = recv (client->socket, buffer, dyn_len, MSG_WAITALL);

    if (nbytes == dyn_len)
    {
      DEBUG (" received response data\n");
      cmsg_debug_buffer_print (buffer, dyn_len);

      //todo: call cmsg_client_response_message_processor
      ProtobufCMessage *message = 0;
      ProtobufCAllocator *allocator = (ProtobufCAllocator*)client->allocator;

      DEBUG (" unpacking response message\n");

      message = protobuf_c_message_unpack (client->descriptor->methods[header_converted.method_index].output,
          allocator,
          header_converted.message_length,
          buffer);

      if (message == NULL)
      {
        DEBUG ("[SERVER] error unpacking response message\n");
        return NULL;
      }
      return message;
    }
    else
    {
      DEBUG (" recv socket %d no data\n", client->socket);
      ret = 0;
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
    DEBUG (" recv socket %d bad header nbytes %d\n", client->socket, nbytes );

    // TEMP to keep things going
    buffer = malloc (nbytes);
    nbytes = recv (client->socket, buffer , nbytes, MSG_WAITALL);
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
      DEBUG (" recv socket %d error: %s\n", client->socket, strerror(errno) );
    }
    ret = 0;
  }
  return 0;

}


static  int32_t
cmsg_transport_tipc_send (int32_t socket, void *buff, int length, int flag)
{
  return (send (socket, buff, length, flag));
}



void
cmsg_transport_tipc_init(cmsg_transport *transport)
{
  if (transport == NULL)
    return;

  transport->family = PF_TIPC;
  transport->sockaddr.generic.sa_family = PF_TIPC;
  transport->connect = cmsg_transport_tipc_connect;
  transport->listen = cmsg_transport_tipc_listen;
  transport->server_recv = cmsg_transport_tipc_server_recv;
  transport->client_recv = cmsg_transport_tipc_client_recv;
  transport->send = cmsg_transport_tipc_send;
  transport->closure = cmsg_server_closure_rpc;
  transport->invoke = cmsg_client_invoke_rpc;
}

