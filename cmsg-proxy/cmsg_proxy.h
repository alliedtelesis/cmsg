/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#ifndef __CMSG_PROXY_H_
#define __CMSG_PROXY_H_

#include <stdbool.h>
#include <cmsg/cmsg.h>
#include <cmsg/cmsg_server.h>

/* Standard HTTP/1.1 status codes */
#define HTTP_CODE_CONTINUE                  100 /* Continue with request, only partial content transmitted */
#define HTTP_CODE_SWITCHING                 101 /* Switching protocols */
#define HTTP_CODE_OK                        200 /* The request completed successfully */
#define HTTP_CODE_CREATED                   201 /* The request has completed and a new resource was created */
#define HTTP_CODE_ACCEPTED                  202 /* The request has been accepted and processing is continuing */
#define HTTP_CODE_NOT_AUTHORITATIVE         203 /* The request has completed but content may be from another source */
#define HTTP_CODE_NO_CONTENT                204 /* The request has completed and there is no response to send */
#define HTTP_CODE_RESET                     205 /* The request has completed with no content. Client must reset view */
#define HTTP_CODE_PARTIAL                   206 /* The request has completed and is returning partial content */
#define HTTP_CODE_MOVED_PERMANENTLY         301 /* The requested URI has moved permanently to a new location */
#define HTTP_CODE_MOVED_TEMPORARILY         302 /* The URI has moved temporarily to a new location */
#define HTTP_CODE_SEE_OTHER                 303 /* The requested URI can be found at another URI location */
#define HTTP_CODE_NOT_MODIFIED              304 /* The requested resource has changed since the last request */
#define HTTP_CODE_USE_PROXY                 305 /* The requested resource must be accessed via the location proxy */
#define HTTP_CODE_TEMPORARY_REDIRECT        307 /* The request should be repeated at another URI location */
#define HTTP_CODE_BAD_REQUEST               400 /* The request is malformed */
#define HTTP_CODE_UNAUTHORIZED              401 /* Authentication for the request has failed */
#define HTTP_CODE_PAYMENT_REQUIRED          402 /* Reserved for future use */
#define HTTP_CODE_FORBIDDEN                 403 /* The request was legal, but the server refuses to process */
#define HTTP_CODE_NOT_FOUND                 404 /* The requested resource was not found */
#define HTTP_CODE_BAD_METHOD                405 /* The request HTTP method was not supported by the resource */
#define HTTP_CODE_NOT_ACCEPTABLE            406 /* The requested resource cannot generate the required content */
#define HTTP_CODE_REQUEST_TIMEOUT           408 /* The server timed out waiting for the request to complete */
#define HTTP_CODE_CONFLICT                  409 /* The request had a conflict in the request headers and URI */
#define HTTP_CODE_GONE                      410 /* The requested resource is no longer available */
#define HTTP_CODE_LENGTH_REQUIRED           411 /* The request did not specify a required content length */
#define HTTP_CODE_PRECOND_FAILED            412 /* The server cannot satisfy one of the request preconditions */
#define HTTP_CODE_REQUEST_TOO_LARGE         413 /* The request is too large for the server to process */
#define HTTP_CODE_REQUEST_URL_TOO_LARGE     414 /* The request URI is too long for the server to process */
#define HTTP_CODE_UNSUPPORTED_MEDIA_TYPE    415 /* The request media type is not supported by the server or resource */
#define HTTP_CODE_RANGE_NOT_SATISFIABLE     416 /* The request content range does not exist for the resource */
#define HTTP_CODE_EXPECTATION_FAILED        417 /* The server cannot satisfy the Expect header requirements */
#define HTTP_CODE_NO_RESPONSE               444 /* The connection was closed with no response to the client */
#define HTTP_CODE_INTERNAL_SERVER_ERROR     500 /* Server processing or configuration error. No response generated */
#define HTTP_CODE_NOT_IMPLEMENTED           501 /* The server does not recognize the request or method */
#define HTTP_CODE_BAD_GATEWAY               502 /* The server cannot act as a gateway for the given request */
#define HTTP_CODE_SERVICE_UNAVAILABLE       503 /* The server is currently unavailable or overloaded */
#define HTTP_CODE_GATEWAY_TIMEOUT           504 /* The server gateway timed out waiting for the upstream server */
#define HTTP_CODE_BAD_VERSION               505 /* The server does not support the HTTP protocol version */
#define HTTP_CODE_INSUFFICIENT_STORAGE      507 /* The server has insufficient storage to complete the request */

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
void cmsg_proxy (const cmsg_proxy_input *input, cmsg_proxy_output *output);
void cmsg_proxy_set_pre_api_http_check_callback (pre_api_http_check_callback cb);

void cmsg_proxy_passthrough_init (const char *library_path);
void cmsg_proxy_passthrough_deinit (void);
void cmsg_proxy_passthrough_free_output_contents (cmsg_proxy_output *output);
void cmsg_proxy_passthrough (const cmsg_proxy_input *input, cmsg_proxy_output *output);

void cmsg_proxy_streaming_set_response_send_function (cmsg_proxy_stream_response_send_func
                                                      func);
void cmsg_proxy_streaming_set_response_close_function (cmsg_proxy_stream_response_close_func
                                                       func);



#endif /* __CMSG_PROXY_H_ */
