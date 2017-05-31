/*
 * CMSG proxy tree construction and functionality
 *
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */

#include <config.h>
#include "cmsg_proxy.h"
#include <glib.h>
#include <dlfcn.h>
#include <dirent.h>
#include <cmsg/cmsg_client.h>
#include "cmsg_proxy_tree.h"
#include "cmsg_proxy_mem.h"
#include "cmsg_proxy_counters.h"

#define CMSG_PROXY_LIB_PATH "/var/packages/network/lib"

/* Current CMSG API version string */
#define CMSG_API_VERSION_STR                "CMSG-API"

typedef cmsg_service_info *(*proxy_defs_array_get_func_ptr) ();
typedef int (*proxy_defs_array_size_func_ptr) ();

static GList *library_handles_list = NULL;
static GList *proxy_clients_list = NULL;
GNode *proxy_entries_tree = NULL;

/**
 * Allocate memory and store values for an embedded url parameter
 *
 * @param key - The name of the url parameter ie 'id' for /vlan/vlans/{id} or
 *              'vrf_name' in /dns/relay_cache?vrf_name=VRF1
 * @param value - The parameter ie '5' for /vlan/vlans/5/... or
 *                'VRF1' in /dns/relay_cache?vrf_name=VRF1
 * @return - allocated cmsg_url_parameter or NULL if allocation fails
 */
cmsg_url_parameter *
_cmsg_proxy_create_url_parameter (const char *key, const char *value)
{
    cmsg_url_parameter *new = calloc (1, sizeof (cmsg_url_parameter));

    if (new)
    {
        if (key[0] == '{')
        {
            /* strip the braces from the parameter name */
            new->key = CMSG_PROXY_STRNDUP (key + 1, strlen (key) - 2);
        }
        else
        {
            new->key = CMSG_PROXY_STRDUP (key);
        }
        new->value = value ? CMSG_PROXY_STRDUP (value) : NULL;
    }
    return new;
}

/**
 * Free a cmsg_url_parameter structure
 * @param ptr - the cms_url_parameter to be freed
 */
void
_cmsg_proxy_free_url_parameter (gpointer ptr)
{
    cmsg_url_parameter *p = (cmsg_url_parameter *) ptr;

    if (p)
    {
        CMSG_PROXY_FREE (p->key);
        CMSG_PROXY_FREE (p->value);
        CMSG_PROXY_FREE (p);
    }
}

/**
 * SET CMSG API info details to the proxy tree
 *
 * @param leaf_node - Add the CMSG service info to this leaf node
 * @param service_info - CMSG service info to be added
 */
static void
_cmsg_proxy_api_info_node_set (GNode *leaf_node, const cmsg_service_info *service_info)
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
    default:
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
        cmsg_proxy_api_ptr = CMSG_PROXY_CALLOC (1, sizeof (*cmsg_proxy_api_ptr));
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
            cmsg_proxy_api_ptr = CMSG_PROXY_CALLOC (1, sizeof (*cmsg_proxy_api_ptr));
            cmsg_api_info_node = g_node_insert_data (last_node, 0, cmsg_proxy_api_ptr);
        }
    }

    return cmsg_api_info_node;
}

/**
 * Free CMSG API info data allocated to the leaf nodes
 *
 * @param leaf_node - leaf node contains CMSG API info
 * @param data - Unused data to the callback
 *
 * @return - returns FALSE to ensure the traversal of the leaf nodes
 *           in the tree continues.
 */
static gboolean
_cmsg_proxy_api_info_free (GNode *leaf_node, gpointer data)
{
    cmsg_proxy_api_info *api_info = leaf_node->data;

    CMSG_PROXY_FREE (api_info);
    CMSG_PROXY_COUNTER_INC (cntr_service_info_unloaded);

    return FALSE;
}

/**
 * Free each node data allocated to the non leaf nodes
 *
 * @param node - node contains allocated string data
 * @param data - Unused data to the callback
 *
 * @return - returns FALSE to ensure the traversal of the non-leaf nodes
 *           in the tree continues.
 */
