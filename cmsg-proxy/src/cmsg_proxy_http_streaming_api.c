/*
 * A helper library for using the CMSG proxy http streaming functionality.
 *
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */

#include <stdbool.h>
#include <cmsg/cmsg_client.h>
#include "http_streaming_api_auto.h"
#include "cmsg_proxy_http_streaming_api.h"

static cmsg_client *stream_client = NULL;

/**
 * Create a cmsg_client to use to talk to the http streaming server.
 *
 * @returns A pointer to the cmsg_client to use to talk to the http streaming
 *          server. This should be destroyed using 'cmsg_destroy_client_and_transport'.
 */
cmsg_client *
cmsg_proxy_http_streaming_api_create_client (void)
{
    return cmsg_create_client_unix (CMSG_DESCRIPTOR_NOPACKAGE (http_streaming));
}

/**
 * Close the streaming connection with the given id.
 *
 * @client - The cmsg_client to send to the server with. This should have been
 *           created using 'cmsg_proxy_http_streaming_api_create_client'.
 * @param id - The id of the streaming connection to close.
 */
void
cmsg_proxy_http_streaming_api_close_connection (cmsg_client *client, uint32_t id)
{
    stream_id send_msg = STREAM_ID_INIT;
    server_response *recv_msg = NULL;

    CMSG_SET_FIELD_VALUE (&send_msg, id, id);

    http_streaming_api_close_stream_connection (client, &send_msg, &recv_msg);
    CMSG_FREE_RECV_MSG (recv_msg);
}

/**
 * Pack the response message into an array of bytes such that it can
 * be sent to the streaming response server.
 *
 * @param send_msg - The 'ProtobufCMessage' message to pack.
 * @param packed_message_size - Pointer hold the size of the packed message.
 *
 * @returns The array of bytes of the packed message on success, NULL otherwise
 *          on failure. This pointer must be free'd by the caller.
 */
static uint8_t *
cmsg_proxy_http_streaming_api_pack_response (ProtobufCMessage *send_msg,
                                             uint32_t *packed_message_size)
{
    uint8_t *packed_message = NULL;
    uint32_t message_size = 0;
    uint32_t ret;

    message_size = protobuf_c_message_get_packed_size (send_msg);
    packed_message = (uint8_t *) calloc (1, message_size);

    ret = protobuf_c_message_pack (send_msg, packed_message);
    if (ret < message_size)
    {
        syslog (LOG_ERR, "Underpacked message data. Packed %d of %d bytes.", ret,
                message_size);
        free (packed_message);
        return NULL;
    }
    else if (ret > message_size)
    {
        syslog (LOG_ERR, "Overpacked message data. Packed %d of %d bytes.", ret,
                message_size);
        free (packed_message);
        return NULL;
    }

    *packed_message_size = message_size;
    return packed_message;
}

/**
 * Send the streamed response message.
 *
 * @client - The cmsg_client to send to the server with. This should have been
 *           created using 'cmsg_proxy_http_streaming_api_create_client'.
 * @param stream_id - The streaming id to send to.
 * @param send_msg - The 'ProtobufCMessage' message to send.
 *
 * @returns true if sending to the streaming server was successful and it knew about
 *          the streamed connection, false otherwise.
 */
bool
cmsg_proxy_http_streaming_api_send_response (cmsg_client *client, uint32_t stream_id,
                                             ProtobufCMessage *send_msg)
{
    bool ret = false;
    int cmsg_ret;
    server_response *response_msg = NULL;
    stream_data stream_msg = STREAM_DATA_INIT;
    uint8_t *buffer = NULL;
    uint32_t message_size = 0;

    CMSG_SET_FIELD_VALUE (&stream_msg, id, stream_id);

    buffer = cmsg_proxy_http_streaming_api_pack_response (send_msg, &message_size);
    if (!buffer)
    {
        /* Should never happen */
        return false;
    }

    CMSG_SET_FIELD_BYTES (&stream_msg, message_data, buffer, message_size);

    cmsg_ret = http_streaming_api_send_stream_data (client, &stream_msg, &response_msg);
    if (cmsg_ret == CMSG_RET_OK)
    {
        ret = response_msg->stream_found;
    }

    CMSG_FREE_RECV_MSG (response_msg);
    free (buffer);

    return ret;
}

