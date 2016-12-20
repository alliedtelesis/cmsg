/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */

#include <config.h>
#include "cmsg_proxy.h"
#include <glib.h>
#ifdef HAVE_STATMOND
#include <ipc/statmond_proxy_def.h>
#endif /* HAVE_STATMOND */
#include <string.h>
#include <protobuf2json.h>
#include <cmsg/cmsg_client.h>

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

/* Current CMSG API version string */
#define CMSG_API_VERSION_STR                "CMSG-API"

static GList *proxy_entries_list = NULL;
static GList *proxy_clients_list = NULL;
static GNode *proxy_entries_tree = NULL;

/**
 * Parse the given URL string and add to proxy_entries_tree.
 * The parser believes the received 'url' is in the correct format.
 * eg: url_string = "/v5_4_7/statistics/interfaces/enabled"
 *     url_string = "/v5_4_8/statistics/interfaces/enabled"
 *     url_string = "/v5_4_8/statistics/interfaces/<name>/history"
 *     url_string = "/v5_4_8/statistics/interfaces/<name>/current"
 *
 *             --------
 *            |CMSG-API|   <====Root Node
 *             --------
 *            /        \
 *           /          \
 *       ------        ------
 *      |v5_4_7|      |v5_4_8|  <==== First children
 *       ------        ------
 *         |             |
 *         |             |
 *     ----------      ----------
 *    |statistics|    |statistics|
 *     ----------      ----------
 *         |              |
 *         |              |
 *     ----------      ----------
 *    |interfaces|    |interfaces|
 *     ----------      ----------
 *         |              /      \
 *         |             /        \
 *      -------       -------    ------
 *     |enabled|     |enabled|  |<name>| <=== Parameter "<name>" is stored in the tree
 *      -------       -------    ------
 *                                /   \
 *                               /     \
 *                              /       \
 *                          -------     -------
 *                         |history|   |current|
 *                          -------     -------
 * @param url The URL string to be tokenized and added to the tree
 */
static gboolean
_cmsg_proxy_add_service_info (const char *url)
{
    char *tmp_url = NULL;
    char *next_entry = NULL;
    char *rest = NULL;
    GNode *parent_node = g_node_get_root (proxy_entries_tree);
    GNode *node = NULL;
    gboolean found;

    tmp_url = strdup (url);

    for (next_entry = strtok_r (tmp_url, "/", &rest); next_entry;
         next_entry = strtok_r (NULL, "/", &rest))
    {
        found = FALSE;

        /* Check whether the node already existis in the tree. */
        node = g_node_first_child (parent_node);

        while (node)
        {
            if (strcmp (node->data, next_entry) == 0)
            {
                found = TRUE;
                break;
            }
            node = g_node_next_sibling (node);
        }

        /* Add if it doesn't exist. */
        if (found == FALSE)
        {
            node = g_node_append_data (parent_node, g_strdup (next_entry));
        }

        parent_node = node;
    }

    free (tmp_url);

    return TRUE;
}

static void
_cmsg_proxy_list_init (cmsg_service_info *array, int length)
{
    int i = 0;

    for (i = 0; i < length; i++)
    {
        _cmsg_proxy_add_service_info (array[i].url_string);

        proxy_entries_list = g_list_append (proxy_entries_list, (void *) &array[i]);
    }
}

/**
 * Lookup a CMSG client from the proxy_clients_list based on service_descriptor
 *
 * @param service_descriptor - CMSG service descriptor to use for the lookup
 *
 * @return - Pointer to the CMSG client if found, NULL otherwise.
 */
static cmsg_client *
_cmsg_proxy_find_client_by_service (const ProtobufCServiceDescriptor *service_descriptor)
{
    GList *iter;
    cmsg_client *iter_data;

    for (iter = proxy_clients_list; iter != NULL; iter = g_list_next (iter))
    {
        iter_data = (cmsg_client *) iter->data;
        if (strcmp (iter_data->descriptor->name, service_descriptor->name) == 0)
        {
            return iter_data;
        }
    }

    return NULL;
}

/**
 * Get the CMSG server socket name from the CMSG service descriptor.
 *
 * @param service_descriptor - CMSG service descriptor to get server socket name from
 *
 * @return - String representing the server socket name. The memory for this string must
 *           be freed by the caller.
 */
