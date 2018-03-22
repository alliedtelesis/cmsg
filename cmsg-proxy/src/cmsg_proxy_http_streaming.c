/*
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */

#include <config.h>
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
} cmsg_proxy_stream_connection;

static cmsg_proxy_stream_response_send_func stream_response_send = NULL;
static cmsg_proxy_stream_response_close_func stream_response_close = NULL;

GList *stream_connections_list = NULL;
pthread_mutex_t stream_connections_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t stream_id_mutex = PTHREAD_MUTEX_INITIALIZER;



/**
 * Set the function used to send stream responses on an HTTP connection.
 * This should be called once by the web server when initialising cmsg proxy.
 *
 * @param func - The function used to send stream responses.
 */
void
cmsg_proxy_set_stream_response_send_function (cmsg_proxy_stream_response_send_func func)
{
    stream_response_send = func;
}

/**
 * Set the function used to finish the streaming of responses on an HTTP connection.
 * This should be called once by the web server when initialising cmsg proxy.
 *
 * @param func - The function used to finish the streaming of responses.
 */
void
cmsg_proxy_set_stream_response_close_function (cmsg_proxy_stream_response_close_func func)
{
    stream_response_close = func;
}

/**
 * Check whether the input message specifies the API should stream the response.
 *
 * @param msg_descriptor - The descriptor for the input message
 *
 * @returns true if the response should be streamed, false otherwise.
 */
static bool
cmsg_proxy_streaming_required (const ProtobufCMessageDescriptor *msg_descriptor)
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
cmsg_proxy_generate_stream_id (void)
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
cmsg_proxy_setup_streaming (void *connection, json_t **input_json_obj,
                            const ProtobufCMessageDescriptor *input_msg_descriptor,
                            const ProtobufCMessageDescriptor *output_msg_descriptor,
                            uint32_t *streaming_id)
{
    uint32_t id = 0;
    char buffer[50];
    cmsg_proxy_stream_connection *connection_info = NULL;

    if (!cmsg_proxy_streaming_required (input_msg_descriptor))
    {
        return false;
    }

    connection_info = CMSG_PROXY_CALLOC (1, sizeof (cmsg_proxy_stream_connection));
    if (!connection_info)
    {
        syslog (LOG_ERR, "Failed to allocate memory for cmsg proxy stream connection");
        return false;
    }

    id = cmsg_proxy_generate_stream_id ();

    /* VISTA548-114 TODO - make it so we don't need to convert to string */
    sprintf (buffer, "%u", id);

    _cmsg_proxy_set_internal_api_value (buffer, input_json_obj, input_msg_descriptor,
                                        "_streaming_id");

    connection_info->id = id;
    connection_info->output_msg_descriptor = output_msg_descriptor;
    connection_info->connection = connection;

    pthread_mutex_lock (&stream_connections_mutex);
    stream_connections_list = g_list_prepend (stream_connections_list, connection_info);
    pthread_mutex_unlock (&stream_connections_mutex);

    *streaming_id = id;
    return true;
}


void
cmsg_proxy_remove_stream_by_id (uint32_t id)
{
    GList *iter;
    bool found = false;
    cmsg_proxy_stream_connection *connection_info = NULL;

    pthread_mutex_lock (&stream_connections_mutex);
    for (iter = stream_connections_list; iter; iter = g_list_next (iter))
    {
        connection_info = (cmsg_proxy_stream_connection *) iter->data;
        if (connection_info->id == id)
        {
            found = true;
            break;
        }
    }
    if (found)
    {
        stream_connections_list = g_list_remove (stream_connections_list, connection_info);
        CMSG_PROXY_FREE (connection_info);
    }
    pthread_mutex_unlock (&stream_connections_mutex);
}


static cmsg_proxy_stream_connection *
cmsg_proxy_find_connection_by_id (uint32_t id)
{
    GList *iter;
    cmsg_proxy_stream_connection *connection_info = NULL;

    pthread_mutex_lock (&stream_connections_mutex);
    for (iter = stream_connections_list; iter; iter = g_list_next (iter))
    {
        connection_info = (cmsg_proxy_stream_connection *) iter->data;
        if (connection_info->id == id)
        {
            break;
        }
    }
    pthread_mutex_unlock (&stream_connections_mutex);

    return connection_info;
}


