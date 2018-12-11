/*
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */

#include <stdbool.h>
#include <protobuf2json.h>
#include <cmsg/cmsg_server.h>
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

static GList *stream_connections_list = NULL;
static pthread_mutex_t stream_connections_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t stream_id_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t server_thread;

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

static void
stream_headers_set (cmsg_proxy_stream_header_data *data)
{
    if (_stream_headers_set)
    {
        _stream_headers_set (data);
    }
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
            stream_connections_list = g_list_remove (stream_connections_list,
                                                     connection_info);
            pthread_mutex_destroy (&connection_info->lock);
            CMSG_PROXY_FREE (connection_info);
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
 * Function to be called when the 'cmsg_proxy_streaming_server_run'
 * thread is cancelled. This simply cleans up and frees any sockets/
 * memory that was used by the thread.
 *
 * @param poll_info - Structure containing the cmsg_server and sockets used
 *                    by the thread.
 */
static void
cmsg_proxy_streaming_server_cleanup (server_poll_info *poll_info)
{
    int fd;

    for (fd = 0; fd < poll_info->fd_max; fd++)
    {
        if (FD_ISSET (fd, &poll_info->readfds))
        {
            close (fd);
        }
    }

    cmsg_destroy_server_and_transport (poll_info->server);
}

/**
 * The thread that implements the cmsg proxy http streaming functionality.
 * Simply runs a cmsg server that clients can send their streaming responses to.
 *
 * @param arg - Unused
 */
static void *
cmsg_proxy_streaming_server_run (void *arg)
{
    server_poll_info poll_info;
    int fd = -1;

    poll_info.server = NULL;
    poll_info.fd_max = 0;
    FD_ZERO (&poll_info.readfds);

    pthread_cleanup_push ((void (*)(void *)) cmsg_proxy_streaming_server_cleanup,
                          &poll_info);

    poll_info.server =
        cmsg_create_server_unix_rpc (CMSG_SERVICE_NOPACKAGE (http_streaming));

    fd = cmsg_server_get_socket (poll_info.server);
    poll_info.fd_max = fd + 1;
    FD_SET (fd, &poll_info.readfds);

    while (true)
    {
        cmsg_server_receive_poll (poll_info.server, -1, &poll_info.readfds,
                                  &poll_info.fd_max);
    }

    pthread_cleanup_pop (1);

    return NULL;
}

/**
 * Initialise the cmsg proxy http streaming functionality.
 */
void
cmsg_proxy_streaming_init (void)
{
    int ret;

    ret = pthread_create (&server_thread, NULL, cmsg_proxy_streaming_server_run, NULL);

    if (ret != 0)
    {
        syslog (LOG_ERR, "Failed to start cmsg proxy streaming server thread");
        return;
    }

    pthread_setname_np (server_thread, "cmsg_proxy_http");
}

/**
 * Deinitialise the cmsg proxy http streaming functionality.
 */
void
cmsg_proxy_streaming_deinit (void)
{
    pthread_cancel (server_thread);
    pthread_join (server_thread, NULL);
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
            stream_connections_list = g_list_remove (stream_connections_list,
                                                     connection_info);
            cmsg_proxy_streaming_delete_conn_info (connection_info);
            break;
        }
    }
    pthread_mutex_unlock (&stream_connections_mutex);
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

    cmsg_proxy_strip_ant_result (&message);

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
        http_streaming_server_send_stream_dataSend (service, &send_msg);
        return;
    }

    CMSG_SET_FIELD_VALUE (&send_msg, stream_found, true);

    data = CMSG_PROXY_CALLOC (1, sizeof (cmsg_proxy_stream_response_data));
    data->connection = connection_info->connection;
    data->length = recv_msg->message_data.len;
    data->data = CMSG_PROXY_CALLOC (1, recv_msg->message_data.len);
    if (data->data)
    {
        memcpy (data->data, recv_msg->message_data.data, recv_msg->message_data.len);
    }

    stream_response_send (data);

    cmsg_proxy_streaming_release_conn_info (connection_info);

    http_streaming_server_send_stream_dataSend (service, &send_msg);
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
        http_streaming_server_send_stream_dataSend (service, &send_msg);
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
                    "'CONTENT_TYPE_FILE' missing file_info field");
            cmsg_proxy_streaming_release_conn_info (connection_info);
            http_streaming_server_send_stream_dataSend (service, &send_msg);
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
        cmsg_proxy_streaming_release_conn_info (connection_info);
        http_streaming_server_send_stream_dataSend (service, &send_msg);
        return;
    }

    headers->headers = header_array;
    headers->num_headers = n_headers;
    data->headers = headers;

    stream_headers_set (data);

    cmsg_proxy_streaming_release_conn_info (connection_info);

    http_streaming_server_send_stream_dataSend (service, &send_msg);
}
