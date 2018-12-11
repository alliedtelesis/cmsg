/*
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */

#include <protobuf2json.h>
#include "cmsg_proxy.h"
#include "cmsg_proxy_mem.h"
#include "cmsg_proxy_private.h"
#include "cmsg_proxy_counters.h"
#include "cmsg_proxy_http_streaming.h"

static const char *cmsg_proxy_output_mime_text_plain = "text/plain";

/**
 * Returns true if a message has a field named "_body" (CMSG_PROXY_SPECIAL_FIELD_BODY).
 * This implies that the contents of the field should be returned as the response.
 * @param msg_descriptor descriptor of the message to be checked
 * @returns true if msg_descriptor has a field named "_body", else false.
 */
static bool
cmsg_proxy_msg_has_body_override (const ProtobufCMessageDescriptor *msg_descriptor)
{
    if (protobuf_c_message_descriptor_get_field_by_name (msg_descriptor,
                                                         CMSG_PROXY_SPECIAL_FIELD_BODY))
    {
        return true;
    }

    return false;
}

/**
 * Generate a plaintext response based on the contents of the "_body"
 * (CMSG_PROXY_SPECIAL_FIELD_BODY) field.
 *
 * @param output_proto_message - The message returned from calling the CMSG API
 * @param output - CMSG proxy response
 */
static bool
cmsg_proxy_generate_plaintext_response (ProtobufCMessage *output_proto_message,
                                        cmsg_proxy_output *output)
{
    const ProtobufCFieldDescriptor *field_descriptor = NULL;
    const char **field_value = NULL;

    field_descriptor =
        protobuf_c_message_descriptor_get_field_by_name (output_proto_message->descriptor,
                                                         "_body");

    output->response_length = 0;

    if (field_descriptor && (field_descriptor->type == PROTOBUF_C_TYPE_STRING))
    {
        field_value =
            (const char **) ((char *) output_proto_message + field_descriptor->offset);

        if (field_value && *field_value)
        {
            /* This is allocated with strdup rather than CMSG_PROXY_STRDUP as it is freed with
             * free by the caller in cmsgProxyHandler. response_body is also populated using
             * json_dumps, which uses standard allocation.
             */
            output->response_body = strdup (*field_value);
            output->mime_type = cmsg_proxy_output_mime_text_plain;
            if (output->response_body)
            {
                output->response_length = strlen (output->response_body);
            }
        }

        return true;
    }

    return false;
}

/**
 * Helper function which takes the ProtobufCMessage received from calling
 * the CMSG API function, finds the error information set in the response,
 * and sets the HTTP response status based on the code returned from the
 * CMSG API.
 *
 * If the CMSG API has returned ANT_CODE_OK and the request is with an HTTP_GET
 * then the error information field is unset from the protobuf message and
 * hence will not be returned in the JSON sent back to the user.
 *
 * @param http_status - Pointer to the http_status integer that should be set
 * @param http_verb - The HTTP verb sent with the HTTP request.
 * @param msg - Pointer to the ProtobufCMessage received from the CMSG API call
 *
 * @returns 'true' if _error_info is updated with error message otherwise 'false'
 */
static bool
cmsg_proxy_set_http_status (int *http_status, cmsg_http_verb http_verb,
                            ProtobufCMessage **msg)
{
    const ProtobufCFieldDescriptor *field_desc = NULL;
    ProtobufCMessage **error_message_ptr = NULL;
    ant_result *error_message = NULL;
    bool ret = false;

    if (*msg == NULL)
    {
        *http_status = HTTP_CODE_INTERNAL_SERVER_ERROR;
        return false;
    }

    field_desc = protobuf_c_message_descriptor_get_field_by_name ((*msg)->descriptor,
                                                                  "_error_info");
    if (field_desc)
    {
        error_message_ptr = (ProtobufCMessage **) (((char *) *msg) + field_desc->offset);
    }
    else if (strcmp ((*msg)->descriptor->name, "ant_result") == 0)
    {
        error_message_ptr = msg;
    }
    else
    {
        *http_status = HTTP_CODE_INTERNAL_SERVER_ERROR;
        return false;
    }

    error_message = (ant_result *) (*error_message_ptr);
    if (error_message && CMSG_IS_FIELD_PRESENT (error_message, code))
    {
        *http_status = cmsg_proxy_ant_code_to_http_code (error_message->code);
        if (error_message->code == ANT_CODE_OK && http_verb == CMSG_HTTP_GET)
        {
            /* Unset the error info message from the protobuf message */
            CMSG_FREE_RECV_MSG (error_message);
            *error_message_ptr = NULL;
        }
        ret = true;
    }
    else
    {
        *http_status = HTTP_CODE_INTERNAL_SERVER_ERROR;
    }

    return ret;
}

/**
 * Generate the body of the response that should be returned to the web API caller.
 *
 * @param output_proto_message - The message returned from calling the CMSG API
 * @param output - CMSG proxy response
 */