static gboolean
_cmsg_proxy_entry_data_free (GNode *node, gpointer data)
{
    char *str = node->data;

    CMSG_PROXY_FREE (str);

    return FALSE;
}

/**
 * Check whether a given string represents a URL parameter.
 * i.e. "{ xxx }"
 *
 * @param token - The string to check
 *
 * @return - true if it is a URL parameter, false otherwise
 */
static bool
_cmsg_proxy_token_is_url_param (const char *token)
{
    int token_len = (token ? strlen (token) : 0);

    if (token && token_len > 0 && token[0] == '{' && token[token_len - 1] == '}')
    {
        return true;
    }

    return false;
}

/**
 * Check existing tokens on the parent node we are adding to. If either
 * of the following are true then we cannot add this URL to the service info
 * tree as it is ambiguous which URL to use:
 *
 * - We are adding a URL parameter (i.e. '{ xxx }') to a parent_node that already has
 *   another child node that is not a leaf.
 * - We are adding a non URL paremeter to a parent_node that already has another
 *   child that is a URL parameter.
 *
 * @param parent_node - The parent node we are adding the new token string to
 * @param token - The token string that will be used for the new node
 *
 * @return - true if the token string will conflict with an existing node on the parent
 *           false otherwise
 */
static bool
_cmsg_proxy_service_info_conflicts (GNode *parent_node, const char *token)
{
    GNode *node = NULL;

    /* Check whether the node already exists in the tree. */
    node = g_node_first_child (parent_node);
    while (node)
    {
        /* API info node should be skipped */
        if (!G_NODE_IS_LEAF (node))
        {
            if (_cmsg_proxy_token_is_url_param (token) ||
                _cmsg_proxy_token_is_url_param (node->data))
            {
                /* CMSGPROX-117 todo: This should return true however we can't enforce
                 *                    this until some v0.1 URLs are removed.  */
                return false;
            }

            /* Once we have found at least one leaf node there is no need
             * to keep checking - this function ensures the tree is correct. */
            break;
        }
        node = g_node_next_sibling (node);
    }

    return false;
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
 *
 * @return - true if the service info was successfully added to the tree
 *           false otherwise
 */
static bool
_cmsg_proxy_service_info_add (const cmsg_service_info *service_info)
{
    char *tmp_url = NULL;
    char *next_entry = NULL;
    char *rest = NULL;
    GNode *parent_node = g_node_get_root (proxy_entries_tree);
    GNode *node = NULL;
    GNode *cmsg_api_info_node = NULL;
    bool found = false;

    tmp_url = CMSG_PROXY_STRDUP (service_info->url_string);

    for (next_entry = strtok_r (tmp_url, "/", &rest); next_entry;
         next_entry = strtok_r (NULL, "/", &rest))
    {
        found = false;

        /* Check whether the node already exists in the tree. */
        node = g_node_first_child (parent_node);

        while (node)
        {
            /* API info node should be skipped */
            if (!G_NODE_IS_LEAF (node) && strcmp (node->data, next_entry) == 0)
            {
                found = true;
                break;
            }
            node = g_node_next_sibling (node);
        }

        /* Add if it doesn't exist. Insert as the last child of parent_node. */
        if (!found)
        {
            if (_cmsg_proxy_service_info_conflicts (parent_node, next_entry))
            {
                syslog (LOG_ERR, "URL '%s' conflicts with a previously loaded URL",
                        service_info->url_string);
                CMSG_PROXY_FREE (tmp_url);
                return false;
            }
            node = g_node_insert_data (parent_node, -1, CMSG_PROXY_STRDUP (next_entry));
        }

        parent_node = node;
    }

    cmsg_api_info_node = _cmsg_proxy_api_info_node_new (parent_node);

    /* Fill the cmsg_service_info to the leaf node */
    _cmsg_proxy_api_info_node_set (cmsg_api_info_node, service_info);

    CMSG_PROXY_FREE (tmp_url);

    return true;
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
    const cmsg_service_info *service_info;

    for (i = 0; i < length; i++)
    {
        service_info = &array[i];

        if (_cmsg_proxy_service_info_add (service_info))
        {
            CMSG_PROXY_COUNTER_INC (cntr_service_info_loaded);
        }

#ifdef HAVE_COUNTERD
        /* Initialise service counter */
        cmsg_proxy_session_counter_init (service_info);
#endif
    }
}

/**
 * Deinitialise the cmsg proxy entry tree with the autogenerated array entries
 */
static void
_cmsg_proxy_service_info_deinit (void)
{
    GNode *root = g_node_get_root (proxy_entries_tree);

    /* Free cmsg service API info if proxy_entries_tree is not empty. */
    if (!G_NODE_IS_LEAF (root))
    {
        g_node_traverse (root, G_LEVEL_ORDER, G_TRAVERSE_LEAVES, -1,
                         _cmsg_proxy_api_info_free, NULL);

        /* Now free all nodes' data */
        g_node_traverse (root, G_POST_ORDER, G_TRAVERSE_NON_LEAVES, -1,
                         _cmsg_proxy_entry_data_free, NULL);
    }
    else
    {
        /* Free the root node's data. */
        _cmsg_proxy_entry_data_free (root, NULL);
    }

    g_node_destroy (proxy_entries_tree);
    proxy_entries_tree = NULL;
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
cmsg_client *
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
        syslog (LOG_ERR, "Failed to create client for service: %s",
                service_descriptor->name);
        CMSG_PROXY_COUNTER_INC (cntr_client_create_failure);
        return;
    }

    CMSG_PROXY_COUNTER_INC (cntr_client_created);

    proxy_clients_list = g_list_append (proxy_clients_list, (void *) client);
    return;
}

