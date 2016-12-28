/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 *
 * The CMSG proxy is a library that can be used by a web server to proxy HTTP
 * requests into CMSG service APIs.
 *
 * The required information is auto-generated by protoc-cmsg using the HttpRules
 * defined for each rpc in the CMSG .proto files. The user of the library only
 * needs to call two functions:
 *
 * - cmsg_proxy_init() to initialise the library
 * - cmsg_proxy() for each HTTP request the user wishes to proxy through to the
 *   CMSG service APIs
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

static GList *proxy_clients_list = NULL;
static GNode *proxy_entries_tree = NULL;

/**
 * SET CMSG API info details to the proxy tree
 *
 * @param leaf_node - Add the CMSG service info to this leaf node
 * @param service_info - CMSG service info to be added
 */
static void
_cmsg_proxy_api_info_node_set (GNode *leaf_node, cmsg_service_info *service_info)
{
    cmsg_proxy_api_info *api_info = leaf_node->data;

    switch (service_info->http_verb)
    {
    case CMSG_HTTP_GET:
        api_info->cmsg_http_get = service_info;
        break;
    case CMSG_HTTP_PUT:
        api_info->cmsg_http_put = service_info;
        break;
    case CMSG_HTTP_POST:
        api_info->cmsg_http_post = service_info;
        break;
    case CMSG_HTTP_DELETE:
        api_info->cmsg_http_delete = service_info;
        break;
    case CMSG_HTTP_PATCH:
        api_info->cmsg_http_patch = service_info;
        break;
    }
}

/**
 * Get CMSG proxy API info node from the last node of a URL. If the API
 * info node doesn't exist create one otherwise return the existing
 * one. If the last node corresponding to a URL is the leaf node, then
 * we need to create one. If the last node is not a leaf node, check
 * its first child. If the first child is not a leaf node, create
 * cmsg_api_info_node. cmsg_api_info_node is always inserted as the
 * first child.
 * eg: url_string1 = "/v1/A/B/C"
 *     url_string2 = "/v1/A/B/C/D"
 *     url_string3 = "/v1/A/B"
 *     url_string4 = "/v1/A/B/C/E"
 *     url_string5 = "/v1/A/B/C/F"
 *     url_string6 = "/v1/A/B/C/G"
 *     url_string7 = "/v1/A/B/C/G/H"
 *
 *           --------
 *          |CMSG-API|   <====Root Node
 *           --------
 *              |
 *              |
 *            ------
 *           |  v1  | <=== Level 1
 *            ------
 *              |
 *              |
 *            -----
 *           |  A  |  <=== Level 2
 *            -----
 *              |
 *              |
 *            -----
 *           |  B  |  <=== Level 3
 *            -----
 *           /    \
 *          /      \
 *     --------   -----
 *    |API INFO| |  C  |--------------------------------------- <=== Level 3
 *     --------   -----               |            |          |
 *               /     \              |            |          |
 *              /       \             |            |          |
 *         --------     -----        -----       -----      -----
 *        |API INFO|   |  D  |      |  E  |     |  F  |    |  G  |------ <=== Level 4
 *         --------     -----        -----       -----      -----       |
 *                        |            |           |          |         |
 *                     --------     --------    --------   --------   -----
 *                    |API INFO|   |API INFO|  |API INFO| |API INFO| |  H  | <=== Level 5
 *                     --------     --------    --------   --------   -----
 *                                                                      |
 *                                                                   --------
 *                                                                  |API INFO|
 *                                                                   --------
 *
 *  Important Note: API INFO is added as the first child for a URL.
 *
 *  @param last_node Last GNode corresponding to a URL
 *  @return  Newly created cmsg_api_info_node or the existing one if found.
 */
static GNode *
_cmsg_proxy_api_info_node_new (GNode *last_node)
{
    GNode *first_child = NULL;
    GNode *cmsg_api_info_node = NULL;
    cmsg_proxy_api_info *cmsg_proxy_api_ptr;

    /* Insert cmsg_api_info_node as the first child of the last_node. */
    if (G_NODE_IS_LEAF (last_node))
    {
        cmsg_proxy_api_ptr = calloc (1, sizeof (*cmsg_proxy_api_ptr));
        cmsg_api_info_node = g_node_insert_data (last_node, 0, cmsg_proxy_api_ptr);
    }
    else
    {
        first_child = g_node_first_child (last_node);
        /* Check whether the first child is API info node. Otherwise create one and
         * insert as the first child.*/
        if (G_NODE_IS_LEAF (first_child))
        {
            cmsg_api_info_node = first_child;
        }
        else
        {
            cmsg_proxy_api_ptr = calloc (1, sizeof (*cmsg_proxy_api_ptr));
            cmsg_api_info_node = g_node_insert_data (last_node, 0, cmsg_proxy_api_ptr);
        }
    }

    return cmsg_api_info_node;
}