/**
 * Send the streamed file data. The data does not need to be packed.
 *
 * @param client - The cmsg_client to send to the server with. This should have been
 *                 created using 'cmsg_proxy_http_streaming_api_create_client'.
 * @param stream_id - The streaming id to send to.
 * @param data - The raw file data to send.
 * @param length - The length of the data buffer.
 *
 * @returns true if sending to the streaming server was successful and it knew about
 *          the streamed connection, false otherwise.
 */
bool
cmsg_proxy_http_streaming_api_send_file_response (cmsg_client *client, uint32_t stream_id,
                                                  uint8_t *data, ssize_t length)
{
    bool ret = false;
    int cmsg_ret;
    server_response *response_msg = NULL;
    stream_data stream_msg = STREAM_DATA_INIT;

    CMSG_SET_FIELD_VALUE (&stream_msg, id, stream_id);
    CMSG_SET_FIELD_BYTES (&stream_msg, message_data, data, length);

    cmsg_ret = http_streaming_api_send_stream_file_data (client, &stream_msg,
                                                         &response_msg);
    if (cmsg_ret == CMSG_RET_OK)
    {
        ret = response_msg->stream_found;
    }

    CMSG_FREE_RECV_MSG (response_msg);

    return ret;
}

/**
 * Sets the correct HTTP headers for streaming JSON data.
 *
 * @param client - The cmsg_client to send to the server with. This should have been
 *                 created using 'cmsg_proxy_http_streaming_api_create_client'.
 * @param stream_id - The streaming id to send to.
 *
 * @returns true if sending to the streaming server was successful and it knew about
 *          the streamed connection, false otherwise.
 */
bool
cmsg_proxy_http_streaming_api_set_json_data_headers (cmsg_client *client,
                                                     uint32_t stream_id)
{
    bool ret = false;
    int cmsg_ret;
    server_response *response_msg = NULL;
    stream_headers_info stream_msg = STREAM_HEADERS_INFO_INIT;

    CMSG_SET_FIELD_VALUE (&stream_msg, id, stream_id);
    CMSG_SET_FIELD_VALUE (&stream_msg, type, CONTENT_TYPE_JSON);

    cmsg_ret = http_streaming_api_set_stream_headers (client, &stream_msg, &response_msg);
    if (cmsg_ret == CMSG_RET_OK)
    {
        ret = response_msg->stream_found;
    }

    CMSG_FREE_RECV_MSG (response_msg);

    return ret;
}

/**
 * Sets the correct HTTP headers for streaming file data.
 *
 * @param client - The cmsg_client to send to the server with. This should have been
 *                 created using 'cmsg_proxy_http_streaming_api_create_client'.
 * @param stream_id - The streaming id to send to.
 * @param file_name - The name of the file being streamed.
 * @param file_size - The size of the file in bytes.
 *
 * @returns true if sending to the streaming server was successful and it knew about
 *          the streamed connection, false otherwise.
 */
bool
cmsg_proxy_http_streaming_api_set_file_data_headers (cmsg_client *client,
                                                     uint32_t stream_id,
                                                     const char *file_name,
                                                     uint32_t file_size)
{
    bool ret = false;
    int cmsg_ret;
    server_response *response_msg = NULL;
    stream_headers_info stream_msg = STREAM_HEADERS_INFO_INIT;
    file_info file_info_msg = FILE_INFO_INIT;

    CMSG_SET_FIELD_VALUE (&stream_msg, id, stream_id);
    CMSG_SET_FIELD_VALUE (&stream_msg, type, CONTENT_TYPE_FILE);
    CMSG_SET_FIELD_PTR (&stream_msg, file_info, &file_info_msg);
    CMSG_SET_FIELD_PTR (&file_info_msg, file_name, file_name);
    CMSG_SET_FIELD_VALUE (&file_info_msg, file_size, file_size);

    cmsg_ret = http_streaming_api_set_stream_headers (client, &stream_msg, &response_msg);
    if (cmsg_ret == CMSG_RET_OK)
    {
        ret = response_msg->stream_found;
    }

    CMSG_FREE_RECV_MSG (response_msg);

    return ret;
}
