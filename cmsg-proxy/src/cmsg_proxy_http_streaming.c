/*
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */

#include <stdbool.h>
#include <protobuf2json.h>
#include <cmsg/cmsg_pthread_helpers.h>
#include "cmsg_proxy.h"
#include "cmsg_proxy_mem.h"
#include "cmsg_proxy_private.h"
#include "http_streaming_impl_auto.h"

typedef struct _cmsg_proxy_stream_connection
{
    uint32_t id;
    void *connection;
    const ProtobufCMessageDescriptor *output_msg_descriptor;
    bool in_use;
    bool to_delete;
    bool headers_set;
    pthread_mutex_t lock;
} cmsg_proxy_stream_connection;

typedef struct _server_poll_info
{
    cmsg_server *server;
    fd_set readfds;
    int fd_max;
} server_poll_info;

static const char *cmsg_content_type_key = "Content-Type";
static const char *cmsg_content_disposition_key = "Content-Disposition";
static const char *cmsg_content_encoding_key = "Content-Transfer-Encoding";
static const char *cmsg_content_length_key = "Content-Length";
static const char *cmsg_mime_octet_stream = "application/octet-stream";
static const char *cmsg_mime_application_json = "application/json";
static const char *cmsg_mime_text_plain = "text/plain";
static const char *cmsg_binary_encoding = "binary";
static const char *cmsg_filename_header_format = "attachment; filename=\"%s\"";

static cmsg_proxy_stream_response_send_func _stream_response_send = NULL;
static cmsg_proxy_stream_response_close_func _stream_response_close = NULL;
static cmsg_proxy_stream_conn_release_func _stream_conn_release = NULL;
static cmsg_proxy_stream_headers_set_func _stream_headers_set = NULL;
static cmsg_proxy_stream_conn_abort_func _stream_conn_abort = NULL;
static cmsg_proxy_stream_conn_busy_func _stream_conn_busy = NULL;

static GList *stream_connections_list = NULL;
static pthread_mutex_t stream_connections_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t stream_id_mutex = PTHREAD_MUTEX_INITIALIZER;
static cmsg_server *streaming_server = NULL;
static pthread_t streaming_server_thread;

/**
 * Set the function used to send stream responses on an HTTP connection.
 * This should be called once by the web server when initialising cmsg proxy.
 *
 * @param func - The function used to send stream responses.
 */
void
cmsg_proxy_streaming_set_response_send_function (cmsg_proxy_stream_response_send_func func)
{
    _stream_response_send = func;
}

/**
 * Set the function used to finish the streaming of responses on an HTTP connection.
 * This should be called once by the web server when initialising cmsg proxy.
 *
 * @param func - The function used to finish the streaming of responses.
 */
void
cmsg_proxy_streaming_set_response_close_function (cmsg_proxy_stream_response_close_func
                                                  func)
{
    _stream_response_close = func;
}

/**
 * Set the function used to release the streaming connection.
 * This should be called once by the web server when initialising cmsg proxy.
 *
 * @param func - The function used to release the streaming connection.
 */
void
cmsg_proxy_streaming_set_conn_release_function (cmsg_proxy_stream_conn_release_func func)
{
    _stream_conn_release = func;
}

/**
 * Set the function used to set the correct headers for a streaming connection.
 * This should be called once by the web server when initialising cmsg proxy.
 *
 * @param func - The function used to release the streaming connection.
 */
void
cmsg_proxy_streaming_set_headers_set_function (cmsg_proxy_stream_headers_set_func func)
{
    _stream_headers_set = func;
}

/**
 * Set the function used to abort a streaming connection due to an error.
 * This should be called once by the web server when initialising cmsg proxy.
 *
 * @param func - The function used to abort the streaming connection.
 */
void
cmsg_proxy_streaming_set_conn_abort_function (cmsg_proxy_stream_conn_abort_func func)
{
    _stream_conn_abort = func;
}

/**
 * Set the function used to get whether a streaming connection is busy.
 * This should be called once by the web server when initialising cmsg proxy.
 *
 * @param func - The function used to get whether a streaming connection is busy.
 */