/**
 * Free the CMSG proxy clients created
 */
static void
_cmsg_proxy_client_free (gpointer data, gpointer user_data)
{
    cmsg_client *client = (cmsg_client *) data;

    cmsg_destroy_client_and_transport (client);

    CMSG_PROXY_COUNTER_INC (cntr_client_freed);
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
 * Lookup a cmsg_service_info entry from the proxy tree based on URL and
 * HTTP verb and update jason_object if any parameter found in the URL
 *
 * @param url - URL string to use for the lookup.
 * @param http_verb - HTTP verb to use for the lookup.
 * @param json_obj - jason object to update
 *
 * @return - Pointer to the cmsg_service_info entry if found, NULL otherwise.
 */
const cmsg_service_info *
_cmsg_proxy_find_service_from_url_and_verb (const char *url, cmsg_http_verb verb,
                                            GList **url_parameters)
{
    GNode *node;
    char *tmp_url;
    char *next_entry = NULL;
    char *rest = NULL;
    const char *key = NULL;
    GNode *parent_node;
    GNode *info_node;
    cmsg_url_parameter *param = NULL;

    tmp_url = CMSG_PROXY_STRDUP (url);
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

                /* if this URL segment is a parameter, store it to be parsed later */
                if (_cmsg_proxy_token_is_url_param (key))
                {

                    param = _cmsg_proxy_create_url_parameter (key, next_entry);
                    *url_parameters = g_list_prepend (*url_parameters, param);
                    parent_node = node;
                    break;
                }

            }
            node = g_node_next_sibling (node);
        }

        /* No match found. */
        if (node == NULL)
        {
            CMSG_PROXY_FREE (tmp_url);
            return NULL;
        }
    }

    CMSG_PROXY_FREE (tmp_url);

    info_node = g_node_first_child (parent_node);
    if ((info_node) != NULL && G_NODE_IS_LEAF (info_node))
    {
        return cmsg_proxy_service_info_get (info_node->data, verb);

    }

    return NULL;
}

/**
 * Callback to add CMSG clients.
 *
 * @param leaf_node - leaf node that contains cmsg_proxy_api_info
 * @param data - data passed in by the caller. This is NULL
 *
 * @return - returns FALSE to ensure the traversal of the leaf nodes
 *           in the tree continues.
 */