/**
 * Parse the given URL string and add to proxy_entries_tree.
 * Add 'cmsg_service_info' to the leaf node.
 * The parser believes the received 'url' is in the correct format.
 * eg: url_string = "/v5_4_7/statistics/interfaces/enabled"
 *     url_string = "/v5_4_8/statistics/interfaces/enabled"
 *     url_string = "/v5_4_8/statistics/interfaces/<name>/history"
 *     url_string = "/v5_4_8/statistics/interfaces/<name>/current"
 *     url_string = "/v5_4_8/statistics/interfaces"
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
 *    |interfaces|    |interfaces|----------
 *     ----------      ----------           |
 *      |               /      \            |
 *      |              /        \           |
 *   -------        --------    ------     ------
 *  |enabled|      |API INFO|  |enabled|  |<name>| <=== Parameter "<name>" is stored in the tree
 *   -------        --------    ------     ------
 *       |                       |         /   \
 *       |                       |        /     \
 *   --------                ---------   /       \
 *  |API INFO|              |API INFO | |         |
 *   --------                ---------  |         |
 *                                   -------  -------
 *                                  |history| |current|
 *                                   -------   -------
 *                                     |          |
 *                                     |          |
 *                                  --------   ---------
 *                                 |API INFO| |API INFO |
 *                                  --------   ---------
 * API INFO at the leaf node points to the corresponding cmsg_service_info
 *
 * @param service_info CMSG service information
 */
static gboolean
_cmsg_proxy_service_info_add (cmsg_service_info *service_info)
{
    char *tmp_url = NULL;
    char *next_entry = NULL;
    char *rest = NULL;
    GNode *parent_node = g_node_get_root (proxy_entries_tree);
    GNode *node = NULL;
    GNode *cmsg_api_info_node = NULL;
    gboolean found;

    tmp_url = strdup (service_info->url_string);

    for (next_entry = strtok_r (tmp_url, "/", &rest); next_entry;
         next_entry = strtok_r (NULL, "/", &rest))
    {
        found = FALSE;

        /* Check whether the node already exists in the tree. */
        node = g_node_first_child (parent_node);

        while (node)
        {
            /* API info node should be skipped */
            if (!G_NODE_IS_LEAF (node) && strcmp (node->data, next_entry) == 0)
            {
                found = TRUE;
                break;
            }
            node = g_node_next_sibling (node);
        }

        /* Add if it doesn't exist. Insert as the last child of parent_node. */
        if (found == FALSE)
        {
            node = g_node_insert_data (parent_node, -1, g_strdup (next_entry));
        }

        parent_node = node;
    }

    cmsg_api_info_node = _cmsg_proxy_api_info_node_new (parent_node);

    /* Fill the cmsg_service_info to the leaf node */
    _cmsg_proxy_api_info_node_set (cmsg_api_info_node, service_info);

    free (tmp_url);

    return TRUE;
}

/**
 * Initialise the cmsg proxy list with the autogenerated array entries
 *
 * @param array - Pointer to the start of the array of entries
 * @param length - Length of the array
 */
static void
_cmsg_proxy_service_info_init (cmsg_service_info *array, int length)
{
    int i = 0;

    for (i = 0; i < length; i++)
    {
        _cmsg_proxy_service_info_add (&array[i]);
    }
}

/**
 * Helper function used by _cmsg_proxy_find_client_by_service()
 * with g_list_find_custom() to find an entry from the proxy
 * client list based on service name.
 *
 * @param list_data - GList data passed in by g_list_find_custom()
 * @param service_name - Service name to match
 */
static int
_cmsg_proxy_service_name_cmp (void *list_data, void *service_name)
{
    cmsg_client *list_client = (cmsg_client *) list_data;

    return strcmp (list_client->descriptor->name, service_name);
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
    GList *found_data;

    found_data = g_list_find_custom (proxy_clients_list, service_descriptor->name,
                                     (GCompareFunc) _cmsg_proxy_service_name_cmp);
    if (found_data)
    {
        return found_data->data;
    }

    return NULL;
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

    client = cmsg_create_client_unix (service_descriptor);
    if (!client)
    {
        fprintf (stderr, "Failed to create client for service: %s",
                 service_descriptor->name);
        return;
    }

    proxy_clients_list = g_list_append (proxy_clients_list, (void *) client);
    return;
}