void
cmsg_proxy_streaming_set_conn_busy_function (cmsg_proxy_stream_conn_busy_func func)
{
    _stream_conn_busy = func;
}

/**
 * Wrapper function to call '_stream_response_send' if the function pointer is set.
 */
static void
stream_response_send (cmsg_proxy_stream_response_data *data)
{
    if (_stream_response_send)
    {
        _stream_response_send (data);
    }
}

/**
 * Wrapper function to call '_stream_response_close' if the function pointer is set.
 */
static void
stream_response_close (void *connection)
{
    if (_stream_response_close)
    {
        _stream_response_close (connection);
    }
}

/**
 * Wrapper function to call '_stream_conn_release' if the function pointer is set.
 */
static void
stream_conn_release (void *connection)
{
    if (_stream_conn_release)
    {
        _stream_conn_release (connection);
    }
}

/**
 * Wrapper function to call '_stream_headers_set' if the function pointer is set.
 */
static void
stream_headers_set (cmsg_proxy_stream_header_data *data)
{
    if (_stream_headers_set)
    {
        _stream_headers_set (data);
    }
}

/**
 * Wrapper function to call '_stream_conn_abort' if the function pointer is set.
 */
static void
stream_conn_abort (void *connection)
{
    if (_stream_conn_abort)
    {
        _stream_conn_abort (connection);
    }
}

/**
 * Wrapper function to call '_stream_conn_busy' if the function pointer is set.
 */
static bool
stream_conn_busy (void *connection)
{
    if (_stream_conn_busy)
    {
        return _stream_conn_busy (connection);
    }

    return false;
}

/**
 * Free data allocated to a cmsg_proxy_stream_response_data struct.
 *
 * @param data - The struct to free.
 */
void
cmsg_proxy_streaming_free_stream_response_data (cmsg_proxy_stream_response_data *data)
{
    if (data)
    {
        CMSG_PROXY_FREE (data->data);
        CMSG_PROXY_FREE (data);
    }
}

/**
 * Free data allocated to a cmsg_proxy_stream_header_data struct.
 *
 * @param data - The struct to free.
 */
void
cmsg_proxy_streaming_free_stream_header_data (cmsg_proxy_stream_header_data *data)
{
    int i = 0;

    if (data)
    {
        /* Free all the header values */
        for (i = 0; i < data->headers->num_headers; i++)
        {
            CMSG_PROXY_FREE (data->headers->headers[i].value);
        }
        CMSG_PROXY_FREE (data->headers->headers);
        CMSG_PROXY_FREE (data->headers);
        CMSG_PROXY_FREE (data);
    }
}

/**
 * Delete a connection info structure if it is not in use. Otherwise mark it
 * to be deleted once the structure is released.
 *
 * @param connection_info - The connection info structure to delete
 */
static void
cmsg_proxy_streaming_delete_conn_info (cmsg_proxy_stream_connection *connection_info)
{
    pthread_mutex_lock (&connection_info->lock);
    if (connection_info->in_use)
    {
        connection_info->to_delete = true;
        pthread_mutex_unlock (&connection_info->lock);
    }
    else
    {
        pthread_mutex_unlock (&connection_info->lock);
        pthread_mutex_destroy (&connection_info->lock);
        stream_conn_release (connection_info->connection);
        CMSG_PROXY_FREE (connection_info);
    }
}

/**
 * Release a connection info structure. If it is marked to be deleted then delete it.
 *
 * @param connection_info - The connection info structure to release
 */
static void
cmsg_proxy_streaming_release_conn_info (cmsg_proxy_stream_connection *connection_info)
{
    pthread_mutex_lock (&connection_info->lock);
    if (connection_info->to_delete)
    {
        pthread_mutex_unlock (&connection_info->lock);
        pthread_mutex_destroy (&connection_info->lock);
        stream_conn_release (connection_info->connection);
        CMSG_PROXY_FREE (connection_info);
    }
    else
    {
        connection_info->in_use = false;
        pthread_mutex_unlock (&connection_info->lock);
    }
}

/**
 * Check whether the input message specifies the API should stream the response.
 *
 * @param msg_descriptor - The descriptor for the input message
 *
 * @returns true if the response should be streamed, false otherwise.
 */
