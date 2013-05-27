#include "protobuf-c-cmsg-client.h"


cmsg_client*
cmsg_client_new (cmsg_transport*                   transport,
                 const ProtobufCServiceDescriptor* descriptor)
{
  cmsg_client* client = malloc (sizeof(cmsg_client));
  client->base_service.destroy = 0;
  client->allocator = &protobuf_c_default_allocator;
  client->transport = transport;
  client->socket = 0;
  client->request_id = 0;

  //for compatibility with current generated code
  //this is a hack to get around a check when a client method is called
  client->descriptor = descriptor;
  client->base_service.descriptor = descriptor;

  if (transport->type == CMSG_TRANSPORT_RPC_TCP ||
      transport->type == CMSG_TRANSPORT_RPC_TIPC)
    {
      client->invoke = &cmsg_client_invoke_rpc;
      client->base_service.invoke = &cmsg_client_invoke_rpc;
    }
  else if (transport->type == CMSG_TRANSPORT_ONEWAY_TCP ||
           transport->type == CMSG_TRANSPORT_ONEWAY_TIPC)
    {
      client->invoke = &cmsg_client_invoke_oneway;
      client->base_service.invoke = &cmsg_client_invoke_oneway;
    }

  if (client->transport->type == CMSG_TRANSPORT_RPC_TCP ||
      client->transport->type == CMSG_TRANSPORT_ONEWAY_TCP)
    {
    }
  else if (client->transport->type == CMSG_TRANSPORT_RPC_TIPC ||
           client->transport->type == CMSG_TRANSPORT_ONEWAY_TIPC)
    {
    }
  else
    {
      DEBUG ("[CLIENT] error transport type not supported\n");
      free (client);
      client = 0;
      return 0;
    }
  return client;
}


int32_t
cmsg_client_destroy(cmsg_client *client)
{
  if (!client)
    {
      DEBUG ("[CLIENT] client not defined\n");
      return 0;
    }

  client->state = CMSG_CLIENT_STATE_DESTROYED;
  DEBUG ("[CLIENT] shutting down socket\n");
  shutdown (client->socket, 2);
  
  DEBUG ("[CLIENT] closing socket\n");
  close (client->socket);

  free (client);
  client = 0;

  return 1;
}


ProtobufCMessage*
cmsg_client_response_receive (cmsg_client *client)
{
  int32_t nbytes;
  int32_t dyn_len;
  int32_t ret = 0;
  cmsg_header_response header_received;
  cmsg_header_response header_converted;
  uint8_t* buffer = 0;
  uint8_t buf_static[512];

  if (!client)
    {
      DEBUG ("[CLIENT] client not defined\n");
      return 0;
    }

  if (!client->socket)
    {
      DEBUG ("[CLIENT] socket not defined\n");
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

      DEBUG ("[CLIENT] received response header\n");
      cmsg_debug_buffer_print ((void*)&header_received, sizeof (cmsg_header_response));

      DEBUG ("[CLIENT] status_code    host: %d, wire: %d\n", header_converted.status_code, header_received.status_code);
      DEBUG ("[CLIENT] method_index   host: %d, wire: %d\n", header_converted.method_index, header_received.method_index);
      DEBUG ("[CLIENT] message_length host: %d, wire: %d\n", header_converted.message_length, header_received.message_length);
      DEBUG ("[CLIENT] request_id     host: %d, wire: %d\n", header_converted.request_id, header_received.request_id);

      if (header_converted.status_code != CMSG_STATUS_CODE_SUCCESS)
        {
          DEBUG ("[CLIENT] server could not process message correctly\n");
          DEBUG ("[CLIENT] todo: handle this case better\n");
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
          DEBUG ("[CLIENT] received response data\n");
          cmsg_debug_buffer_print (buffer, dyn_len);

          //todo: call cmsg_client_response_message_processor
          ProtobufCMessage *message = 0;
          ProtobufCAllocator *allocator = (ProtobufCAllocator*)client->allocator;

          DEBUG ("[CLIENT] unpacking response message\n");

          message = protobuf_c_message_unpack (client->descriptor->methods[header_converted.method_index].output,
                                               allocator,
                                               header_converted.message_length,
                                               buffer);

          if (message == 0)
            {
              DEBUG ("[SERVER] error unpacking response message\n");
              return 0;
            }
          return message;
        }
      else
        {
          DEBUG ("[CLIENT] recv socket %d no data\n", client->socket);
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
      DEBUG ("[CLIENT] recv socket %d bad header nbytes %d\n", client->socket, nbytes );

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
          DEBUG ("[CLIENT] recv socket %d error: %s\n", client->socket, strerror(errno) );
        }
      ret = 0;
    }
  return 0;
}


