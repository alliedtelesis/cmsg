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

  client->invoke = transport->invoke;
  client->base_service.invoke = transport->invoke;

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
  ProtobufCMessage* ret = NULL;

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

  ret = client->transport->client_recv (client);

  return ret;

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

 return (client->transport->connect(client));

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

  ret = client->transport->send (client->socket, buffer, packed_size + sizeof (header), 0);
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

  ret = client->transport->send (client->socket, buffer, packed_size + sizeof (header), 0);
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