/**
 * Find CMSG service info corresponding to the given cmsg_http_verb from cmsg_proxy_api_info
 *
 * @param api_info - CMSG API info
 * @param verb - HTTP action verb
 *
 * @return Returns matching cmsg_service_info from api_info if not NULL
 */
static const cmsg_service_info *
cmsg_proxy_service_info_get (const cmsg_proxy_api_info *api_info, cmsg_http_verb verb)
{
    switch (verb)
    {
    case CMSG_HTTP_GET:
        return api_info->cmsg_http_get;
    case CMSG_HTTP_PUT:
        return api_info->cmsg_http_put;
    case CMSG_HTTP_POST:
        return api_info->cmsg_http_post;
    case CMSG_HTTP_DELETE:
        return api_info->cmsg_http_delete;
    case CMSG_HTTP_PATCH:
        return api_info->cmsg_http_patch;
    default:
        return NULL;
    }
}

/**
 * Callback to add CMSG clients.
 *
 * @param leaf_node - leaf node that contains cmsg_proxy_api_info
 * @param data - data passed in by the caller. This is NULL
 *
 * @returns TRUE always
 */
static gboolean
_cmsg_proxy_clients_add (GNode *leaf_node, gpointer data)
{
    cmsg_proxy_api_info *api_info = leaf_node->data;
    const cmsg_service_info *service_info;
    int action;

    for (action = CMSG_HTTP_GET; action <= CMSG_HTTP_PATCH; action++)
    {
        service_info = cmsg_proxy_service_info_get (api_info, action);
        if (service_info &&
            !_cmsg_proxy_find_client_by_service (service_info->service_descriptor))
        {
            _cmsg_proxy_create_client (service_info->service_descriptor);
        }
    }

    return TRUE;
}

/**
 * Initialise the CMSG clients required to connect to every service descriptor
 * used in the CMSG proxy entries tree. Traverse all the leaf nodes in the
 * GNode proxy entry tree. All the leaf nodes should be contain cmsg_proxy_api_info.
 */
static void
_cmsg_proxy_clients_init (void)
{
    GNode *root = g_node_get_root (proxy_entries_tree);

    /* Do not traverse if proxy_entries_tree is empty. */
    if (!G_NODE_IS_LEAF (root))
    {
        g_node_traverse (root, G_LEVEL_ORDER, G_TRAVERSE_LEAVES, -1,
                         _cmsg_proxy_clients_add, NULL);
    }
}

/**
 * Create a new json object from the given json string
 *
 * @param json_object - Place holder for the created json object
 * @param input_json - input json string to create the json object
 */
static void
_cmsg_proxy_json_object_create (json_t **json_object, const char *input_json)
{
    json_error_t error;

    *json_object = json_loads (input_json, 0, &error);
}

/**
 *
 * Destroy the given json object
 *
 * @param json_object - json object to be destroyed
 */
static void
_cmsg_proxy_json_object_destroy (json_t *json_object)
{
    if (json_object)
    {
        json_decref (json_object);
    }
}

/**
 * Update the json object according to the new key,value pair. The 'key'
 * is within '{ }' so we need to parse it to remove the symbols before
 * creating the new object.
 *
 * @json_object - json object to update
 * @key - key used to create new json object
 * @value - value for the new json object
 *
 * @returns TRUE if the key is parsed and updated the json object successfully
 * otherwise FALSE
 */
static gboolean
_cmsg_proxy_key_parser (json_t **json_object, const char *key, const char *value)
{
    json_t *new_object;
    char *tmp_key;
    char *ptr;

    /* Return early if 'key' is not in '{ }' */
    if (key[0] != '{' && key[strlen (key)] != '}')
    {
        return FALSE;
    }

    tmp_key = strdup (key);
    ptr = tmp_key;

    ptr++;
    ptr[strlen (ptr) - 1] = '\0';

    new_object = json_pack ("{ss}", ptr, value);

    if (*json_object)
    {
        json_object_update (*json_object, new_object);
    }
    else
    {
        *json_object = new_object;
    }

    free (tmp_key);

    return TRUE;
}

