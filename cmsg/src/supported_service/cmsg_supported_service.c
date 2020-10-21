/**
 * cmsg_supported_service.c
 *
 * Common code for supported_service extension
 *
 * Copyright 2020, Allied Telesis Labs New Zealand, Ltd
 */

#include "cmsg_private.h"
#include "cmsg_supported_service.h"
#include "cmsg_ant_result.h"
#include "cmsg_client.h"
#include "cmsg_error.h"

/**
 * Check if service is available and if not, generate response message.
 * Requires recv_msg to either be ant_result or have an ant_result field called _error_info)
 * @param check_params Parameters to use for service check.
 * @param output_desc descriptor for message to be generated
 * @param recv_msg array to hold response. (Response is only set in first entry)
 * @returns true if service is available/supported, else false.
 */
static bool
cmsg_supported_service_check (const service_support_parameters *check_params,
                              const ProtobufCMessageDescriptor *output_desc,
                              ProtobufCMessage **recv_msg)
{
    /* Service support check */
    if (access (check_params->filename, F_OK) == -1)
    {
        recv_msg[0] = cmsg_create_ant_response (check_params->msg,
                                                check_params->return_code, output_desc);

        return false;
    }
    return true;
}

/**
 * Check if service is available before invoking the API. If it is not available, a
 * response is generated on the calling side. (Requires recv_msg to either be ant_result or
 * have an ant_result field called _error_info). If it is available, the API is invoked.
 * The call to this function is intended to be auto-generated, so shouldn't be manually
 * called.
 * @param client cmsg client for API call
 * @param service_desc Descriptor for the service being called
 * @param method_index index of method being called
 * @param send_msg message to be sent to the server
 * @param recv_msg pointer to hold message responses
 * @param check_params Parameters to use for service check.
 * @returns API return code (CMSG_RET_OK if service is not available)
 */
int
cmsg_api_invoke_with_service_check (cmsg_client *client,
                                    const ProtobufCServiceDescriptor *service_desc,
                                    int method_index,
                                    const ProtobufCMessage *send_msg,
                                    ProtobufCMessage **recv_msg,
                                    const service_support_parameters *check_params)
{
    if (!cmsg_supported_service_check (check_params,
                                       service_desc->methods[method_index].output,
                                       recv_msg))
    {
        return CMSG_RET_OK;
    }

    return cmsg_api_invoke (client, service_desc, method_index, send_msg, recv_msg);
}
