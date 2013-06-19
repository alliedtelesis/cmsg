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

  if (!transport || !service)
    {
      DEBUG ("[SERVER] transport / service not defined\n");
      return NULL;
    }
  
  server = malloc (sizeof(cmsg_server));
  server->transport = transport;
  server->service = service;
  server->listening_socket = 0;
  server->client_socket = 0;
  server->allocator = &protobuf_c_default_allocator; //initialize alloc and free for message_unpack() and message_free()


  DEBUG ("[SERVER] creating new server with type: %d\n", transport->type);

  ret = transport->listen (server);

  if (ret < 0 )
  {
    free (server);
    return NULL;
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
  int32_t ret = 0;


  if (server_socket <= 0 || !server )
    {
      DEBUG("[SERVER] socket/server not defined\n");
      return 0;
    }


  ret = server->transport->server_recv ( server_socket, server);

  if (ret < 0)
    {
      DEBUG ("[SERVER] server receive failed\n");
      return 0;
    }
}


int32_t
cmsg_server_message_processor (cmsg_server*         server,
                               uint8_t*             buffer_data)
{
  cmsg_server_request* server_request = server->server_request;
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

  server->service->invoke (server->service,
                           server_request->method_index,
                           message,
                           server->transport->closure,
                           (void*)server);

  //todo: we need to handle errors from closure data


  protobuf_c_message_free_unpacked (message, allocator);

  DEBUG ("[SERVER] end of message processor\n");
  return 0;
}


void
cmsg_server_closure_rpc (const ProtobufCMessage* message,
                         void*                   closure_data)
{

  cmsg_server* server = ( cmsg_server* )closure_data;
  cmsg_server_request* server_request = server->server_request;
  int ret = 0;

  DEBUG ("[SERVER] invoking rpc method=%d\n", server_request->method_index);
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

      ret = server->transport->send (server_request->client_socket, &header, sizeof (header), 0);
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

      ret = server->transport->send (server_request->client_socket, buffer, packed_size + sizeof (header), 0);
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
  cmsg_server* server = ( cmsg_server* )closure_data;
  cmsg_server_request* server_request = server->server_request;
  //we are not sending a response in this transport mode
  DEBUG ("[SERVER] invoking oneway method=%d\n", server_request->method_index);
  DEBUG("[SERVER] nothing to do\n");

  server_request->closure_response = 0;
}