static char *
_cmsg_proxy_server_socket_name_get (const ProtobufCServiceDescriptor *service_descriptor)
{
    char *copy_str = NULL;
    char *iter;

    asprintf (&copy_str, "/tmp/%s", service_descriptor->name);

    /* Replace the '.' in the name with '_' */
    iter = copy_str;
    while (*iter)
    {
        if (*iter == '.')
        {
            *iter = '_';
        }
        iter++;
    }

    return copy_str;
}

/**
 * Create a CMSG client to connect to the input service descriptor and
 * add this client to the proxy clients list.
 *
 * @param service_descriptor - CMSG service descriptor to connect the client to
 */
static void
_cmsg_proxy_create_client (const ProtobufCServiceDescriptor *service_descriptor)
{
    cmsg_client *client = NULL;
    char *server_socket_name = _cmsg_proxy_server_socket_name_get (service_descriptor);

    client = cmsg_create_client_unix (server_socket_name, service_descriptor);
    if (!client)
    {
        fprintf (stderr, "Failed to create client for unix socket %s", server_socket_name);
        free (server_socket_name);
        return;
    }

    proxy_clients_list = g_list_append (proxy_clients_list, (void *) client);
    free (server_socket_name);
    return;
}

/**
 * Initialise the CMSG clients required to connect to every service descriptor
 * used in the CMSG proxy entries list.
 */
static void
_cmsg_proxy_clients_init (void)
{
    GList *iter;
    cmsg_service_info *iter_data;

    for (iter = proxy_entries_list; iter != NULL; iter = g_list_next (iter))
    {
        iter_data = (cmsg_service_info *) iter->data;
        if (!_cmsg_proxy_find_client_by_service (iter_data->service_descriptor))
        {
            _cmsg_proxy_create_client (iter_data->service_descriptor);
        }
    }
}

/**
 * Lookup a cmsg_service_info entry from the proxy list based on URL and
 * HTTP verb.
 *
 * @param url - URL string to use for the lookup.
 * @param http_verb - HTTP verb to use for the lookup.
 *
 * @return - Pointer to the cmsg_service_info entry if found, NULL otherwise.
 */
static const cmsg_service_info *
_cmsg_proxy_find_service_from_url_and_verb (const char *url, cmsg_http_verb verb)
{
    GList *iter;
    cmsg_service_info *iter_data;

    for (iter = proxy_entries_list; iter != NULL; iter = g_list_next (iter))
    {
        iter_data = (cmsg_service_info *) iter->data;
        if ((strcmp (url, iter_data->url_string) == 0) && (iter_data->http_verb == verb))
        {
            return iter_data;
        }
    }

    return NULL;
}

/**
 * Convert the input json string into a protobuf message structure.
 *
 * @param input_json - The json string to convert.
 * @param msg_descriptor - The message descriptor that defines the protobuf
 *                         message to convert the json string to.
 * @param output_protobuf - A pointer to store the output protobuf message.
 *                          If the conversion succeeds then this pointer must
 *                          be freed by the caller.
 *
 * @return - true on success, false on failure.
 */
static bool
_cmsg_proxy_convert_json_to_protobuf (char *input_json,
                                      const ProtobufCMessageDescriptor *msg_descriptor,
                                      ProtobufCMessage **output_protobuf)
{
    if (json2protobuf_string (input_json, 0, msg_descriptor, output_protobuf, NULL, 0) < 0)
    {
        return false;
    }

    return true;
}

/**
 * Convert the input protobuf message into a json string.
 *
 * @param input_protobuf - The protobuf message to convert.
 * @param output_json - A pointer to store the output json string.
 *                      If the conversion succeeds then this pointer must
 *                      be freed by the caller.
 *
 * @return - true on success, false on failure.
 */
static bool
_cmsg_proxy_convert_protobuf_to_json (ProtobufCMessage *input_protobuf, char **output_json)
{
    if (protobuf2json_string (input_protobuf, JSON_INDENT (4), output_json, NULL, 0) < 0)
    {
        return false;
    }

    return true;
}