static bool
cmsg_proxy_streaming_is_required (const ProtobufCMessageDescriptor *msg_descriptor)
{
    if (protobuf_c_message_descriptor_get_field_by_name (msg_descriptor, "_streaming_id"))
    {
        return true;
    }

    return false;
}

/**
 * Generate the ID to use for the stream connection.
 *
 * @returns a 32bit unsigned integer to use as the ID.
 */
static uint32_t
cmsg_proxy_streaming_generate_id (void)
{
    static uint32_t last_id_assigned = 0;
    uint32_t new_id = 0;

    pthread_mutex_lock (&stream_id_mutex);

    new_id = last_id_assigned++;

    pthread_mutex_unlock (&stream_id_mutex);

    return new_id;
}

/**
 * If required, generate an ID to differentiate stream connections and
 * store this along with the connection. Also set this ID value into the
 * input message so the IMPL can specify the stream connection to send on.
 *
 * @param connection - The HTTP connection
 * @param input_json_obj - The input json message to potentially put the ID into
 * @param input_msg_descriptor - The descriptor for the input message
 * @param output_msg_descriptor - The descriptor for the output message
 * @param streaming_id - Pointer to store the chosen stream ID in
 *
 * @returns true if streaming is required, or false if streaming is not required.
 */
bool
cmsg_proxy_streaming_create_conn (void *connection, json_t **input_json_obj,
                                  const ProtobufCMessageDescriptor *input_msg_descriptor,
                                  const ProtobufCMessageDescriptor *output_msg_descriptor,
                                  uint32_t *streaming_id)
{
    uint32_t id = 0;
    char buffer[50];
    cmsg_proxy_stream_connection *connection_info = NULL;

    if (!cmsg_proxy_streaming_is_required (input_msg_descriptor))
    {
        return false;
    }

    connection_info = CMSG_PROXY_CALLOC (1, sizeof (cmsg_proxy_stream_connection));
    if (!connection_info)
    {
        syslog (LOG_ERR, "Failed to allocate memory for cmsg proxy stream connection");
        return false;
    }

    id = cmsg_proxy_streaming_generate_id ();

    sprintf (buffer, "%u", id);
    cmsg_proxy_set_internal_api_value (buffer, input_json_obj, input_msg_descriptor,
                                       "_streaming_id");

    connection_info->id = id;
    connection_info->output_msg_descriptor = output_msg_descriptor;
    connection_info->connection = connection;
    connection_info->in_use = false;
    connection_info->to_delete = false;
    connection_info->headers_set = false;
    pthread_mutex_init (&connection_info->lock, NULL);

    pthread_mutex_lock (&stream_connections_mutex);
    stream_connections_list = g_list_prepend (stream_connections_list, connection_info);
    pthread_mutex_unlock (&stream_connections_mutex);

    *streaming_id = id;
    return true;
}

/**
 * Delete a streaming connection with the given id. Note that this
 * function does not take the connection lock as it assumes the connection
 * being removed is never used outside of a single thread.
 *
 * @param id - The streaming connection id
 */
void
cmsg_proxy_streaming_delete_conn_by_id (uint32_t id)
{
    GList *iter;
    cmsg_proxy_stream_connection *connection_info = NULL;

    pthread_mutex_lock (&stream_connections_mutex);
    for (iter = stream_connections_list; iter; iter = g_list_next (iter))
    {
        connection_info = (cmsg_proxy_stream_connection *) iter->data;
        if (connection_info->id == id)
        {
            pthread_mutex_destroy (&connection_info->lock);
            CMSG_PROXY_FREE (connection_info);
            stream_connections_list = g_list_delete_link (stream_connections_list, iter);
            break;
        }
    }
    pthread_mutex_unlock (&stream_connections_mutex);
}

/**
 * Find a streaming connection with the given id.
 *
 * @param id - The streaming connection id
 *
 * @returns The streaming connection structure or
 *          NULL if it does not exist.
 */
