/**
 * cmsg_ant_result.c
 *
 * Common code to handle ant_result
 *
 * Copyright 2020, Allied Telesis Labs New Zealand, Ltd
 */

#include "cmsg_private.h"
#include "cmsg_ant_result.h"
#include "cmsg_error.h"
#include "ant_result_types_auto.h"

/**
 * Generate an ant_result response message with the CMSG allocator. Either as a top-level
 * response or as the _error_info field in the parent message
 * @param message message to set in the ant_result
 * @param code code to set in the ant_result
 * @param output_desc descriptor for the message to be created
 * @return the allocated message (or NULL if unusable message type)
 */
ProtobufCMessage *
cmsg_create_ant_response (const char *message, ant_code code,
                          const ProtobufCMessageDescriptor *output_desc)
{
    ProtobufCMessage *response = NULL;
    ant_result *ant_result_msg = CMSG_MALLOC (sizeof (ant_result));
    ant_result_init (ant_result_msg);

    CMSG_SET_FIELD_VALUE (ant_result_msg, code, code);
    if (message)
    {
        CMSG_SET_FIELD_PTR (ant_result_msg, message, CMSG_STRDUP (message));
    }

    if (strcmp (output_desc->name, "ant_result") == 0)
    {
        response = (ProtobufCMessage *) ant_result_msg;
    }
    else
    {
        const ProtobufCFieldDescriptor *error_info_desc = NULL;
        ProtobufCMessage **error_info_ptr = NULL;

        response = CMSG_MALLOC (output_desc->sizeof_message);
        protobuf_c_message_init (output_desc, response);
        error_info_desc = protobuf_c_message_descriptor_get_field_by_name (output_desc,
                                                                           "_error_info");
        if (error_info_desc)
        {
            error_info_ptr =
                (ProtobufCMessage **) (((char *) response) + error_info_desc->offset);
            *error_info_ptr = (ProtobufCMessage *) ant_result_msg;
        }
        else
        {
            CMSG_LOG_GEN_ERROR ("Can't generate ANT response for message %s",
                                output_desc->name);
            CMSG_FREE_RECV_MSG (ant_result_msg);
            CMSG_FREE (response);
            response = NULL;
        }
    }

    return response;
}
