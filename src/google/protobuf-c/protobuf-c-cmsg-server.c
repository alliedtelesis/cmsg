#include "protobuf-c-cmsg-server.h"


cmsg_server*
cmsg_server_new (cmsg_transport*   transport,
                 ProtobufCService* service)
{
  int32_t yes = 1; // for setsockopt() SO_REUSEADDR, below
  int32_t listening_socket = -1;
  int32_t ret = 0;
  socklen_t addrlen  = sizeof (cmsg_socket_address);
  cmsg_server* server = 0;
  
  server = malloc (sizeof(cmsg_server));
  server->transport = 0;
  server->service = 0;
  server->listening_socket = 0;
  server->client_socket = 0;
  server->allocator = &protobuf_c_default_allocator; //initialize alloc and free for message_unpack() and message_free()

  if (!transport)
    {
      DEBUG ("[SERVER] transport not defined\n");
      free(server);
      server = 0;
      return 0;
    }

  //pass our transport type
  server->transport = transport;

  if (!service)
    {
      DEBUG ("[SERVER] service not defined\n");
      free(server);
      server = 0;
      return 0;
    }

  server->service = service;

  DEBUG ("[SERVER] creating new server with type: %d\n", transport->type);

  // get the server_socket
  listening_socket = socket (transport->family, SOCK_STREAM, 0);
  if (listening_socket == -1 )
  {
    DEBUG ("[SERVER] socket failed with: %s\n", strerror(errno));
    free (server);
    server = 0;
    return 0;
  }

  // lose the pesky "address already in use" error message
  ret = setsockopt (listening_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int32_t));
  if (ret == -1)
  {
    DEBUG ("[SERVER] setsockopt failed with: %s\n", strerror(errno));
    close (listening_socket);
    free (server);
    server = 0;
    return 0;
  }

  if (server->transport->family == AF_UNIX)
    addrlen = sizeof (struct sockaddr_un);

  ret = bind (listening_socket, &transport->sockaddr.generic, addrlen);
  if (ret < 0)
  {
    DEBUG ("[SERVER] bind failed with: %s\n", strerror(errno));
    close (listening_socket);
    free (server);
    server = 0;
    return 0;
  }

  ret = listen (listening_socket, 10);
  if (ret < 0)
  {
    DEBUG ("[SERVER] listen failed with: %s\n", strerror(errno));
    close (listening_socket);
    free (server);
    server = 0;
    return 0;
  }

  server->listening_socket = listening_socket;

  if (server->transport->type == CMSG_TRANSPORT_RPC_TCP)
    {
      DEBUG ("[SERVER] listening on tcp socket: %d\n", listening_socket);
      DEBUG ("[SERVER] listening on port: %d\n", (int)ntohs(server->transport->sockaddr.in.sin_port));
    }
  else if (server->transport->type == CMSG_TRANSPORT_RPC_TIPC)
    {
      DEBUG ("[SERVER] listening on tipc socket: %d\n", listening_socket);
      DEBUG ("[SERVER] listening on tipc type: %d\n", server->transport->sockaddr.tipc.addr.name.name.type);
      DEBUG ("[SERVER] listening on tipc instance: %d\n", server->transport->sockaddr.tipc.addr.name.name.instance);
      DEBUG ("[SERVER] listening on tipc domain: %d\n", server->transport->sockaddr.tipc.addr.name.domain);
      DEBUG ("[SERVER] listening on tipc scope: %d\n", server->transport->sockaddr.tipc.scope);
    }
  else
    {
      DEBUG ("[SERVER] listening on unknow socket: %d\n", listening_socket);
      DEBUG ("[SERVER] debug not implemented for this transport type\n");
    }

  return server;
}


int32_t
cmsg_server_destroy (cmsg_server *server)
{
  if (!server)
    {
      DEBUG ("[SERVER] server not defined\n");
      return 0;
    }

  if (!server->service)
    {
      DEBUG ("[SERVER] service not defined\n");
      return 0;
    }

  free (server);
  server = 0;
  return 0;
}


