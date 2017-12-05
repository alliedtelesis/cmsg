/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#ifndef __CMSG_PROXY_H_
#define __CMSG_PROXY_H_

#include <stdbool.h>
#include <cmsg/cmsg.h>

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

typedef struct _cmsg_proxy_api_request_info
{
    const char *api_request_ip_address;
    const char *api_request_username;
} cmsg_proxy_api_request_info;


typedef int (*cmsg_api_func_ptr) ();
typedef bool (*pre_api_http_check_callback) (cmsg_http_verb http_verb, char **message);

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

void cmsg_proxy_init (void);
void cmsg_proxy_deinit (void);
bool cmsg_proxy (const char *url, const char *query_string, cmsg_http_verb http_verb,
                 const char *input_json, const cmsg_proxy_api_request_info *web_api_info,
                 char **output_json, const char **mime_type, int *http_status);

void cmsg_proxy_set_pre_api_http_check_callback (pre_api_http_check_callback cb);

void cmsg_proxy_passthrough_init (const char *library_path);
void cmsg_proxy_passthrough_deinit (void);
bool cmsg_proxy_passthrough (const char *url, const char *query_string,
                             cmsg_http_verb http_verb, const char *input_json,
                             const cmsg_proxy_api_request_info *web_api_info,
                             char **output_json, const char **response_mime,
                             int *http_status);

#endif /* __CMSG_PROXY_H_ */