static cmsg_proxy_stream_connection *
cmsg_proxy_streaming_lookup_conn_by_id (uint32_t id)
{
    GList *iter;
    cmsg_proxy_stream_connection *connection_info = NULL;

    pthread_mutex_lock (&stream_connections_mutex);
    for (iter = stream_connections_list; iter; iter = g_list_next (iter))
    {
        connection_info = (cmsg_proxy_stream_connection *) iter->data;
        if (connection_info->id == id)
        {
            pthread_mutex_lock (&connection_info->lock);
            connection_info->in_use = true;
            pthread_mutex_unlock (&connection_info->lock);
            pthread_mutex_unlock (&stream_connections_mutex);
            return connection_info;
        }
    }

    pthread_mutex_unlock (&stream_connections_mutex);
    return NULL;
}

/**
 * Initialise the cmsg proxy http streaming functionality.
 */
void
cmsg_proxy_streaming_init (void)
{
    streaming_server =
        cmsg_create_server_unix_rpc (CMSG_SERVICE_NOPACKAGE (http_streaming));
    if (!streaming_server ||
        !cmsg_pthread_server_init (&streaming_server_thread, streaming_server))
    {
        syslog (LOG_ERR, "Failed to start cmsg proxy streaming server thread");
        return;
    }

    pthread_setname_np (streaming_server_thread, "cmsg_proxy_http");
}

/**
 * Deinitialise the cmsg proxy http streaming functionality.
 */
void
cmsg_proxy_streaming_deinit (void)
{
    pthread_cancel (streaming_server_thread);
    pthread_join (streaming_server_thread, NULL);
    cmsg_destroy_server_and_transport (streaming_server);
}

/**
 * Function to be called when a given connection has timed out and
 * subsequently ended.
 *
 * @param connection - The HTTP connection that has timed out
 */
void
cmsg_proxy_streaming_conn_timeout (void *connection)
{
    GList *iter;
    cmsg_proxy_stream_connection *connection_info = NULL;

    pthread_mutex_lock (&stream_connections_mutex);
    for (iter = stream_connections_list; iter; iter = g_list_next (iter))
    {
        connection_info = (cmsg_proxy_stream_connection *) iter->data;
        if (connection_info->connection == connection)
        {
            cmsg_proxy_streaming_delete_conn_info (connection_info);
            stream_connections_list = g_list_delete_link (stream_connections_list, iter);
            break;
        }
    }
    pthread_mutex_unlock (&stream_connections_mutex);
}

/**
 * Unset the ant_result field from the input protobuf message if it exists.
 * For streamed_ant_result, free everything except the 'response' ant_result.
 *
 * @param msg - Pointer to the ProtobufCMessage to remove the field from.
 */
static void
cmsg_proxy_streaming_strip_ant_result (ProtobufCMessage **msg)
{
    const ProtobufCFieldDescriptor *field_desc = NULL;
    ProtobufCMessage **error_message_ptr = NULL;
    ProtobufCMessage **response_ptr = NULL;
    ant_result *error_message = NULL;
    ant_result *response = NULL;

    if (*msg == NULL)
    {
        return;
    }

    field_desc = protobuf_c_message_descriptor_get_field_by_name ((*msg)->descriptor,
                                                                  "_error_info");
    if (field_desc)
    {
        error_message_ptr = (ProtobufCMessage **) (((char *) *msg) + field_desc->offset);
    }
    else if (strcmp ((*msg)->descriptor->name, "ant_result") == 0)
    {
        error_message_ptr = msg;
    }
    else
    {
        return;
    }

    error_message = (ant_result *) (*error_message_ptr);
    if (error_message && CMSG_IS_FIELD_PRESENT (error_message, code))
    {
        /* Unset the error info message from the protobuf message */
        CMSG_FREE_RECV_MSG (error_message);
        *error_message_ptr = NULL;
    }

    /* Return the internal ant_result as the new message and free the wrapper.
     * This means that the details field will be correctly stripped if it is empty.
     */
    if (msg && strcmp ((*msg)->descriptor->name, "streamed_ant_result") == 0)
    {
        field_desc = protobuf_c_message_descriptor_get_field_by_name ((*msg)->descriptor,
                                                                      "response");
        if (field_desc)
        {
            response_ptr = (ProtobufCMessage **) (((char *) *msg) + field_desc->offset);
            response = (ant_result *) * response_ptr;
            *response_ptr = NULL;
            CMSG_FREE_RECV_MSG (*msg);
            *msg = (ProtobufCMessage *) response;
        }
    }

    return;
}

