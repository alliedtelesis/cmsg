/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#ifndef __CMSG_PROXY_H_
#define __CMSG_PROXY_H_

#include <stdbool.h>
#include <cmsg/cmsg.h>

typedef int (*cmsg_api_func_ptr) ();

typedef enum _cmsg_http_verb
{
    CMSG_HTTP_GET = 1,
    CMSG_HTTP_PUT = 2,
    CMSG_HTTP_POST = 3,
    CMSG_HTTP_DELETE = 4,
    CMSG_HTTP_PATCH = 5,
} cmsg_http_verb;

typedef struct _cmsg_service_info
{
    const ProtobufCServiceDescriptor *service_descriptor;
    const ProtobufCMessageDescriptor *input_msg_descriptor;
    const ProtobufCMessageDescriptor *output_msg_descriptor;
    cmsg_api_func_ptr api_ptr;
    const char *url_string;
    cmsg_http_verb http_verb;
} cmsg_service_info;

bool cmsg_proxy (const char *url, cmsg_http_verb http_verb, const char *input_json,
                 char **output_json, int *http_status);

#endif /* __CMSG_PROXY_H_ */
