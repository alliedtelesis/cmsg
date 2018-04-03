/*
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */

#include <config.h>
#include <protobuf2json.h>
#include "cmsg_proxy_index.h"
#include "cmsg_proxy_tree.h"
#include "cmsg_proxy_private.h"

/* This is the prefix that should be prepended to paths in the proto files to get the
 * absolute path on the device.  It is returned in the indexing function, so should be
 * updated if the API base is ever moved.  If the api is ever moved, the indexing API
 * needs to continue to work at the original location.
 */
#define API_PREFIX "/api"

/* Wrapper structure for data passed to cmsg_proxy_index_add_element */
struct index_add_elem_data
{
    json_t *api_array;
    const char *filter;
};

/**
 * Helper function to get search pattern from passed in query string.
 * @param query_string - Url encoded query string to look for search pattern in.
 * @return - Allocated string containing search pattern. or NULL if search parameter is not
 * set or is empty.
 */
static char *
cmsg_proxy_index_search_pattern (const char *query_string)
{
    GList *query_params = NULL;
    GList *node = NULL;
    char *search_pattern = NULL;
    cmsg_url_parameter *param;

    cmsg_proxy_parse_query_parameters (query_string, &query_params);

    for (node = query_params; node; node = node->next)
    {
        param = (cmsg_url_parameter *) node->data;
        if (param && param->key && strcmp (param->key, "search_string") == 0)
        {
            if (param->value && param->value[0] != '\0')
            {
                search_pattern = strdup (param->value);
                break;
            }
        }
    }

    g_list_free_full (query_params, cmsg_proxy_free_url_parameter);
    return search_pattern;
}


/**
 * GNodeTraverseFunc to add API tree leaf entry element to index.
 * @param node - API tree leaf node
 * @param data - Pointer to index_add_elem_data struct containing json array to append to
 *               and an optional search filter to match against.
 */
static gboolean
cmsg_proxy_index_add_element (GNode *node, gpointer data)
{
    struct index_add_elem_data *elem_data = (struct index_add_elem_data *) data;
    json_t *api_array = elem_data->api_array;
    const char *filter = elem_data->filter;
    const cmsg_proxy_api_info *api_info = node->data;
    json_t *method_array = NULL;
    const char *url_string = NULL;
    json_t *api_object = NULL;

    if (!api_info)
    {
        /* Leaf node without data is unexpected.  Return FALSE to skip. */
        return FALSE;
    }

    method_array = json_array ();
    if (api_info->cmsg_http_delete)
    {
        url_string = api_info->cmsg_http_delete->url_string;
        json_array_append_new (method_array, json_string ("DELETE"));
    }
    if (api_info->cmsg_http_get)
    {
        url_string = api_info->cmsg_http_get->url_string;
        json_array_append_new (method_array, json_string ("GET"));
    }
    if (api_info->cmsg_http_patch)
    {
        url_string = api_info->cmsg_http_patch->url_string;
        json_array_append_new (method_array, json_string ("PATCH"));
    }
    if (api_info->cmsg_http_post)
    {
        url_string = api_info->cmsg_http_post->url_string;
        json_array_append_new (method_array, json_string ("POST"));
    }
    if (api_info->cmsg_http_put)
    {
        url_string = api_info->cmsg_http_put->url_string;
        json_array_append_new (method_array, json_string ("PUT"));
    }

    /* No url string found or doesn't match the filter, skip */
    if (!url_string || (filter && !strstr (url_string, filter)))
    {
        json_array_clear (method_array);
        json_decref (method_array);
        return FALSE;
    }

    api_object = json_object ();
    json_object_set_new (api_object, "path", json_string (url_string));
    json_object_set_new (api_object, "methods", method_array);
    json_array_append_new (api_array, api_object);

    return FALSE;
}


/**
 * Generates an allocated string containing a list of all APIs available on the device in
 * the format below.  If the query parameters have a value for the "search" parameter, only
 * APIs that have this string as a substring will be returned. methods can be "DELETE",
 * "GET", "PATCH", "POST" or "PUT".  The basepath is the prefix that must be attached to
 * the returned paths to get the absolute path on the device.
 *
 * {
 *   "basepath": prefix,
 *   "paths" [
 *     {
 *       "path": "/v0.1/atmf_application_proxy/blacklist_entries/{m_device_ip}",
 *       "methods": [
 *         "DELETE",
 *         "PUT"
 *       ]
 *     },
 *     {
 *       "path": "/v0.1/vlan/vlans/{id}",
 *       "methods": [
 *         "DELETE",
 *         "GET",
 *         "PUT"
 *        ]
 *      }
 *   ]
 * }
 *
 * @param query_string - The query string sent with the request. Expected to be URL Encoded.
 * @param output - cmsg proxy response data.
 *
 * @return - http status code for request.
 */
int
cmsg_proxy_index (const char *query_string, cmsg_proxy_output *output)
{
    char *search_pattern = NULL;
    json_t *result = NULL;
    json_t *api_array = NULL;
    struct index_add_elem_data traverse_data;

    if (!output)
    {
        return HTTP_CODE_INTERNAL_SERVER_ERROR;
    }

    search_pattern = cmsg_proxy_index_search_pattern (query_string);
    api_array = json_array ();

    traverse_data.filter = search_pattern;
    traverse_data.api_array = api_array;

    if (!cmsg_proxy_tree_foreach_leaf (cmsg_proxy_index_add_element, &traverse_data))
    {
        free (search_pattern);
        json_decref (api_array);
        return HTTP_CODE_INTERNAL_SERVER_ERROR;
    }

    result = json_object ();

    json_object_set_new (result, "basepath", json_string (API_PREFIX));
    json_object_set (result, "paths", api_array);

    cmsg_proxy_json_t_to_output (result, JSON_ENCODE_ANY | JSON_COMPACT, output);

    free (search_pattern);
    json_decref (api_array);
    json_decref (result);

    return HTTP_CODE_OK;
}