void
http_streaming_impl_send_stream_data (const void *service, const stream_data *recv_msg)
{
    server_response send_msg = SERVER_RESPONSE_INIT;
    ProtobufCMessage *message = NULL;
    cmsg_proxy_stream_connection *connection_info = NULL;
    ProtobufCAllocator *allocator = &cmsg_memory_allocator;
    cmsg_proxy_stream_response_data *data = NULL;
    cmsg_proxy_output output;
    char *new_response_body = NULL;

    connection_info = cmsg_proxy_streaming_lookup_conn_by_id (recv_msg->id);
    if (!connection_info)
    {
        CMSG_SET_FIELD_VALUE (&send_msg, stream_found, false);
        http_streaming_server_send_stream_dataSend (service, &send_msg);
        return;
    }

    CMSG_SET_FIELD_VALUE (&send_msg, stream_found, true);

    /* If the output message has a "_file" field, don't allow using this RPC to
     * stream the response. */
    if (cmsg_proxy_msg_has_file (connection_info->output_msg_descriptor))
    {
        syslog (LOG_ERR, "Cannot stream message type (%s) because it contains a '_file' "
                "field", connection_info->output_msg_descriptor->name);
        cmsg_proxy_streaming_release_conn_info (connection_info);
        http_streaming_server_send_stream_dataSend (service, &send_msg);
        return;
    }

    /* Log an error if the headers haven't been set yet. */
    if (!connection_info->headers_set)
    {
        syslog (LOG_ERR, "Headers not set for streaming response (type = %s)",
                connection_info->output_msg_descriptor->name);
        cmsg_proxy_streaming_release_conn_info (connection_info);
        http_streaming_server_send_stream_dataSend (service, &send_msg);
        return;
    }

    message = protobuf_c_message_unpack (connection_info->output_msg_descriptor, allocator,
                                         recv_msg->message_data.len,
                                         recv_msg->message_data.data);
    if (!message)
    {
        syslog (LOG_ERR, "Failed to unpack stream response (expected message type = %s)",
                connection_info->output_msg_descriptor->name);
        http_streaming_server_send_stream_dataSend (service, &send_msg);
        return;
    }

    cmsg_proxy_streaming_strip_ant_result (&message);

    output.http_status = HTTP_CODE_OK;
    if (!cmsg_proxy_generate_response_body (message, &output))
    {
        syslog (LOG_ERR, "Failed to generate stream response (message type = %s)",
                connection_info->output_msg_descriptor->name);
        protobuf_c_message_free_unpacked (message, allocator);
        http_streaming_server_send_stream_dataSend (service, &send_msg);
        return;
    }

    protobuf_c_message_free_unpacked (message, allocator);

    data = CMSG_PROXY_CALLOC (1, sizeof (cmsg_proxy_stream_response_data));
    data->connection = connection_info->connection;

    /* Add a newline to the end of the text */
    if (CMSG_PROXY_ASPRINTF (&new_response_body, "%s\n", output.response_body) >= 0)
    {
        data->data = new_response_body;
        data->length = output.response_length + 1;

        /* This needs to be freed here since it isn't passed to stream_response_send */
        free (output.response_body);
    }

    /* 'data' will be freed by this call */
    stream_response_send (data);

    cmsg_proxy_streaming_release_conn_info (connection_info);

    http_streaming_server_send_stream_dataSend (service, &send_msg);
}