static gboolean
_cmsg_proxy_clients_add (GNode *leaf_node, gpointer data)
{
    cmsg_proxy_api_info *api_info;
    const cmsg_service_info *service_info;
    int action;

    api_info = leaf_node->data;

    for (action = CMSG_HTTP_GET; action <= CMSG_HTTP_PATCH; action++)
    {
        service_info = cmsg_proxy_service_info_get (api_info, action);
        if (service_info &&
            !_cmsg_proxy_find_client_by_service (service_info->service_descriptor))
        {
            _cmsg_proxy_create_client (service_info->service_descriptor);
        }
    }

    return FALSE;
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
 * Deinitialise the CMSG clients.
 */
static void
_cmsg_proxy_clients_deinit (void)
{
    g_list_foreach (proxy_clients_list, _cmsg_proxy_client_free, NULL);
    g_list_free (proxy_clients_list);
    proxy_clients_list = NULL;
}

/**
 * Helper function for _cmsg_proxy_library_handles_deinit().
 * Call dlclose in a way that compiles with g_list_free_full.
 */
static void
_cmsg_proxy_dlclose (gpointer data)
{
    dlclose (data);
}

/**
 * Close the loaded library handles.
 */
static void
_cmsg_proxy_library_handles_close (void)
{
    g_list_free_full (library_handles_list, _cmsg_proxy_dlclose);
    library_handles_list = NULL;
}

/**
 * Loads all of the *_proto_proxy_def.so libraries that exist in
 * CMSG_PROXY_LIB_PATH into the cmsg proxy library.
 */
static void
_cmsg_proxy_library_handles_load (void)
{
    DIR *d = NULL;
    struct dirent *dir = NULL;
    void *lib_handle = NULL;
    proxy_defs_array_get_func_ptr get_func_addr = NULL;
    proxy_defs_array_size_func_ptr size_func_addr = NULL;
    char *library_path = NULL;

    d = opendir (CMSG_PROXY_LIB_PATH);
    if (d == NULL)
    {
        syslog (LOG_ERR, "Directory '%s' could not be opened\n", CMSG_PROXY_LIB_PATH);
        return;
    }

    while ((dir = readdir (d)) != NULL)
    {
        /* Check that dir points to a file, not (sym)link or directory */
        if (dir->d_type == DT_REG && strstr (dir->d_name, "proto_proxy_def.so"))
        {
            if (asprintf (&library_path, "%s/%s", CMSG_PROXY_LIB_PATH, dir->d_name) < 0)
            {
                syslog (LOG_ERR, "Unable able to load library %s", dir->d_name);
                continue;
            }

            lib_handle = dlopen (library_path, RTLD_NOW | RTLD_GLOBAL);
            if (lib_handle)
            {
                get_func_addr = dlsym (lib_handle, "cmsg_proxy_array_get");
                size_func_addr = dlsym (lib_handle, "cmsg_proxy_array_size");

                if (get_func_addr && size_func_addr)
                {
                    _cmsg_proxy_service_info_init (get_func_addr (), size_func_addr ());

                    /* We need to leave the library loaded in the process address space so
                     * that the data can be accessed. Store a pointer to the library handle
                     * so that it can be closed at deinit. */
                    library_handles_list = g_list_prepend (library_handles_list,
                                                           lib_handle);
                }
                else
                {
                    dlclose (lib_handle);
                }
            }
            CMSG_PROXY_FREE (library_path);
        }
    }

    closedir (d);
}

/**
 * Initialise the cmsg proxy tree module. Specifically:
 *
 * - Create the tree used to hold the proxy mapping information
 * - Load each *_proxy_def library on the device into this tree
 * - Create a client for each service stored in the tree
 */
void
cmsg_proxy_tree_init (void)
{
    /* Create GNode proxy entries tree. */
    proxy_entries_tree = g_node_new (CMSG_PROXY_STRDUP (CMSG_API_VERSION_STR));

    _cmsg_proxy_library_handles_load ();
    _cmsg_proxy_clients_init ();
}

/**
 * Deinitialise the cmsg proxy tree module. Simply cleans up
 * and frees any dynamic memory allocated for this module.
 */
void
cmsg_proxy_tree_deinit (void)
{
    _cmsg_proxy_service_info_deinit ();
    _cmsg_proxy_clients_deinit ();
    _cmsg_proxy_library_handles_close ();
}
