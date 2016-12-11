/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */

#include <config.h>
#include "cmsg_proxy.h"
#include <glib.h>
#include <ipc/statmond_proxy_def.h>

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

static GList *proxy_list = NULL;

static void
_cmsg_proxy_init (cmsg_service_info *array, int length)
{
    int i = 0;

    for (i = 0; i < length; i++)
    {
        proxy_list = g_list_append (proxy_list, (void *) &array[i]);
    }
}

#ifndef HAVE_UNITTEST
void
cmsg_proxy_init (void)
{
    _cmsg_proxy_init (statmond_proxy_array_get (), statmond_proxy_array_size ());
}
#endif /* !HAVE_UNITTEST */

/**
 * Proxy an HTTP request into the AW+ CMSG internal API. Uses the HttpRules defined
 * for each rpc defined in the CMSG .proto files.
 *
 * @param url - URL the HTTP request is for.
 * @param http_verb - The HTTP verb sent with the HTTP request.
 * @param input_json - A string representing the JSON data sent with the HTTP request.
 * @param output_json - A pointer to a string that will store the output JSON data to.
 *                      be sent with the HTTP response. This pointer may be NULL if the
 *                      rpc does not send any response data and the pointer must be
 *                      freed by the caller (if it is non NULL).
 * @param http_status - A pointer to an integer that will store the HTTP status code to
 *                      be sent with the HTTP response.
 *
 * @return - true if the CMSG proxy actioned the request (i.e. it knew about the URL
 *           because it is defined on an rpc in the .proto files).
 *           false if the CMSG proxy performed no action (i.e. it could not map the URL
 *           to a CMSG API).
 */
bool
cmsg_proxy (const char *url, cmsg_http_verb http_verb, const char *input_json,
            char **output_json, int *http_status)
{
    return true;
}