int
cmsg_server_get_socket (cmsg_server *server)
{
  int socket = 0;

  if (server->listening_socket)
    {
      socket = server->listening_socket;
    }

  return socket;
}


int32_t
cmsg_server_receive (cmsg_server* server,
                     int32_t      server_socket)
{
  int32_t nbytes;
  int32_t dyn_len;
  int32_t ret = 0;
  cmsg_header_request header_received;
  cmsg_header_request header_converted;
  cmsg_transport client_transport;
  cmsg_server_request server_request;
  uint8_t* buffer = 0;
  uint8_t buf_static[512];
  uint32_t client_len = 0;
  
  if (!server_socket)
    {
      DEBUG("[SERVER] socket not defined\n");
      return 0;
    }

  if (!server)
    {
      DEBUG("[SERVER] server not defined\n");
      return 0;
    }

  client_len = sizeof (client_transport.sockaddr.in);

  //todo: change accept for different types
  server->client_socket = accept (server_socket,
                                  (struct sockaddr *)&client_transport.sockaddr.in,
                                  &client_len);

  if (server->client_socket <= 0)
    {
      DEBUG ("[SERVER] accept failed\n");
      DEBUG ("[SERVER] server->client_socket = %d\n", server->client_socket);
      return 0;
    }

  DEBUG ("[SERVER] server->accecpted_client_socket %d\n", server->client_socket);

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

      DEBUG ("[SERVER] received header\n");
      cmsg_debug_buffer_print ((void*)&header_received, sizeof (cmsg_header_request));

      DEBUG ("[SERVER] method_index   host: %d, wire: %d\n", header_converted.method_index, header_received.method_index);
      DEBUG ("[SERVER] message_length host: %d, wire: %d\n", header_converted.message_length, header_received.message_length);
      DEBUG ("[SERVER] request_id     host: %d, wire: %d\n", header_converted.request_id, header_received.request_id);

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
    
          DEBUG ("[SERVER] received data\n");
          cmsg_debug_buffer_print(buffer, dyn_len);

          if (cmsg_server_message_processor (server, buffer, &server_request))
            DEBUG ("[SERVER] message processing returned an error\n");
        }
      else
        {
          DEBUG ("[SERVER] recv socket %d no data\n", server->client_socket);
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
      DEBUG ("[SERVER] recv socket %d bad header nbytes %d\n", server->client_socket, nbytes );
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
          DEBUG ("[SERVER] recv socket %d error: %s\n", server->client_socket, strerror(errno) );
        }
      ret = 0;
    }
  return ret;
}


int32_t
cmsg_server_message_processor (cmsg_server*         server,
                               uint8_t*             buffer_data,
                               cmsg_server_request* server_request)
{
  ProtobufCMessage* message = 0;
  ProtobufCAllocator* allocator = (ProtobufCAllocator*)server->allocator;

  if (server_request->method_index >= server->service->descriptor->n_methods)
    {
      DEBUG ("[SERVER] the method index from read from the header seems to be to high\n");
      return 0;
   }

  if (!buffer_data)
    {
    DEBUG ("[SERVER] buffer not defined");
    return 0;
    }

  DEBUG ("[SERVER] unpacking message\n");

  message = protobuf_c_message_unpack (server->service->descriptor->methods[server_request->method_index].input,
                                       allocator,
                                       server_request->message_length,
                                       buffer_data);

  if (message == 0)
    {
      DEBUG ("[SERVER] error unpacking message\n");
      return 0;
    }

  //if rpc we are sending the response in the server response closure
  if (server->transport->type == CMSG_TRANSPORT_RPC_LOCAL ||
      server->transport->type == CMSG_TRANSPORT_RPC_TCP ||
      server->transport->type == CMSG_TRANSPORT_RPC_TIPC)
    {
      DEBUG ("[SERVER] invoking rpc method=%d\n", server_request->method_index);

      server->service->invoke (server->service,
                               server_request->method_index,
                               message,
                               cmsg_server_closure_rpc,
                               (void*)server_request);

      //todo: we need to handle errors from closure data
    }
  else if (server->transport->type == CMSG_TRANSPORT_ONEWAY_TCP ||
           server->transport->type == CMSG_TRANSPORT_ONEWAY_TIPC ||
           server->transport->type == CMSG_TRANSPORT_ONEWAY_CPG ||
           server->transport->type == CMSG_TRANSPORT_ONEWAY_CPUMAIL)
    {  
      DEBUG ("[SERVER] invoking oneway method=%d\n", server_request->method_index);

      server->service->invoke (server->service,
                               server_request->method_index,
                               message,
                               cmsg_server_closure_oneway,
                               (void*)server_request);
    }

  protobuf_c_message_free_unpacked (message, allocator);

  DEBUG ("[SERVER] end of message processor\n");
  return 0;
}


