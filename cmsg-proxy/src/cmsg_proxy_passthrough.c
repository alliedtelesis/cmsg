/*
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 *
 * The CMSG proxy passthrough is a library that can be used by a web server to proxy HTTP
 * directly to a CMSG server without any conversion of URL path to API call or JSON to
 * protobuf taking place.
 */

#include <config.h>
#include "cmsg_proxy.h"
#include <glib.h>
#include <string.h>
#include <dlfcn.h>
#include <cmsg/cmsg_client.h>
#include "cmsg_proxy_mem.h"
#include "cmsg_proxy_counters.h"
#include "cmsg_proxy_tree.h"
#include "common.pb-c.h"

typedef cmsg_service_info *(*proxy_defs_array_get_func_ptr) ();
typedef int (*proxy_defs_array_size_func_ptr) ();

static void *lib_handle = NULL;

static cmsg_api_func_ptr api_ptr = NULL;
static const cmsg_service_info *api_service_info = NULL;
static const ProtobufCServiceDescriptor *service_descriptor = NULL;
static cmsg_client *client = NULL;

/**
 * Return string for HTTP verb value
 */
static char *
_cmsg_proxy_passthrough_verb_to_string (cmsg_http_verb http_verb)
{
    switch (http_verb)
    {
    case CMSG_HTTP_GET:
        return "GET";
    case CMSG_HTTP_PUT:
        return "PUT";
    case CMSG_HTTP_POST:
        return "POST";
    case CMSG_HTTP_DELETE:
        return "DELETE";
    case CMSG_HTTP_PATCH:
        return "PATCH";
    }

    return "UNKNOWN";
}

/**
 * Setup available api_ptr and service_descriptor
 */
static bool
_load_library_info (proxy_defs_array_get_func_ptr get_func_addr,
                    proxy_defs_array_size_func_ptr size_func_addr)
{
    cmsg_service_info *array = NULL;
    const cmsg_service_info *service_info = NULL;

    if (size_func_addr () != 1)
    {
        syslog (LOG_ERR, "Invalid size of function addresses");
        return false;
    }

    array = get_func_addr ();
    service_info = &array[0];

    if (strcmp (service_info->input_msg_descriptor->name, "passthrough_request") != 0)
    {
        syslog (LOG_ERR, "Unexpected input msg descriptor");
        return false;
    }

    if (strcmp (service_info->output_msg_descriptor->name, "passthrough_response") != 0)
    {
        syslog (LOG_ERR, "Unexpected output msg descriptor");
        return false;
    }

    api_service_info = service_info;
    api_ptr = service_info->api_ptr;
    service_descriptor = service_info->service_descriptor;

#ifdef HAVE_COUNTERD
    /* Initialise service counter */
    cmsg_proxy_session_counter_init (api_service_info);
#endif

    return true;
}

/**
 * Loads library by given full path
 */
static bool
_cmsg_proxy_passthrough_library_handle_load (const char *library_path)
{
    proxy_defs_array_get_func_ptr get_func_addr = NULL;
    proxy_defs_array_size_func_ptr size_func_addr = NULL;

    lib_handle = dlopen (library_path, RTLD_NOW | RTLD_GLOBAL);
    if (!lib_handle)
    {
        return false;
    }

    CMSG_PROXY_COUNTER_INC (cntr_service_info_loaded);

    get_func_addr = dlsym (lib_handle, "cmsg_proxy_array_get");
    size_func_addr = dlsym (lib_handle, "cmsg_proxy_array_size");

    if (!get_func_addr || !size_func_addr)
    {
        dlclose (lib_handle);
        lib_handle = NULL;
        return false;
    }

    return _load_library_info (get_func_addr, size_func_addr);
}

/**
 * Initialise the cmsg proxy library
 */
void
cmsg_proxy_passthrough_init (const char *library_path)
{
    cmsg_proxy_passthrough_deinit ();

#ifdef HAVE_COUNTERD
    /* Initialise counters */
    cmsg_proxy_counter_init ();
#endif /* HAVE_COUNTERD */

    if (!_cmsg_proxy_passthrough_library_handle_load (library_path))
    {
        syslog (LOG_ERR, "Unable able to load library %s", library_path);
        return;
    }

    client = cmsg_create_client_unix (service_descriptor);
    if (!client)
    {
        syslog (LOG_ERR, "Failed to initialise the cmsg proxy passthrough");
        CMSG_PROXY_COUNTER_INC (cntr_client_create_failure);
        return;
    }

    CMSG_PROXY_COUNTER_INC (cntr_client_created);
}

/**
 * Deinitialize the cmsg proxy library
 */
void
cmsg_proxy_passthrough_deinit (void)
{
    if (lib_handle)
    {
        dlclose (lib_handle);
        lib_handle = NULL;

        CMSG_PROXY_COUNTER_INC (cntr_service_info_unloaded);
    }

    if (client)
    {
        cmsg_destroy_client_and_transport (client);
        client = NULL;

        CMSG_PROXY_COUNTER_INC (cntr_client_freed);
    }

    api_service_info = NULL;
    api_ptr = NULL;
    service_descriptor = NULL;

#ifdef HAVE_COUNTERD
    /* Cleanup counters */
    cmsg_proxy_counter_deinit ();
#endif /* HAVE_COUNTERD */
}

/**
 * Passthrough an HTTP request directly to a specific daemon to handle.
 *
 * @param url - URL the HTTP request is for.
 * @param query_string - Ignored.
 * @param http_verb - The HTTP verb sent with the HTTP request.
 * @param input_json - A string representing the JSON data sent with the HTTP request.
 * @param output_json - A pointer to a string that will store the output JSON data to.
 *                      be sent with the HTTP response. This pointer may be NULL if the
 *                      rpc does not send any response data and the pointer must be
 *                      freed by the caller (if it is non NULL).
 * @param http_status - A pointer to an integer that will store the HTTP status code to
 *                      be sent with the HTTP response.
 *
 * @return - true if the passthrough was successful.
 *           false if the passthrough failed (i.e. the underlying CMSG API call failed).
 */
bool
cmsg_proxy_passthrough (const char *url, const char *query_string,
                        cmsg_http_verb http_verb, const char *input_json,
                        char **output_json, int *http_status)
{
    passthrough_request send_msg = PASSTHROUGH_REQUEST_INIT;
    passthrough_response *recv_msg = NULL;
    int ret;

    CMSG_PROXY_SESSION_COUNTER_INC (api_service_info, cntr_api_calls);

    CMSG_SET_FIELD_PTR (&send_msg, path, (char *) url);
    CMSG_SET_FIELD_PTR (&send_msg, method,
                        _cmsg_proxy_passthrough_verb_to_string (http_verb));
    CMSG_SET_FIELD_PTR (&send_msg, request_body, (char *) input_json);

    ret = api_ptr (client, &send_msg, &recv_msg);
    if (ret != CMSG_RET_OK)
    {
        syslog (LOG_ERR, "Error calling passthrough API");
        CMSG_PROXY_SESSION_COUNTER_INC (api_service_info, cntr_error_api_failure);
        return false;
    }

    if (recv_msg->response_body)
    {
        *output_json = strdup (recv_msg->response_body);
    }
    *http_status = recv_msg->status_code;

    CMSG_FREE_RECV_MSG (recv_msg);
    return true;
}
