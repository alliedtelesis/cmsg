#include "protobuf-c-cmsg-client.h"


cmsg_client *
cmsg_client_new (cmsg_transport                   *transport,
                 const ProtobufCServiceDescriptor *descriptor)
{
    if (!transport)
    {
        DEBUG (CMSG_ERROR, "[CLIENT] error transport not defined\n");
        return 0;
    }

    cmsg_client *client = malloc (sizeof (cmsg_client));
    client->base_service.destroy = 0;
    client->allocator = &protobuf_c_default_allocator;
    client->transport = transport;
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
cmsg_client_destroy (cmsg_client *client)
{
    if (!client)
    {
        DEBUG (CMSG_ERROR, "[CLIENT] client not defined\n");
        return 0;
    }

    free (client);
    client = 0;

    return 1;
}


ProtobufCMessage *
cmsg_client_response_receive (cmsg_client *client)
{
    if (!client)
    {
        DEBUG (CMSG_ERROR, "[CLIENT] client not defined\n");
        return NULL;
    }

    return (client->transport->client_recv (client));
}


int32_t
cmsg_client_connect (cmsg_client *client)
{
    if (!client)
    {
        DEBUG (CMSG_ERROR, "[CLIENT] client not defined\n");
        return 0;
    }

    DEBUG (CMSG_INFO, "[CLIENT] connecting\n");

    if (client->state == CMSG_CLIENT_STATE_CONNECTED)
    {
        DEBUG (CMSG_INFO, "[CLIENT] already connected\n");
        return 0;
    }

    return (client->transport->connect (client));
}


void
cmsg_client_invoke_rpc (ProtobufCService       *service,
                        unsigned                method_index,
                        const ProtobufCMessage *input,
                        ProtobufCClosure        closure,
                        void                   *closure_data)
{
    int ret = 0;
    cmsg_client *client = (cmsg_client *)service;

    /* pack the data */
    /* send */
    /* depending upon transport wait for response */
    /* unpack response */
    /* return response */

    DEBUG (CMSG_INFO, "[CLIENT] cmsg_client_invoke_rpc\n");

    cmsg_client_connect (client);

    if (client->state != CMSG_CLIENT_STATE_CONNECTED)
    {
        DEBUG (CMSG_ERROR, "[CLIENT] error: client is not connected\n");
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
    memcpy ((void *)buffer, &header, sizeof (header));

    DEBUG (CMSG_INFO, "[CLIENT] header\n");
    cmsg_buffer_print (&header, sizeof (header));

    ret = protobuf_c_message_pack (input, buffer_data);
    if (ret < packed_size)
    {
        DEBUG (CMSG_ERROR,
               "[CLIENT] packing message data failed packet:%d of %d\n",
               ret, packed_size);

        free (buffer);
        buffer = 0;
        free (buffer_data);
        buffer_data = 0;
        return;
    }

    memcpy ((void *)buffer + sizeof (header), (void *)buffer_data, packed_size);

    DEBUG (CMSG_INFO, "[CLIENT] packet data\n");
    cmsg_buffer_print (buffer_data, packed_size);

    ret = client->transport->client_send (client, buffer, packed_size + sizeof (header), 0);
    if (ret < packed_size + sizeof (header))
        DEBUG (CMSG_ERROR,
               "[CLIENT] sending response failed send:%d of %ld\n",
               ret, packed_size + sizeof (header));

    //lets go hackety hack
    //todo: recv response
    //todo: process response
    ProtobufCMessage *message = cmsg_client_response_receive (client);

    client->state = CMSG_CLIENT_STATE_DESTROYED;
    client->transport->client_close (client);

    free (buffer);
    buffer = 0;
    free (buffer_data);
    buffer_data = 0;

    if (!message)
    {
        DEBUG (CMSG_ERROR, "[CLIENT] response message not valid or empty\n");
        return;
    }

    //call closure
    if (closure) //check if closure is not zero, can be the case when we use empty messages
        closure (message, closure_data);

    protobuf_c_message_free_unpacked (message, client->allocator);
    return;
}


void
cmsg_client_invoke_oneway (ProtobufCService       *service,
                           unsigned                method_index,
                           const ProtobufCMessage *input,
                           ProtobufCClosure        closure,
                           void                   *closure_data)
{
    int ret = 0;
    cmsg_client *client = (cmsg_client *)service;

    /* pack the data */
    /* send */
    /* depending upon transport wait for response */
    /* unpack response */
    /* return response */

    cmsg_client_connect (client);

    DEBUG (CMSG_INFO, "[CLIENT] cmsg_client_invoke_oneway\n");

    if (client->state != CMSG_CLIENT_STATE_CONNECTED)
    {
        DEBUG (CMSG_ERROR, "[CLIENT] error: client is not connected\n");
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
    memcpy ((void *)buffer, &header, sizeof (header));

    DEBUG (CMSG_INFO, "[CLIENT] header\n");
    cmsg_buffer_print (&header, sizeof (header));

    ret = protobuf_c_message_pack (input, buffer_data);
    if (ret < packed_size)
    {
        DEBUG (CMSG_ERROR,
               "[CLIENT] packing message data failed packet:%d of %d\n",
               ret, packed_size);

        free (buffer);
        buffer = 0;
        free (buffer_data);
        buffer_data = 0;
        return;
    }

    memcpy ((void *)buffer + sizeof (header), (void *)buffer_data, packed_size);

    DEBUG (CMSG_INFO, "[CLIENT] packet data\n");
    cmsg_buffer_print (buffer_data, packed_size);

    ret = client->transport->client_send (client, buffer, packed_size + sizeof (header), 0);
    if (ret < packed_size + sizeof (header))
        DEBUG (CMSG_ERROR,
               "[CLIENT] sending response failed send:%d of %ld\n",
               ret, packed_size + sizeof (header));

    client->state = CMSG_CLIENT_STATE_DESTROYED;
    client->transport->client_close (client);

    free (buffer);
    buffer = 0;
    free (buffer_data);
    buffer_data = 0;

    return;
}