bool
cmsg_proxy_generate_response_body (ProtobufCMessage *output_proto_message,
                                   cmsg_proxy_output *output)
{
    json_t *converted_json_object = NULL;
    const char *key = NULL;
    json_t *value = NULL;
    json_t *_error_info = NULL;

    // Handle special response types (if the response was successful)
    if (output->http_status == HTTP_CODE_OK)
    {
        if (cmsg_proxy_msg_has_body_override (output_proto_message->descriptor))
        {
            // If the message provides a '_body' override, simply return that.
            return cmsg_proxy_generate_plaintext_response (output_proto_message, output);
        }
    }

    if (!cmsg_proxy_protobuf2json_object (output_proto_message, &converted_json_object))
    {
        return false;
    }

    /* If the API simply returns an 'ant_result' message then no further
     * processing is required, simply return it. */
    if (strcmp (output_proto_message->descriptor->name, "ant_result") == 0)
    {
        cmsg_proxy_strip_details_from_ant_result (converted_json_object);
        cmsg_proxy_json_t_to_output (converted_json_object, JSON_COMPACT, output);
        json_decref (converted_json_object);

        return true;
    }

    /* If the status is not HTTP_CODE_OK then we need to simply return the
     * '_error_info' subfield of the message to the API caller */
    if (output->http_status != HTTP_CODE_OK)
    {
        json_object_foreach (converted_json_object, key, value)
        {
            if ((strcmp (key, "_error_info") == 0))
            {
                cmsg_proxy_strip_details_from_ant_result (value);
                cmsg_proxy_json_t_to_output (value, JSON_ENCODE_ANY | JSON_COMPACT, output);
                json_decref (converted_json_object);
                return true;
            }
        }

        /* Sanity check that '_error_info' is actually in the message */
        json_decref (converted_json_object);
        return false;
    }

    /* If there are only two fields in the message (and the http status is HTTP_CODE_OK)
     * we simply return the field that isn't '_error_info'. */
    if (output_proto_message->descriptor->n_fields <= 2)
    {
        json_object_foreach (converted_json_object, key, value)
        {
            if (strcmp (key, "_error_info") != 0)
            {
                cmsg_proxy_json_t_to_output (value, JSON_ENCODE_ANY | JSON_COMPACT, output);
                json_decref (converted_json_object);
                return true;
            }
        }

        /* Sanity check that there is actually a field other than
         * '_error_info' in the message */
        json_decref (converted_json_object);
        return false;
    }

    _error_info = json_object_get (converted_json_object, "_error_info");
    if (_error_info)
    {
        cmsg_proxy_strip_details_from_ant_result (converted_json_object);
    }

    /* If there are more than 2 fields in the message descriptor
     * (and the http status is HTTP_CODE_OK) then simply return the
     * entire message as a JSON string */
    cmsg_proxy_json_t_to_output (converted_json_object, JSON_COMPACT, output);
    json_decref (converted_json_object);
    return true;
}

/**
 * Perform the output path processing for the cmsg proxy functionality.
 * This function takes the CMSG information returned from the proxied CMSG
 * API call (output message and API function result) and transforms this
 * into the required HTTP information (output JSON message and HTTP result).
 *
 * @param output_proto_message - Pointer to the output message from the CMSG API call.
 * @param output - Pointer to a cmsg_proxy_output structure storing the output information.
 * @param processing_info - Pointer to a cmsg_proxy_processing_info structure to store information
 *                          deduced in the input path that is required in the output path.
 */
void
cmsg_proxy_output_process (ProtobufCMessage *output_proto_message,
                           cmsg_proxy_output *output,
                           cmsg_proxy_processing_info *processing_info)
{
    if (processing_info->cmsg_api_result != ANT_CODE_OK)
    {
        /* Something went wrong calling the CMSG api */
        cmsg_proxy_generate_ant_result_error (processing_info->cmsg_api_result,
                                              NULL, output);
        CMSG_PROXY_SESSION_COUNTER_INC (processing_info->service_info,
                                        cntr_error_api_failure);
        if (output->stream_response)
        {
            cmsg_proxy_streaming_delete_conn_by_id (processing_info->streaming_id);
            output->stream_response = false;
        }
        return;
    }

    if (!cmsg_proxy_set_http_status (&output->http_status, processing_info->http_verb,
                                     &output_proto_message))
    {
        syslog (LOG_ERR, "_error_info is not set for %s",
                processing_info->service_info->url_string);
        CMSG_PROXY_SESSION_COUNTER_INC (processing_info->service_info,
                                        cntr_error_missing_error_info);
    }

    if (output->stream_response)
    {
        if (output->http_status == HTTP_CODE_OK)
        {
            /* We're streaming the response so it will be sent back asynchronously */
            CMSG_FREE_RECV_MSG (output_proto_message);
            return;
        }
        else
        {
            /* The IMPL has rejected/failed the request to stream
             * the response. */
            output->stream_response = false;
            cmsg_proxy_streaming_delete_conn_by_id (processing_info->streaming_id);
        }
    }

    if (output_proto_message)
    {
        if (!cmsg_proxy_generate_response_body (output_proto_message, output))
        {
            /* This should not occur (the ProtobufCMessage structure returned
             * by the CMSG api should always be well formed) but check for it */
            output->http_status = HTTP_CODE_INTERNAL_SERVER_ERROR;
            CMSG_PROXY_SESSION_COUNTER_INC (processing_info->service_info,
                                            cntr_error_protobuf_to_json);
        }
        CMSG_FREE_RECV_MSG (output_proto_message);
    }
}