/**
 * Helper function to call the CMSG api function pointer in the
 * cmsg service info entry. This is required as the api function
 * takes a different number of parameters depending on the input/
 * output message types.
 *
 * @param client - CMSG client to call the API with
 * @param input_msg - Input message to send with the API
 * @param output_msg - Pointer for the received message from the API
 *                     to be stored in.
 * @param service_info - Service info entry that contains the API
 *                       function to call.
 *
 * @returns - HTTP_CODE_OK on success,
 *            HTTP_CODE_BAD_REQUEST if the input message is NULL when the
 *            api expects an input message,
 *            HTTP_CODE_INTERNAL_SERVER_ERROR if CMSG fails internally.
 */
static int
_cmsg_proxy_call_cmsg_api (const cmsg_client *client, ProtobufCMessage *input_msg,
                           ProtobufCMessage **output_msg,
                           const cmsg_service_info *service_info)
{
    int ret;

    if (input_msg == NULL &&
        strcmp (service_info->input_msg_descriptor->name, "dummy") != 0)
    {
        return HTTP_CODE_BAD_REQUEST;
    }

    if (strcmp (service_info->input_msg_descriptor->name, "dummy") == 0)
    {
        ret = service_info->api_ptr (client, output_msg);
    }
    else if (strcmp (service_info->output_msg_descriptor->name, "dummy") == 0)
    {
        ret = service_info->api_ptr (client, input_msg);
    }
    else
    {
        ret = service_info->api_ptr (client, input_msg, output_msg);
    }

    if (ret == CMSG_RET_OK)
    {
        return HTTP_CODE_OK;
    }
    else
    {
        return HTTP_CODE_INTERNAL_SERVER_ERROR;
    }
}

void
cmsg_proxy_init (void)
{
    /* This is to pass some build targets with interface statistics monitoring
     * disabled. Once we find a way to initialize the proxy list at compile
     * time or similar, we can remove the code. */
    _cmsg_proxy_list_init (NULL, 0);

    /* Create GNode proxy entries tree. */
    proxy_entries_tree = g_node_new (CMSG_API_VERSION_STR);

#ifdef HAVE_STATMOND
    _cmsg_proxy_list_init (statmond_proxy_array_get (), statmond_proxy_array_size ());
#endif /* HAVE_STATMOND */

    _cmsg_proxy_clients_init ();
}

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
    const cmsg_service_info *service_info = NULL;
    const cmsg_client *client = NULL;
    ProtobufCMessage *input_proto_message = NULL;
    ProtobufCMessage *output_proto_message = NULL;
    bool ret = false;
    int result;

    service_info = _cmsg_proxy_find_service_from_url_and_verb (url, http_verb);
    if (service_info == NULL)
    {
        /* The cmsg proxy does not know about this url and verb combination */
        return false;
    }

    client = _cmsg_proxy_find_client_by_service (service_info->service_descriptor);
    if (client == NULL)
    {
        /* This should not occur but check for it */
        *http_status = HTTP_CODE_INTERNAL_SERVER_ERROR;
        return true;
    }

    if (input_json)
    {
        ret = _cmsg_proxy_convert_json_to_protobuf ((char *) input_json,
                                                    service_info->input_msg_descriptor,
                                                    &input_proto_message);
        if (!ret)
        {
            /* The JSON sent with the request is malformed */
            *http_status = HTTP_CODE_BAD_REQUEST;
            return true;
        }
    }

    result = _cmsg_proxy_call_cmsg_api (client, input_proto_message,
                                        &output_proto_message, service_info);
    if (result != HTTP_CODE_OK)
    {
        /* Something went wrong calling the CMSG api */
        free (input_proto_message);
        *http_status = result;
        return true;
    }

    ret = _cmsg_proxy_convert_protobuf_to_json (output_proto_message, output_json);
    if (!ret)
    {
        /* This should not occur (the ProtobufCMessage structure returned
         * by the CMSG api should always be well formed) but check for it */
        free (input_proto_message);
        CMSG_FREE_RECV_MSG (output_proto_message);
        *http_status = HTTP_CODE_INTERNAL_SERVER_ERROR;
        return true;
    }

    free (input_proto_message);
    CMSG_FREE_RECV_MSG (output_proto_message);
    *http_status = HTTP_CODE_OK;
    return true;
}