static gboolean
cmsg_proxy_streaming_server_receive (GIOChannel *source, GIOCondition condition,
                                     gpointer data)
{
    int fd = g_io_channel_unix_get_fd (source);
    cmsg_server *streaming_server = (cmsg_server *) data;

    if (cmsg_server_receive (streaming_server, fd) < 0)
    {
        g_io_channel_shutdown (source, true, NULL);
        g_io_channel_unref (source);
        return FALSE;
    }
    return TRUE;
}

/**
 * Callback function for server socket accept
 */
static gboolean
cmsg_proxy_streaming_server_accept (GIOChannel *source, GIOCondition condition,
                                    gpointer data)
{
    int server_socket = g_io_channel_unix_get_fd (source);
    int fd = -1;
    GIOChannel *server_read_channel = NULL;
    cmsg_server *streaming_server = (cmsg_server *) data;

    fd = cmsg_server_accept (streaming_server, server_socket);
    if (fd >= 0)
    {
        server_read_channel = g_io_channel_unix_new (fd);
        g_io_add_watch (server_read_channel, G_IO_IN, cmsg_proxy_streaming_server_receive,
                        streaming_server);
    }

    return TRUE;
}

static void *
cmsg_proxy_streaming_server_run (void *arg)
{
    GMainLoop *loop = g_main_loop_new (NULL, false);
    cmsg_server *streaming_server = NULL;
    GIOChannel *server_accept_channel = NULL;
    int server_socket = -1;

    streaming_server =
        cmsg_create_server_unix_rpc (CMSG_SERVICE_NOPACKAGE (http_streaming));

    server_socket = cmsg_server_get_socket (streaming_server);

    server_accept_channel = g_io_channel_unix_new (server_socket);

    g_io_add_watch (server_accept_channel, G_IO_IN, cmsg_proxy_streaming_server_accept,
                    streaming_server);

    g_main_loop_run (loop);
    g_main_loop_unref (loop);

    return NULL;
}

void
cmsg_proxy_streaming_init (void)
{
    int ret;
    pthread_t server_thread;

    ret = pthread_create (&server_thread, NULL, cmsg_proxy_streaming_server_run, NULL);

    if (ret != 0)
    {
        syslog (LOG_ERR, "Failed to start cmsg proxy streaming server thread");
    }
}

void
http_streaming_impl_send_stream_data (const void *service, const stream_data *recv_msg)
{
    server_response send_msg = SERVER_RESPONSE_INIT;
    ProtobufCMessage *message = NULL;
    cmsg_proxy_stream_connection *connection_info = NULL;
    ProtobufCAllocator *allocator = &cmsg_memory_allocator;
    json_t *converted_json_object = NULL;
    char *response_body = NULL;
    cmsg_proxy_stream_response_data *data = NULL;

    connection_info = cmsg_proxy_find_connection_by_id (recv_msg->id);
    if (!connection_info)
    {
        // VISTA548-115 TODO - handle this case
    }

    message = protobuf_c_message_unpack (connection_info->output_msg_descriptor, allocator,
                                         recv_msg->message_data.len,
                                         recv_msg->message_data.data);

    if (protobuf2json_object (message, &converted_json_object, NULL, 0) < 0)
    {
        // VISTA548-115 TODO - handle this case
    }

    response_body = json_dumps (converted_json_object, JSON_COMPACT);

    data = CMSG_PROXY_CALLOC (1, sizeof (cmsg_proxy_stream_response_data));
    data->connection = connection_info->connection;
    data->data = response_body;

    stream_response_send (data);

    if (recv_msg->status == STREAM_STATUS_CLOSE)
    {
        stream_response_close (connection_info->connection);
    }

    http_streaming_server_send_stream_dataSend (service, &send_msg);
}