void
http_streaming_impl_send_stream_file_data (const void *service, const stream_data *recv_msg)
{
    server_response send_msg = SERVER_RESPONSE_INIT;
    cmsg_proxy_stream_connection *connection_info = NULL;
    cmsg_proxy_stream_response_data *data = NULL;

    connection_info = cmsg_proxy_streaming_lookup_conn_by_id (recv_msg->id);
    if (!connection_info)
    {
        CMSG_SET_FIELD_VALUE (&send_msg, stream_found, false);
        http_streaming_server_send_stream_file_dataSend (service, &send_msg);
        return;
    }

    CMSG_SET_FIELD_VALUE (&send_msg, stream_found, true);

    /* If the output message does not have a "_file" field, don't allow using this
     * RPC to stream the response. */
    if (!cmsg_proxy_msg_has_file (connection_info->output_msg_descriptor))
    {
        syslog (LOG_ERR, "Cannot stream message type (%s) as raw file data since it does "
                "not contain a '_file' field",
                connection_info->output_msg_descriptor->name);
        cmsg_proxy_streaming_release_conn_info (connection_info);
        http_streaming_server_send_stream_file_dataSend (service, &send_msg);
        return;
    }

    /* The headers must be explicitly set before this RPC is used to stream the
     * response. */
    if (!connection_info->headers_set)
    {
        syslog (LOG_ERR, "Headers not set for streaming raw file data response");
        http_streaming_server_send_stream_file_dataSend (service, &send_msg);
        cmsg_proxy_streaming_release_conn_info (connection_info);
        return;
    }

    data = CMSG_PROXY_CALLOC (1, sizeof (cmsg_proxy_stream_response_data));
    data->connection = connection_info->connection;
    data->length = recv_msg->message_data.len;
    data->data = CMSG_PROXY_CALLOC (1, recv_msg->message_data.len);
    if (data->data)
    {
        memcpy (data->data, recv_msg->message_data.data, recv_msg->message_data.len);
    }

    while (stream_conn_busy (connection_info->connection))
    {
        usleep (1000);
    }

    /* 'data' will be freed by this call */
    stream_response_send (data);

    cmsg_proxy_streaming_release_conn_info (connection_info);

    http_streaming_server_send_stream_file_dataSend (service, &send_msg);
}

void
http_streaming_impl_close_stream_connection (const void *service, const stream_id *recv_msg)
{
    server_response send_msg = SERVER_RESPONSE_INIT;
    cmsg_proxy_stream_connection *connection_info = NULL;
    bool found = false;

    connection_info = cmsg_proxy_streaming_lookup_conn_by_id (recv_msg->id);
    if (connection_info)
    {
        stream_response_close (connection_info->connection);

        pthread_mutex_lock (&stream_connections_mutex);
        stream_connections_list = g_list_remove (stream_connections_list, connection_info);
        pthread_mutex_unlock (&stream_connections_mutex);

        connection_info->to_delete = true;
        connection_info->in_use = false;
        cmsg_proxy_streaming_release_conn_info (connection_info);
        found = true;
    }

    CMSG_SET_FIELD_VALUE (&send_msg, stream_found, found);

    http_streaming_server_close_stream_connectionSend (service, &send_msg);
}

