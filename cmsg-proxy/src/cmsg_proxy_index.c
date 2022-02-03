/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */

#include <protobuf2json.h>
#include "cmsg_proxy_index.h"
#include "cmsg_proxy_tree.h"
#include "cmsg_proxy_private.h"
#include "cmsg_proxy_config.h"

#define CMSG_PROXY_PRIVILEGE_NOT_SET    0
#define CMSG_PROXY_PRIVILEGE_MIN        1
#define CMSG_PROXY_PRIVILEGE_MAX        15

/* Wrapper structure for data passed to cmsg_proxy_index_add_element */
struct index_add_elem_data
{
    json_t *api_array;
    const char *search_pattern;
    int privilege;
    int readonly_privilege;
};

/**
 * Helper function to get search pattern from passed in query string.
 * @param query_string - Url encoded query string to look for search pattern in.
 * @param search_pattern - Pointer to return allocated string containing search pattern.
 * @param privilege - Pointer to store privilege level from the query string.
 */
static void
cmsg_proxy_index_parse_query_param (const char *query_string, char **search_pattern,
                                    int *privilege)
{
    GList *query_params = NULL;
    GList *node = NULL;
    cmsg_url_parameter *param;
    const char *search_string_param = NULL;
    const char *privilege_param = NULL;
    int number;

    cmsg_proxy_parse_query_parameters (query_string, &query_params);

    for (node = query_params; node; node = node->next)
    {
        param = (cmsg_url_parameter *) node->data;
        if (param && param->key && param->value && param->value[0] != '\0')
        {
            /* Use the first occurrence of the param */
            if (strcmp (param->key, "search_string") == 0)
            {
                if (!search_string_param)
                {
                    search_string_param = param->value;
                }
            }
            else if (strcmp (param->key, "privilege") == 0)
            {
                if (!privilege_param)
                {
                    privilege_param = param->value;
                }
            }
        }
    }

    /* Return the specified params */
    if (search_pattern && search_string_param)
    {
        *search_pattern = strdup (search_string_param);
    }
    if (privilege && privilege_param)
    {
        number = strtoll (privilege_param, NULL, 10);
        if (number >= CMSG_PROXY_PRIVILEGE_MIN && number <= CMSG_PROXY_PRIVILEGE_MAX)
        {
            *privilege = number;
        }
    }

    g_list_free_full (query_params, cmsg_proxy_free_url_parameter);
}


/**
 * Filter API by the privilege level.
 * API matches if the given privilege level is same or higher than the API privilege.
 *
 * @param service_info - CMSG service info of the API
 * @param privilege - Privilege level to filter the API
 * @param default_api_privilege - Default privilege of the API.
 *
 * @raturn TRUE if the API matches or FASLE otherwise.
 */
static gboolean
cmsg_proxy_index_privilege_filter (const cmsg_service_info *service_info,
                                   int privilege, int default_api_privilege)
{
    const cmsg_method_client_extensions *extensions;
    int api_privilege = default_api_privilege;

    /* If privilege filter is not set, allow all */
    if (privilege == CMSG_PROXY_PRIVILEGE_NOT_SET)
    {
        return TRUE;
    }

    /* Check the privilege of API if set in .proto file */
    if (service_info->cmsg_desc)
    {
        extensions = service_info->cmsg_desc->method_extensions[service_info->method_index];
        if (extensions && extensions->rpc_privilege > 0)
        {
            api_privilege = extensions->rpc_privilege;
        }
    }

    return (privilege >= api_privilege);
}


/**
 * GNodeTraverseFunc to add API tree leaf entry element to index.
 * @param node - API tree leaf node
 * @param data - Pointer to index_add_elem_data struct containing json array to append to
 *               and an optional search string to match against.
 */
static gboolean
cmsg_proxy_index_add_element (GNode *node, gpointer data)
{
    struct index_add_elem_data *elem_data = (struct index_add_elem_data *) data;
    json_t *api_array = elem_data->api_array;
    const char *search_pattern = elem_data->search_pattern;
    int privilege = elem_data->privilege;
    int readonly_privilege = elem_data->readonly_privilege;
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
        if (cmsg_proxy_index_privilege_filter (api_info->cmsg_http_delete, privilege,
                                               CMSG_PROXY_PRIVILEGE_MAX))
        {
            url_string = api_info->cmsg_http_delete->url_string;
            json_array_append_new (method_array, json_string ("DELETE"));
        }
    }
    if (api_info->cmsg_http_get)
    {
        if (cmsg_proxy_index_privilege_filter (api_info->cmsg_http_get, privilege,
                                               readonly_privilege))
        {
            url_string = api_info->cmsg_http_get->url_string;
            json_array_append_new (method_array, json_string ("GET"));
        }
    }
    if (api_info->cmsg_http_patch)
    {
        if (cmsg_proxy_index_privilege_filter (api_info->cmsg_http_patch, privilege,
                                               CMSG_PROXY_PRIVILEGE_MAX))
        {
            url_string = api_info->cmsg_http_patch->url_string;
            json_array_append_new (method_array, json_string ("PATCH"));
        }
    }
    if (api_info->cmsg_http_post)
    {
        if (cmsg_proxy_index_privilege_filter (api_info->cmsg_http_post, privilege,
                                               CMSG_PROXY_PRIVILEGE_MAX))
        {
            url_string = api_info->cmsg_http_post->url_string;
            json_array_append_new (method_array, json_string ("POST"));
        }
    }
    if (api_info->cmsg_http_put)
    {
        if (cmsg_proxy_index_privilege_filter (api_info->cmsg_http_put, privilege,
                                               CMSG_PROXY_PRIVILEGE_MAX))
        {
            url_string = api_info->cmsg_http_put->url_string;
            json_array_append_new (method_array, json_string ("PUT"));
        }
    }

    /* No url string found or doesn't match the search string, skip */
    if (!url_string || (search_pattern && !strstr (url_string, search_pattern)))
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
    int privilege = CMSG_PROXY_PRIVILEGE_NOT_SET;
    json_t *result = NULL;
    json_t *api_array = NULL;
    struct index_add_elem_data traverse_data;

    if (!output)
    {
        return HTTP_CODE_INTERNAL_SERVER_ERROR;
    }

    cmsg_proxy_index_parse_query_param (query_string, &search_pattern, &privilege);

    api_array = json_array ();

    traverse_data.search_pattern = search_pattern;
    traverse_data.api_array = api_array;
    traverse_data.privilege = privilege;
    traverse_data.readonly_privilege = cmsg_proxy_config_get_readonly_privilege ();

    if (!cmsg_proxy_tree_foreach_leaf (cmsg_proxy_index_add_element, &traverse_data))
    {
        free (search_pattern);
        json_decref (api_array);
        return HTTP_CODE_INTERNAL_SERVER_ERROR;
    }

    result = json_object ();

    json_object_set_new (result, "basepath", json_string (CMSG_PROXY_API_PREFIX));
    json_object_set (result, "paths", api_array);

    cmsg_proxy_json_t_to_output (result, JSON_ENCODE_ANY | JSON_COMPACT, output);

    free (search_pattern);
    json_decref (api_array);
    json_decref (result);

    return HTTP_CODE_OK;
}
