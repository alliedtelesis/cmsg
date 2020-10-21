/**
 * cmsg_supported_service.h
 *
 * Copyright 2020, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_SUPPORTED_SERVICE_H_
#define __CMSG_SUPPORTED_SERVICE_H_

#include <protobuf-c/protobuf-c.h>
#include <stdbool.h>
#include "cmsg_client.h"

typedef struct
{
    const char *filename;
    const char *msg;
    int return_code;
} service_support_parameters;

int
cmsg_api_invoke_with_service_check (cmsg_client *client,
                                    const ProtobufCServiceDescriptor *service_desc,
                                    int method_index, const ProtobufCMessage *send_msg,
                                    ProtobufCMessage **recv_msg,
                                    const service_support_parameters *check_params);

#endif /* __CMSG_SUPPORTED_SERVICE_H_ */