int32_t
cmsg_client_connect (cmsg_client *client)
{
  if  (!client)
    {
      DEBUG ("[CLIENT] client not defined");
      return 0;
    }

  DEBUG ("[CLIENT] connecting\n");

  if (client->state == CMSG_CLIENT_STATE_CONNECTED)
    {
      DEBUG ("[CLIENT] already connected\n");
      return 0;
    }

  if (client->transport->type == CMSG_TRANSPORT_RPC_TCP ||
      client->transport->type == CMSG_TRANSPORT_ONEWAY_TCP)
    {
      client->socket = socket (client->transport->family, SOCK_STREAM, 0);
    
      if (client->socket < 0)
        {
          DEBUG ("[CLIENT] error creating socket: %s\n", strerror (errno));
          return 0;
        }

      if (connect (client->socket, (struct sockaddr*)&client->transport->sockaddr.in, sizeof (client->transport->sockaddr.in)) < 0)
        {
          if (errno == EINPROGRESS)
            {
              //?
            }
          close (client->socket);
          client->socket = 0;
          DEBUG ("[CLIENT] error connecting to remote host: %s\n", strerror (errno));
          return 0;
        }
      else
        {
          client->state = CMSG_CLIENT_STATE_CONNECTED;
          DEBUG ("[CLIENT] succesfully connected\n");
          return 0;
        }
    }
  else if (client->transport->type == CMSG_TRANSPORT_RPC_TIPC ||
           client->transport->type == CMSG_TRANSPORT_ONEWAY_TIPC)
    {
      client->socket = socket (client->transport->family, SOCK_STREAM, 0);

      if (client->socket < 0)
        {
          DEBUG ("[CLIENT] error creating socket: %s\n", strerror (errno));
          return 0;
        }
      if (connect (client->socket, (struct sockaddr*)&client->transport->sockaddr.tipc, sizeof (client->transport->sockaddr.tipc)) < 0)
        {
          if (errno == EINPROGRESS)
            {
              //?
            }
          close (client->socket);
          client->socket = 0;
          DEBUG ("[CLIENT] error connecting to remote host: %s\n", strerror (errno));
          return 0;
        }
      else
        {
          client->state = CMSG_CLIENT_STATE_CONNECTED;
          DEBUG ("[CLIENT] succesfully connected\n");
          return 0;
        }
    }
  return 0;
}


