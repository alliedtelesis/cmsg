/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#ifndef __CMSG_PROXY_H_
#define __CMSG_PROXY_H_

#include <stdbool.h>
#include <cmsg/cmsg.h>
#include <cmsg/cmsg_server.h>

/* Counter session prefix for CMSG Proxy */
#define CMSG_PROXY_COUNTER_APP_NAME_PREFIX  "CMSG PROXY"

typedef enum _cmsg_http_verb
{
    CMSG_HTTP_GET = 1,
    CMSG_HTTP_PUT = 2,
    CMSG_HTTP_POST = 3,
    CMSG_HTTP_DELETE = 4,
    CMSG_HTTP_PATCH = 5,
} cmsg_http_verb;

typedef struct _cmsg_proxy_header
{
    const char *key;
    char *value;
} cmsg_proxy_header;

typedef struct _cmsg_proxy_headers
{
    cmsg_proxy_header *headers;
    int num_headers;
} cmsg_proxy_headers;

typedef struct _cmsg_proxy_api_request_info
{
    const char *api_request_ip_address;
    const char *api_request_username;
} cmsg_proxy_api_request_info;

/* CMSG proxy input/request data */
typedef struct _cmsg_proxy_input
{
    /* URL the HTTP request is for. */
    const char *url;

    /* The query string sent with the request. Expected to be URL Encoded. */
    const char *query_string;

    /* The HTTP verb sent with the HTTP request. */
    cmsg_http_verb http_verb;

    /* Data received for the request. This could be raw file data in some cases, but is
     * usually a JSON string.
     */
    const char *data;

    /* Length of the input data. */
    size_t data_length;

    /* Information about the web API request. */
    cmsg_proxy_api_request_info web_api_info;

    /* The connection structure */
    void *connection;
} cmsg_proxy_input;

/* CMSG proxy output/response data */
typedef struct _cmsg_proxy_output
{
    /* A pointer to hold the response body to be sent in the HTTP response. This could be a
     * JSON string or raw file data. The pointer may return NULL if the rpc sends no
     * response data.
     * Will be freed as part of the call to cmsg_proxy_(passthrough_)free_output_contents.
     */
    char *response_body;

    /* Length of the response body. */
    size_t response_length;

    /* A pointer to the mime type that will be sent in the HTTP response */
    const char *mime_type;

    /* Pointer to hold any extra headers that should be returned.
     * Will be freed as part of the call to cmsg_proxy_(passthrough_)free_output_contents.
     */
    cmsg_proxy_headers *extra_headers;

    /* The response will be asynchronously written via an HTTP stream */
    bool stream_response;

    /* The HTTP status code to be returned */
    int http_status;
} cmsg_proxy_output;

typedef struct _cmsg_proxy_stream_response_data
{
    void *connection;
    char *data;
} cmsg_proxy_stream_response_data;

typedef int (*cmsg_api_func_ptr) ();
typedef bool (*pre_api_http_check_callback) (cmsg_http_verb http_verb, char **message);
typedef void (*cmsg_proxy_stream_response_send_func) (cmsg_proxy_stream_response_data
                                                      *data);
typedef void (*cmsg_proxy_stream_response_close_func) (void *connection);

typedef struct _cmsg_proxy_web_socket_info
{
    const char *id;
    cmsg_server *server;
    const void *connection;
} cmsg_proxy_web_socket_info;

typedef struct _cmsg_service_info
{
    const ProtobufCServiceDescriptor *service_descriptor;
    const ProtobufCMessageDescriptor *input_msg_descriptor;
    const ProtobufCMessageDescriptor *output_msg_descriptor;
    cmsg_api_func_ptr api_ptr;
    const char *url_string;
    cmsg_http_verb http_verb;
    const char *body_string;
} cmsg_service_info;

typedef struct _cmsg_proxy_api_info
{
    const cmsg_service_info *cmsg_http_get;
    const cmsg_service_info *cmsg_http_put;
    const cmsg_service_info *cmsg_http_post;
    const cmsg_service_info *cmsg_http_delete;
    const cmsg_service_info *cmsg_http_patch;
} cmsg_proxy_api_info;

#define CMSG_PROXY_SPECIAL_FIELD_FILE "_file"
#define CMSG_PROXY_SPECIAL_FIELD_FILE_NAME "file_name"
#define CMSG_PROXY_SPECIAL_FIELD_BODY "_body"

void cmsg_proxy_init (void);
void cmsg_proxy_deinit (void);
void cmsg_proxy_free_output_contents (cmsg_proxy_output *output);
bool cmsg_proxy (const cmsg_proxy_input *input, cmsg_proxy_output *output);
void cmsg_proxy_set_pre_api_http_check_callback (pre_api_http_check_callback cb);

void cmsg_proxy_passthrough_init (const char *library_path);
void cmsg_proxy_passthrough_deinit (void);
void cmsg_proxy_passthrough_free_output_contents (cmsg_proxy_output *output);
bool cmsg_proxy_passthrough (const cmsg_proxy_input *input, cmsg_proxy_output *output);

void cmsg_proxy_set_stream_response_send_function (cmsg_proxy_stream_response_send_func
                                                   func);
void cmsg_proxy_set_stream_response_close_function (cmsg_proxy_stream_response_close_func
                                                    func);



#endif /* __CMSG_PROXY_H_ */