void
cmsg_server_closure_rpc (const ProtobufCMessage* message,
                         void*                   closure_data)
{
  cmsg_server_request* server_request = (cmsg_server_request*)closure_data;
  int ret = 0;

  if(!message)
    {
      DEBUG ("[SERVER] sending response without data\n");

      uint32_t header[4];
      header[0] = cmsg_common_uint32_to_le (CMSG_STATUS_CODE_SERVICE_FAILED);
      header[1] = cmsg_common_uint32_to_le (server_request->method_index);
      header[2] = 0;            /* no message */
      header[3] = server_request->request_id;

      DEBUG ("[SERVER] response header\n");

      cmsg_debug_buffer_print ((void*)&header, sizeof (header));

      ret = send (server_request->client_socket, &header, sizeof (header), 0);
      if (ret < sizeof (header))
        DEBUG ("[SERVER] sending if response failed send:%d of %ld\n", ret, sizeof (header));

      DEBUG ("[SERVER] shutting down socket\n");
      shutdown (server_request->client_socket, 2);

      DEBUG ("[SERVER] closing socket\n");
      close (server_request->client_socket);
    }
  else
    {
      DEBUG ("[SERVER] sending response with data\n");

      uint32_t packed_size = protobuf_c_message_get_packed_size(message);
      uint32_t header[4];
      header[0] = cmsg_common_uint32_to_le (CMSG_STATUS_CODE_SUCCESS);
      header[1] = cmsg_common_uint32_to_le (server_request->method_index);
      header[2] = cmsg_common_uint32_to_le (packed_size); //packesize
      header[3] = server_request->request_id;

      uint8_t *buffer = malloc (packed_size + sizeof (header));
      uint8_t *buffer_data = malloc (packed_size);

      memcpy ((void*)buffer, &header, sizeof (header));

      DEBUG("[SERVER] packing message\n");

      ret = protobuf_c_message_pack(message, buffer_data);
      if (ret < packed_size)
        {
          DEBUG ("[SERVER] packing response data failed packet:%d of %d\n", ret, packed_size);
          free (buffer);
          buffer = 0;
          free (buffer_data);
          buffer_data = 0;
          return;
        }

      memcpy ((void*)buffer + sizeof (header), (void*)buffer_data, packed_size);

      DEBUG ("[SERVER] response header\n");
      cmsg_debug_buffer_print ((void*)&header, sizeof (header));

      DEBUG ("[SERVER] response data\n");
      cmsg_debug_buffer_print ((void*)buffer + sizeof (header), packed_size);

      ret = send (server_request->client_socket, buffer, packed_size + sizeof (header), 0);
      if (ret < packed_size + sizeof (header))
        DEBUG ("[SERVER] sending if response failed send:%d of %ld\n", ret, packed_size + sizeof (header));

      DEBUG ("[SERVER] shutting down socket\n");
      shutdown(server_request->client_socket, 2);
      
      DEBUG ("[SERVER] closing socket\n");
      close(server_request->client_socket);

      free (buffer);
      buffer = 0;
      free (buffer_data);
      buffer_data = 0;
    }
  
  server_request->closure_response = 0;
}


void
cmsg_server_closure_oneway (const ProtobufCMessage* message,
                            void*                   closure_data)
{
  cmsg_server_request* server_request = (cmsg_server_request*)closure_data;
  //we are not sending a response in this transport mode
  DEBUG("[SERVER] nothing to do\n");

  server_request->closure_response = 0;
}