void
cmsg_client_invoke_rpc (ProtobufCService*       service,
                        unsigned                method_index,
                        const ProtobufCMessage* input,
                        ProtobufCClosure        closure,
                        void*                   closure_data)
{
  int ret = 0;
  cmsg_client* client = (cmsg_client*)service;

  /* pack the data */
  /* send */
  /* depending upon transport wait for response */
  /* unpack response */
  /* return response */

  DEBUG ("[CLIENT] cmsg_client_invoke_rpc\n");

  cmsg_client_connect (client);
  
  if (client->state != CMSG_CLIENT_STATE_CONNECTED)
    {
      DEBUG ("[CLIENT] error: client is not connected\n");
      return;
    }

  const ProtobufCServiceDescriptor *desc = service->descriptor;
  const ProtobufCMethodDescriptor *method = desc->methods + method_index;

  uint32_t packed_size = protobuf_c_message_get_packed_size (input);

  client->request_id++;

  cmsg_header_request header;
  header.method_index = cmsg_common_uint32_to_le (method_index);
  header.message_length = cmsg_common_uint32_to_le (packed_size);
  header.request_id = client->request_id;
  uint8_t* buffer = malloc (packed_size + sizeof (header));
  uint8_t* buffer_data = malloc (packed_size);
  memcpy ((void*)buffer, &header, sizeof (header));

  DEBUG ("[CLIENT] header\n");
  cmsg_debug_buffer_print (&header, sizeof (header));

  ret = protobuf_c_message_pack (input, buffer_data);
  if (ret < packed_size)
    {
      DEBUG ("[CLIENT] packing message data failed packet:%d of %d\n", ret, packed_size);
      free (buffer);
      buffer = 0;
      free (buffer_data);
      buffer_data = 0;
      return;
    }

  memcpy ((void*)buffer + sizeof (header), (void*)buffer_data, packed_size);

  printf ("[CLIENT] packet data\n");
  cmsg_debug_buffer_print(buffer_data, packed_size);

  ret = send (client->socket, buffer, packed_size + sizeof (header), 0);
  if (ret < packed_size + sizeof (header))
    DEBUG ("[CLIENT] sending response failed send:%d of %ld\n", ret, packed_size + sizeof (header));

  //lets go hackety hack
  //todo: recv response
  //todo: process response
  ProtobufCMessage *message = cmsg_client_response_receive(client);

  client->state = CMSG_CLIENT_STATE_DESTROYED;
  DEBUG ("[CLIENT] shutting down socket\n");
  shutdown (client->socket, 2);

  DEBUG ("[CLIENT] closing socket\n");
  close (client->socket);

  free (buffer);
  buffer = 0;
  free (buffer_data);
  buffer_data = 0;

  if (!message)
    {
      DEBUG ("[CLIENT] response message not valid\n");
      return;
    }

  //todo: call closure
  closure (message, closure_data);

  protobuf_c_message_free_unpacked (message, client->allocator);
  return;
}


void
cmsg_client_invoke_oneway (ProtobufCService*       service,
                           unsigned                method_index,
                           const ProtobufCMessage* input,
                           ProtobufCClosure        closure,
                           void*                   closure_data)
{
  int ret = 0;
  cmsg_client *client = (cmsg_client*)service;

  /* pack the data */
  /* send */
  /* depending upon transport wait for response */
  /* unpack response */
  /* return response */

  cmsg_client_connect(client);

  DEBUG ("[CLIENT] cmsg_client_invoke_oneway\n");

  if (client->state != CMSG_CLIENT_STATE_CONNECTED)
    {
      DEBUG ("[CLIENT] error: client is not connected\n");
      return;
    }

  const ProtobufCServiceDescriptor *desc = service->descriptor;
  const ProtobufCMethodDescriptor *method = desc->methods + method_index;

  uint32_t packed_size = protobuf_c_message_get_packed_size (input);


  client->request_id++;

  cmsg_header_request header;
  header.method_index = cmsg_common_uint32_to_le (method_index);
  header.message_length = cmsg_common_uint32_to_le (packed_size);
  header.request_id = client->request_id;
  uint8_t *buffer = malloc (packed_size + sizeof (header));
  uint8_t *buffer_data = malloc (packed_size);
  memcpy ((void*)buffer, &header, sizeof (header));

  DEBUG ("[CLIENT] header\n");
  cmsg_debug_buffer_print (&header, sizeof (header));

  ret = protobuf_c_message_pack (input, buffer_data);
  if (ret < packed_size)
    {
      DEBUG ("[CLIENT] packing message data failed packet:%d of %d\n", ret, packed_size);
      free (buffer);
      buffer = 0;
      free (buffer_data);
      buffer_data = 0;
      return;
    }

  memcpy ((void*)buffer + sizeof (header), (void*)buffer_data, packed_size);

  DEBUG ("[CLIENT] packet data\n");
  cmsg_debug_buffer_print (buffer_data, packed_size);

  ret = send (client->socket, buffer, packed_size + sizeof (header), 0);
  if (ret < packed_size + sizeof (header))
    DEBUG ("[CLIENT] sending response failed send:%d of %ld\n", ret, packed_size + sizeof (header));

  client->state = CMSG_CLIENT_STATE_DESTROYED;
  DEBUG ("[CLIENT] shutting down socket\n");
  shutdown (client->socket, 2);

  DEBUG ("[CLIENT] closing socket\n");
  close (client->socket);

  free (buffer);
  buffer = 0;
  free (buffer_data);
  buffer_data = 0;

  return;
}