/**
 * Lookup a cmsg_service_info entry from the proxy tree based on URL and
 * HTTP verb and update jason_object if any parameter found in the URL
 *
 * @param url - URL string to use for the lookup.
 * @param http_verb - HTTP verb to use for the lookup.
 * @param json_object - jason object to update
 *
 * @return - Pointer to the cmsg_service_info entry if found, NULL otherwise.
 */
static const cmsg_service_info *
_cmsg_proxy_find_service_from_url_and_verb (const char *url, cmsg_http_verb verb,
                                            json_t **json_object)
{
    GNode *node;
    char *tmp_url;
    char *next_entry = NULL;
    char *rest = NULL;
    GNode *parent_node;
    GNode *info_node;
    const char *key;

    tmp_url = strdup (url);
    parent_node = g_node_get_root (proxy_entries_tree);

    for (next_entry = strtok_r (tmp_url, "/", &rest); next_entry;
         next_entry = strtok_r (NULL, "/", &rest))
    {
        node = g_node_first_child (parent_node);
        while (node)
        {
            if (!G_NODE_IS_LEAF (node) && strcmp (next_entry, node->data) == 0)
            {
                parent_node = node;
                break;
            }
            else
            {
                key = (const char *) node->data;
                /* If a key is found, go to the next level of the GNode tree. */
                if (_cmsg_proxy_key_parser (json_object, key, next_entry))
                {
                    parent_node = node;
                    break;
                }
            }
            node = g_node_next_sibling (node);
        }

        /* No match found. */
        if (node == NULL)
        {
            free (tmp_url);
            return NULL;
        }
    }

    free (tmp_url);

    info_node = g_node_first_child (parent_node);
    if ((info_node) != NULL && G_NODE_IS_LEAF (info_node))
    {
        return cmsg_proxy_service_info_get (info_node->data, verb);

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
_cmsg_proxy_convert_json_to_protobuf (json_t *json_object,
                                      const ProtobufCMessageDescriptor *msg_descriptor,
                                      ProtobufCMessage **output_protobuf)
{
    if (json2protobuf_object (json_object, msg_descriptor, output_protobuf, NULL, 0) < 0)
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

/**
 * Initialise the cmsg proxy library
 */
void
cmsg_proxy_init (void)
{
    /* This is to pass some build targets with interface statistics monitoring
     * disabled. Once we find a way to initialize the proxy list at compile
     * time or similar, we can remove the code. */
    _cmsg_proxy_service_info_init (NULL, 0);

    /* Create GNode proxy entries tree. */
    proxy_entries_tree = g_node_new (CMSG_API_VERSION_STR);

#ifdef HAVE_STATMOND
    _cmsg_proxy_service_info_init (statmond_proxy_array_get (),
                                   statmond_proxy_array_size ());
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
    json_t *json_object = NULL;

    if (input_json)
    {
        _cmsg_proxy_json_object_create (&json_object, input_json);
    }

    service_info = _cmsg_proxy_find_service_from_url_and_verb (url, http_verb,
                                                               &json_object);
    if (service_info == NULL)
    {
        /* The cmsg proxy does not know about this url and verb combination */
        _cmsg_proxy_json_object_destroy (json_object);
        return false;
    }

    client = _cmsg_proxy_find_client_by_service (service_info->service_descriptor);
    if (client == NULL)
    {
        /* This should not occur but check for it */
        *http_status = HTTP_CODE_INTERNAL_SERVER_ERROR;
        _cmsg_proxy_json_object_destroy (json_object);
        return true;
    }

    if (json_object)
    {
        ret = _cmsg_proxy_convert_json_to_protobuf (json_object,
                                                    service_info->input_msg_descriptor,
                                                    &input_proto_message);
        if (!ret)
        {
            /* The JSON sent with the request is malformed */
            *http_status = HTTP_CODE_BAD_REQUEST;
            _cmsg_proxy_json_object_destroy (json_object);
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
        _cmsg_proxy_json_object_destroy (json_object);
        return true;
    }

    free (input_proto_message);

    ret = _cmsg_proxy_convert_protobuf_to_json (output_proto_message, output_json);
    if (!ret)
    {
        /* This should not occur (the ProtobufCMessage structure returned
         * by the CMSG api should always be well formed) but check for it */
        CMSG_FREE_RECV_MSG (output_proto_message);
        *http_status = HTTP_CODE_INTERNAL_SERVER_ERROR;

        _cmsg_proxy_json_object_destroy (json_object);
        return true;
    }

    _cmsg_proxy_json_object_destroy (json_object);
    CMSG_FREE_RECV_MSG (output_proto_message);
    *http_status = HTTP_CODE_OK;
    return true;
}