void
http_streaming_impl_set_stream_headers (const void *service,
                                        const stream_headers_info *recv_msg)
{
    server_response send_msg = SERVER_RESPONSE_INIT;
    cmsg_proxy_stream_connection *connection_info = NULL;
    cmsg_proxy_stream_header_data *data = NULL;
    cmsg_proxy_header *header_array;
    cmsg_proxy_headers *headers;
    int n_headers;

    connection_info = cmsg_proxy_streaming_lookup_conn_by_id (recv_msg->id);
    if (!connection_info)
    {
        CMSG_SET_FIELD_VALUE (&send_msg, stream_found, false);
        http_streaming_server_set_stream_headersSend (service, &send_msg);
        return;
    }

    CMSG_SET_FIELD_VALUE (&send_msg, stream_found, true);

    data = CMSG_PROXY_CALLOC (1, sizeof (cmsg_proxy_stream_header_data));
    data->connection = connection_info->connection;
    headers = CMSG_PROXY_CALLOC (1, sizeof (cmsg_proxy_headers));

    switch (recv_msg->type)
    {
    case CONTENT_TYPE_JSON:
        n_headers = 1;
        header_array = CMSG_PROXY_CALLOC (n_headers, sizeof (cmsg_proxy_header));
        header_array[0].key = cmsg_content_type_key;
        header_array[0].value = CMSG_PROXY_STRDUP (cmsg_mime_application_json);
        break;
    case CONTENT_TYPE_FILE:
        if (!CMSG_IS_PTR_PRESENT (recv_msg, file_info))
        {
            syslog (LOG_ERR, "stream_headers_info message with content type "
                    "'CONTENT_TYPE_FILE' missing 'file_info' field");
            CMSG_PROXY_FREE (data);
            CMSG_PROXY_FREE (headers);
            cmsg_proxy_streaming_release_conn_info (connection_info);
            http_streaming_server_set_stream_headersSend (service, &send_msg);
            return;
        }

        if (!cmsg_proxy_msg_has_file (connection_info->output_msg_descriptor))
        {
            syslog (LOG_ERR, "Message type (%s) does not contain raw file data. Cannot "
                    "set headers", connection_info->output_msg_descriptor->name);
            CMSG_PROXY_FREE (data);
            CMSG_PROXY_FREE (headers);
            cmsg_proxy_streaming_release_conn_info (connection_info);
            http_streaming_server_set_stream_headersSend (service, &send_msg);
            return;
        }

        n_headers = 4;
        header_array = CMSG_PROXY_CALLOC (n_headers, sizeof (cmsg_proxy_header));
        header_array[0].key = cmsg_content_type_key;
        header_array[0].value = CMSG_PROXY_STRDUP (cmsg_mime_octet_stream);
        header_array[1].key = cmsg_content_encoding_key;
        header_array[1].value = CMSG_PROXY_STRDUP (cmsg_binary_encoding);
        header_array[2].key = cmsg_content_disposition_key;
        CMSG_PROXY_ASPRINTF (&(header_array[2].value), cmsg_filename_header_format,
                             recv_msg->file_info->file_name);
        header_array[3].key = cmsg_content_length_key;
        CMSG_PROXY_ASPRINTF (&(header_array[3].value), "%d",
                             recv_msg->file_info->file_size);
        break;
    case CONTENT_TYPE_PLAINTEXT:
        n_headers = 1;
        header_array = CMSG_PROXY_CALLOC (n_headers, sizeof (cmsg_proxy_header));
        header_array[0].key = cmsg_content_type_key;
        header_array[0].value = CMSG_PROXY_STRDUP (cmsg_mime_text_plain);
        break;
    default:
        syslog (LOG_ERR, "Unrecognized content type for streaming API response (type = %d)",
                recv_msg->type);
        CMSG_PROXY_FREE (data);
        CMSG_PROXY_FREE (headers);
        cmsg_proxy_streaming_release_conn_info (connection_info);
        http_streaming_server_set_stream_headersSend (service, &send_msg);
        return;
    }

    headers->headers = header_array;
    headers->num_headers = n_headers;
    data->headers = headers;

    /* 'data' will be freed by this call */
    stream_headers_set (data);

    pthread_mutex_lock (&connection_info->lock);
    connection_info->headers_set = true;
    pthread_mutex_unlock (&connection_info->lock);

    cmsg_proxy_streaming_release_conn_info (connection_info);

    http_streaming_server_set_stream_headersSend (service, &send_msg);
}

void
http_streaming_impl_abort_stream_connection (const void *service, const stream_id *recv_msg)
{
    server_response send_msg = SERVER_RESPONSE_INIT;
    cmsg_proxy_stream_connection *connection_info = NULL;

    connection_info = cmsg_proxy_streaming_lookup_conn_by_id (recv_msg->id);
    if (!connection_info)
    {
        CMSG_SET_FIELD_VALUE (&send_msg, stream_found, false);
        http_streaming_server_abort_stream_connectionSend (service, &send_msg);
        return;
    }

    CMSG_SET_FIELD_VALUE (&send_msg, stream_found, true);

    stream_conn_abort (connection_info->connection);

    pthread_mutex_lock (&stream_connections_mutex);
    stream_connections_list = g_list_remove (stream_connections_list, connection_info);
    pthread_mutex_unlock (&stream_connections_mutex);

    connection_info->to_delete = true;
    connection_info->in_use = false;
    cmsg_proxy_streaming_release_conn_info (connection_info);

    http_streaming_server_abort_stream_connectionSend (service, &send_msg);
}
