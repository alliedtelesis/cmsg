/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 *
 * The CMSG proxy is a library that can be used by a web server to proxy HTTP
 * requests into CMSG service APIs.
 *
 * The required information is auto-generated by protoc-cmsg using the HttpRules
 * defined for each rpc in the CMSG .proto files. The user of the library only
 * needs to call two functions:
 *
 * - cmsg_proxy_init() to initialise the library
 * - cmsg_proxy() for each HTTP request the user wishes to proxy through to the
 *   CMSG service APIs
 */

#include <config.h>
#include "cmsg_proxy.h"
#include <glib.h>
#include <string.h>
#include <protobuf2json.h>
#include <cmsg/cmsg_client.h>
#include "cmsg_proxy_mem.h"
#include "cmsg_proxy_counters.h"
#include "cmsg_proxy_tree.h"
#include "cmsg_proxy_http_streaming.h"
#include "cmsg_proxy_private.h"
#include "cmsg_proxy_index.h"
#include "cmsg_proxy_input.h"
#include "ant_result.pb-c.h"

/* work out the number of elements in an array */
#define ARRAY_ELEMENTS(arr) (sizeof((arr)) / sizeof((arr)[0]))

/* compile time check that an array has the expected number of elements */
#define ARRAY_SIZE_COMPILE_CHECK(array,exp_num) G_STATIC_ASSERT(ARRAY_ELEMENTS((array)) == (exp_num))


/**
 * Map the ANT code returned from the CMSG API call to the
 * HTTP response code sent in the HTTP header.
 */
int ant_code_to_http_code_array[] = {
    HTTP_CODE_OK,                       /* ANT_CODE_OK */
    HTTP_CODE_REQUEST_TIMEOUT,          /* ANT_CODE_CANCELLED */
    HTTP_CODE_INTERNAL_SERVER_ERROR,    /* ANT_CODE_UNKNOWN */
    HTTP_CODE_BAD_REQUEST,              /* ANT_CODE_INVALID_ARGUMENT */
    HTTP_CODE_REQUEST_TIMEOUT,          /* ANT_CODE_DEADLINE_EXCEEDED */
    HTTP_CODE_NOT_FOUND,                /* ANT_CODE_NOT_FOUND */
    HTTP_CODE_CONFLICT,                 /* ANT_CODE_ALREADY_EXISTS */
    HTTP_CODE_FORBIDDEN,                /* ANT_CODE_PERMISSION_DENIED */
    HTTP_CODE_FORBIDDEN,                /* ANT_CODE_RESOURCE_EHAUSTED */
    HTTP_CODE_BAD_REQUEST,              /* ANT_CODE_FAILED_PRECONDITION */
    HTTP_CODE_CONFLICT,                 /* ANT_CODE_ABORTED */
    HTTP_CODE_BAD_REQUEST,              /* ANT_CODE_OUT_OF_RANGE */
    HTTP_CODE_NOT_IMPLEMENTED,          /* ANT_CODE_UNIMPLEMENTED */
    HTTP_CODE_INTERNAL_SERVER_ERROR,    /* ANT_CODE_INTERNAL */
    HTTP_CODE_SERVICE_UNAVAILABLE,      /* ANT_CODE_UNAVAILABLE */
    HTTP_CODE_INTERNAL_SERVER_ERROR,    /* ANT_CODE_DATALOSS */
    HTTP_CODE_UNAUTHORIZED,             /* ANT_CODE_UNAUTHENTICATED */
    HTTP_CODE_OK,                       /* ANT_CODE_BATCH_PARTIAL_FAIL */
};

ARRAY_SIZE_COMPILE_CHECK (ant_code_to_http_code_array, ANT_CODE_MAX);

pre_api_http_check_callback pre_api_check_callback = NULL;

static const char *cmsg_content_disposition_key = "Content-Disposition";
static const char *cmsg_content_encoding_key = "Content-Transfer-Encoding";
static const char *cmsg_mime_text_plain = "text/plain";
static const char *cmsg_mime_octet_stream = "application/octet-stream";
static const char *cmsg_mime_application_json = "application/json";
static const char *cmsg_binary_encoding = "binary";
static const char *cmsg_filename_header_format = "attachment; filename=\"%s\"";

/**
 * Return the HTTP code that matches a particular ANT code. If the passed in value is
 * out of range, HTTP_CODE_INTERNAL_SERVER_ERROR is returned.
 * @param ant_code ant_code to convert to HTTP return code
 * @returns HTTP code that corresponds to ant_code, or HTTP_CODE_INTERNAL_SERVER_ERROR
 */
static int
_cmsg_proxy_ant_code_to_http_code (int ant_code)
{
    if (ant_code < 0 || ant_code >= ANT_CODE_MAX)
    {
        return HTTP_CODE_INTERNAL_SERVER_ERROR;
    }

    return ant_code_to_http_code_array[ant_code];
}

/**
 * Returns true if a message has a field named "_file" (CMSG_PROXY_SPECIAL_FIELD_FILE).
 * This implies that on input, the field should be populated with the raw data of the
 * request, and on output, the contents of the field should be returned as raw data.
 * @param msg_descriptor descriptor of the message to be checked
 * @returns true if msg_descriptor has the field, else false.
 */
bool
_cmsg_proxy_msg_has_file (const ProtobufCMessageDescriptor *msg_descriptor)
{
    if (protobuf_c_message_descriptor_get_field_by_name (msg_descriptor,
                                                         CMSG_PROXY_SPECIAL_FIELD_FILE))
    {
        return true;
    }

    return false;
}

/**
 * Returns true if a message has a field named "_body" (CMSG_PROXY_SPECIAL_FIELD_BODY).
 * This implies that the contents of the field should be returned as the response.
 * @param msg_descriptor descriptor of the message to be checked
 * @returns true if msg_descriptor has a field named "_body", else false.
 */
static bool
_cmsg_proxy_msg_has_body_override (const ProtobufCMessageDescriptor *msg_descriptor)
{
    if (protobuf_c_message_descriptor_get_field_by_name (msg_descriptor,
                                                         CMSG_PROXY_SPECIAL_FIELD_BODY))
    {
        return true;
    }

    return false;
}

/**
 * The function below relies on LLONG_MAX to be greater than UINT32_MAX to allow json inputs
 * with negative values to be translated without casting.  This will allow json2protobuf to
 * correctly reject the value for being negative.
 */
G_STATIC_ASSERT (LLONG_MAX > UINT32_MAX);

/**
 * Make sure that jansson has been compiled with a json_int_t as a long long rather than
 * a long.
 */
G_STATIC_ASSERT (sizeof (json_int_t) == sizeof (long long));

/**
 * Convert a single JSON value (i.e. not a JSON object or array) into
 * a JSON object using the input protobuf-c field name as the key.
 *
 * @param field_descriptor - Protobuf-c field descriptor to get the key name from
 * @param value - The value to put into the JSON object
 *
 * @returns - Pointer to the converted JSON object or NULL if the
 *            conversion fails or is not supported.
 */
json_t *
_cmsg_proxy_json_value_to_object (const ProtobufCFieldDescriptor *field_descriptor,
                                  const char *value)
{
    char *fmt = NULL;
    char *endptr = NULL;
    json_int_t llvalue;
    json_t *new_object = NULL;

    switch (field_descriptor->type)
    {
    case PROTOBUF_C_TYPE_INT32:
    case PROTOBUF_C_TYPE_SINT32:
    case PROTOBUF_C_TYPE_SFIXED32:
    case PROTOBUF_C_TYPE_UINT32:
    case PROTOBUF_C_TYPE_FIXED32:
        /* Treat all values as signed, as strtoul will cast a negative input to a positive
         * value, which is not what we want, as we want to catch if a negative value is set.
         * This will work for all 32 bit values as sizeof (long long) > 32 bits. (confirmed
         * by static asserts above)
         */
        llvalue = strtoll (value, &endptr, 0);
        if (endptr && *endptr == '\0')
        {
            fmt = field_descriptor->label == PROTOBUF_C_LABEL_REPEATED ? "{s[I]}" : "{sI}";
            new_object = json_pack (fmt, field_descriptor->name, llvalue);
            break;
        }
        /* fall through (storing as string) */
    case PROTOBUF_C_TYPE_UINT64:
    case PROTOBUF_C_TYPE_INT64:
    case PROTOBUF_C_TYPE_SINT64:
    case PROTOBUF_C_TYPE_SFIXED64:
    case PROTOBUF_C_TYPE_FIXED64:
        /* 64 bit values are stored as strings in json */
    case PROTOBUF_C_TYPE_ENUM:
    case PROTOBUF_C_TYPE_STRING:
        fmt = field_descriptor->label == PROTOBUF_C_LABEL_REPEATED ? "{s[s?]}" : "{ss?}";

        new_object = json_pack (fmt, field_descriptor->name, value);
        break;
    case PROTOBUF_C_TYPE_BOOL:
        if (strcmp (value, "false") == 0 || strcmp (value, "true") == 0)
        {
            fmt = field_descriptor->label == PROTOBUF_C_LABEL_REPEATED ? "{s[b]}" : "{sb}";
            new_object = json_pack (fmt, field_descriptor->name, strcmp (value, "false"));
        }
        break;

    /* Not (currently) supported */
    case PROTOBUF_C_TYPE_FLOAT:
    case PROTOBUF_C_TYPE_DOUBLE:
    case PROTOBUF_C_TYPE_BYTES:
    case PROTOBUF_C_TYPE_MESSAGE:
    default:
        break;
    }

    return new_object;
}


/**
 * Set the _file field of an existing proto message to point to the passed input_data
 * pointer with length input_length. (The _file field is a repeated uint8). The data is not
 * copied.
 * @param input_data data pointer to set. Should not be freed till after message is sent.
 * @param input_length length of input_data
 * @param msg message to update
 */
void
_cmsg_proxy_file_data_to_message (const char *input_data, size_t input_length,
                                  ProtobufCMessage *msg)
{
    ProtobufCBinaryData *data_ptr = NULL;
    protobuf_c_boolean *has_field_ptr = NULL;
    const ProtobufCFieldDescriptor *file_field = NULL;

    file_field = protobuf_c_message_descriptor_get_field_by_name (msg->descriptor,
                                                                  CMSG_PROXY_SPECIAL_FIELD_FILE);

    if (file_field)
    {
        data_ptr = (ProtobufCBinaryData *) (((char *) msg) + file_field->offset);
        has_field_ptr =
            (protobuf_c_boolean *) (((char *) msg) + file_field->quantifier_offset);

        data_ptr->data = (uint8_t *) input_data;
        data_ptr->len = input_length;

        if (input_length > 0)
        {
            *has_field_ptr = true;
        }
        else
        {
            *has_field_ptr = false;
        }
    }
}

/**
 * Clear the pointer and length of the "_file" (CMSG_PROXY_SPECIAL_FIELD_FILE) field in a
 * protobuf message. (No freeing)
 * @param msg message to clear _file pointer for.
 */
static void
_cmsg_proxy_file_data_strip (ProtobufCMessage *msg)
{
    _cmsg_proxy_file_data_to_message (NULL, 0, msg);
}

/**
 * Compares a url parameter's key with a specified string.
 *
 * @param param - The url parameter as a cmsg_url_parameter
 * @param name_to_match - The string to compare to the parameter's key
 * @return Returns 0 if the url parameter's key matches the provided string, otherwise
 *         a non-zero value is returned.
 */
static int
_cmsg_proxy_param_name_matches (gconstpointer param, gconstpointer name_to_match)
{
    cmsg_url_parameter *p = (cmsg_url_parameter *) param;
    const char *key = (const char *) name_to_match;

    if (p && p->key && key)
    {
        return strcmp (p->key, key);
    }

    return -1;
}

/**
 * Parses an HTTP query string, and adds the key-value pairs to the provided list.
 *
 * Values will be URL Decoded.
 *
 * @param query_string - Query string to parse
 * @param url_parameters - List of key value pairs of parameters specified in the URL
 */
void
_cmsg_proxy_parse_query_parameters (const char *query_string, GList **url_parameters)
{
    char *tmp_query_string;
    char *next_entry = NULL;
    char *rest = NULL;
    char *value = NULL;
    char *decoded_value;
    cmsg_url_parameter *param = NULL;
    GList *matching_param = NULL;

    /* Parse query string */
    if (query_string)
    {
        tmp_query_string = CMSG_PROXY_STRDUP (query_string);

        for (next_entry = strtok_r (tmp_query_string, "&", &rest); next_entry;
             next_entry = strtok_r (NULL, "&", &rest))
        {
            value = strrchr (next_entry, '=');
            if (value)
            {
                value[0] = '\0';

                /* Only add the parameter if it is not already assigned
                 * (query parameters shouldn't overwrite path parameters) */
                matching_param = g_list_find_custom (*url_parameters, next_entry,
                                                     _cmsg_proxy_param_name_matches);
                if (!matching_param)
                {
                    decoded_value = g_uri_unescape_string (value + 1, NULL);
                    param = _cmsg_proxy_create_url_parameter (next_entry, decoded_value);
                    g_free (decoded_value);
                    *url_parameters = g_list_prepend (*url_parameters, param);
                }
            }
        }

        CMSG_PROXY_FREE (tmp_query_string);
    }
}

/**
 * Set the internal api info field value in the input message descriptor.
 *
 * @param internal_info_value - The internal api info value to set in the message body
 * @param json_obj - Pointer to the message body.
 * @param msg_descriptor - Used to determine the target field type when converting to JSON.
 * @param field_name - Target field name in the message to put the internal api info value.
 */
void
_cmsg_proxy_set_internal_api_value (const char *internal_info_value,
                                    json_t **json_obj,
                                    const ProtobufCMessageDescriptor *msg_descriptor,
                                    const char *field_name)
{
    const ProtobufCFieldDescriptor *field_descriptor = NULL;
    json_t *new_object = NULL;

    field_descriptor = protobuf_c_message_descriptor_get_field_by_name (msg_descriptor,
                                                                        field_name);

    if (field_descriptor)
    {
        new_object = _cmsg_proxy_json_value_to_object (field_descriptor,
                                                       internal_info_value);
        if (!new_object)
        {
            syslog (LOG_ERR, "Could not create json object for %s", field_name);
            return;
        }

        if (*json_obj)
        {
            json_object_update (*json_obj, new_object);
            json_decref (new_object);
        }
        else
        {
            *json_obj = new_object;
        }
    }
}


/**
 * Convert the input protobuf message into a json object.
 *
 * @param input_protobuf - The protobuf message to convert.
 * @param output_json - A pointer to store the output json object.
 *                      If the conversion succeeds then this object must
 *                      be freed by the caller.
 *
 * @return - true on success, false on failure.
 */
static bool
_cmsg_proxy_protobuf2json_object (ProtobufCMessage *input_protobuf, json_t **output_json)
{
    if (protobuf2json_object (input_protobuf, output_json, NULL, 0) < 0)
    {
        return false;
    }

    return true;
}

/**
 * Helper function to call the CMSG api function pointer in the
 * cmsg service info entry. This is required as the api function
 * takes a different number of parameters depending on the input/
 * output message types.
 *
 * @param client - CMSG client to call the API with
 * @param input_msg - Input message to send with the API
 * @param output_msg - Pointer for the received message from the API
 *                     to be stored in.
 * @param service_info - Service info entry that contains the API
 *                       function to call.
 *
 * @returns - ANT_CODE_OK on success,
 *            ANT_CODE_INTERNAL if CMSG fails internally.
 */
static ant_code
_cmsg_proxy_call_cmsg_api (const cmsg_client *client, ProtobufCMessage *input_msg,
                           ProtobufCMessage **output_msg,
                           const cmsg_service_info *service_info)
{
    int ret;

    if (strcmp (service_info->input_msg_descriptor->name, "dummy") == 0)
    {
        ret = service_info->api_ptr (client, output_msg);
    }
    else if (strcmp (service_info->output_msg_descriptor->name, "dummy") == 0)
    {
        ret = service_info->api_ptr (client, input_msg);
    }
    else
    {
        ret = service_info->api_ptr (client, input_msg, output_msg);
    }

    if (ret == CMSG_RET_OK)
    {
        return ANT_CODE_OK;
    }
    else
    {
        return ANT_CODE_INTERNAL;
    }
}

/**
 * Applies json_t object to output (sets data and length). Output data is allocated.
 * @param json_data json_t of data we want to convert to string
 * @param json_flags conversion flags
 * @param output response data for cmsg proxy
 */
void
_cmsg_proxy_json_t_to_output (json_t *json_data, size_t json_flags,
                              cmsg_proxy_output *output)
{
    char *response_body = json_dumps (json_data, json_flags);

    if (response_body)
    {
        output->response_body = response_body;
        output->response_length = strlen (response_body);
    }
    else
    {
        output->response_length = 0;
    }
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
_cmsg_proxy_set_http_status (int *http_status, cmsg_http_verb http_verb,
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
        *http_status = _cmsg_proxy_ant_code_to_http_code (error_message->code);
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
 * Strip out the details array field from the JSON object representing
 * an ant_result structure if the array is empty. This is a special case
 * strictly for the ant_result message where we don't want to return empty
 * arrays.
 *
 * @param ant_result_json_object - JSON object representing the ant_result message
 */
static void
cmsg_proxy_strip_details_from_ant_result (json_t *ant_result_json_object)
{
    json_t *details_array = NULL;

    details_array = json_object_get (ant_result_json_object, "details");
    if (details_array && json_is_array (details_array) &&
        json_array_size (details_array) == 0)
    {
        json_object_del (ant_result_json_object, "details");
    }
}

/**
 * Generate a ANT_RESULT error output for an internal cmsg_proxy error
 *
 * @param code - ANT_CODE appropriate to the reason for failure
 * @param message - Descriptive error message
 * @param output - CMSG proxy response
 */
void
_cmsg_proxy_generate_ant_result_error (ant_code code, char *message,
                                       cmsg_proxy_output *output)
{
    ant_result error = ANT_RESULT_INIT;
    json_t *converted_json_object = NULL;

    CMSG_SET_FIELD_VALUE (&error, code, code);
    CMSG_SET_FIELD_PTR (&error, message, message);

    output->http_status = _cmsg_proxy_ant_code_to_http_code (code);

    if (!_cmsg_proxy_protobuf2json_object ((ProtobufCMessage *) &error,
                                           &converted_json_object))
    {
        output->http_status = HTTP_CODE_INTERNAL_SERVER_ERROR;
        return;
    }

    cmsg_proxy_strip_details_from_ant_result (converted_json_object);

    _cmsg_proxy_json_t_to_output (converted_json_object, JSON_COMPACT, output);
    json_decref (converted_json_object);
}

/**
 * Generate a plaintext response based on the contents of the "_body"
 * (CMSG_PROXY_SPECIAL_FIELD_BODY) field.
 *
 * @param output_proto_message - The message returned from calling the CMSG API
 * @param output - CMSG proxy response
 */
static bool
_cmsg_proxy_generate_plaintext_response (ProtobufCMessage *output_proto_message,
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
            output->mime_type = cmsg_mime_text_plain;
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
 * Generate a file response based on the contents of the "_file"
 * (CMSG_PROXY_SPECIAL_FIELD_FILE) field.
 * Sets a header with the file name if the message contains a field called "file_name"
 *
 * @param output_proto_message - The message returned from calling the CMSG API
 * @param output - CMSG proxy response
 */
static bool
_cmsg_proxy_generate_file_response (ProtobufCMessage *output_proto_message,
                                    cmsg_proxy_output *output)
{
    const ProtobufCFieldDescriptor *field_descriptor = NULL;
    const ProtobufCBinaryData *file_ptr = NULL;
    const char **file_name_ptr = NULL;
    const char *file_name = NULL;
    cmsg_proxy_header *header_array;
    cmsg_proxy_headers *headers;
    char *filename_header_value = NULL;
    int ret;

    output->response_length = 0;

    field_descriptor =
        protobuf_c_message_descriptor_get_field_by_name (output_proto_message->descriptor,
                                                         CMSG_PROXY_SPECIAL_FIELD_FILE);
    if (!field_descriptor)
    {
        return false;
    }

    file_ptr =
        (const ProtobufCBinaryData *) ((char *) output_proto_message +
                                       field_descriptor->offset);

    if (file_ptr && file_ptr->data)
    {
        /* This is allocated with malloc rather than CMSG_PROXY_MALLOC as it is freed with
         * free by the caller in cmsgProxyHandler. response_body is also populated using
         * json_dumps, which uses standard allocation. memcpy is used rather than strdup,
         * because this may be binary data that includes (or isn't terminated with) null
         * characters.
         */
        output->response_body = malloc (file_ptr->len);
        if (!output->response_body)
        {
            return false;
        }

        memcpy (output->response_body, file_ptr->data, file_ptr->len);

        // header("Content-Type: application/octet-stream");
        output->mime_type = cmsg_mime_octet_stream;
        output->response_length = file_ptr->len;
    }

    field_descriptor =
        protobuf_c_message_descriptor_get_field_by_name (output_proto_message->descriptor,
                                                         CMSG_PROXY_SPECIAL_FIELD_FILE_NAME);
    if (field_descriptor)
    {
        file_name_ptr =
            (const char **) ((char *) output_proto_message + field_descriptor->offset);
        file_name = *file_name_ptr;
    }
    ret = CMSG_PROXY_ASPRINTF (&filename_header_value, cmsg_filename_header_format,
                               file_name ? file_name : "unknown");

#define NUM_FILE_HEADERS 2

    header_array = CMSG_PROXY_CALLOC (NUM_FILE_HEADERS, sizeof (cmsg_proxy_header));
    headers = CMSG_PROXY_CALLOC (1, sizeof (cmsg_proxy_headers));
    if (!header_array || !headers || ret < 0)
    {
        CMSG_PROXY_FREE (header_array);
        CMSG_PROXY_FREE (headers);
        CMSG_PROXY_FREE (filename_header_value);
        free (output->response_body);
        output->response_body = NULL;
        output->response_length = 0;
        return false;
    }

    // header("Content-Disposition: attachment; filename=\"$file_name\"");
    // header("Content-Transfer-Encoding: binary");

    header_array[0].key = cmsg_content_disposition_key;
    header_array[0].value = filename_header_value;
    header_array[1].key = cmsg_content_encoding_key;
    header_array[1].value = CMSG_PROXY_STRDUP (cmsg_binary_encoding);
    headers->headers = header_array;
    headers->num_headers = NUM_FILE_HEADERS;
    output->extra_headers = headers;

    return true;
}

/**
 * Generate the body of the response that should be returned to the web API caller.
 *
 * @param output_proto_message - The message returned from calling the CMSG API
 * @param output - CMSG proxy response
 */
static bool
_cmsg_proxy_generate_response_body (ProtobufCMessage *output_proto_message,
                                    cmsg_proxy_output *output)
{
    json_t *converted_json_object = NULL;
    const char *key = NULL;
    json_t *value = NULL;
    json_t *_error_info = NULL;

    // Handle special response types (if the response was successful)
    if (output->http_status == HTTP_CODE_OK)
    {
        if (_cmsg_proxy_msg_has_body_override (output_proto_message->descriptor))
        {
            // If the message provides a '_body' override, simply return that.
            return _cmsg_proxy_generate_plaintext_response (output_proto_message, output);
        }
        else if (_cmsg_proxy_msg_has_file (output_proto_message->descriptor))
        {
            // If the message contains a file, return the contents of the file.
            return _cmsg_proxy_generate_file_response (output_proto_message, output);
        }
    }

    if (!_cmsg_proxy_protobuf2json_object (output_proto_message, &converted_json_object))
    {
        return false;
    }

    /* If the API simply returns an 'ant_result' message then no further
     * processing is required, simply return it. */
    if (strcmp (output_proto_message->descriptor->name, "ant_result") == 0)
    {
        cmsg_proxy_strip_details_from_ant_result (converted_json_object);
        _cmsg_proxy_json_t_to_output (converted_json_object, JSON_COMPACT, output);
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
                _cmsg_proxy_json_t_to_output (value, JSON_ENCODE_ANY | JSON_COMPACT,
                                              output);
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
                _cmsg_proxy_json_t_to_output (value, JSON_ENCODE_ANY | JSON_COMPACT,
                                              output);
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
    _cmsg_proxy_json_t_to_output (converted_json_object, JSON_COMPACT, output);
    json_decref (converted_json_object);
    return true;
}

/**
 * Initialise the cmsg proxy library
 */
void
cmsg_proxy_init (void)
{
    cmsg_proxy_mem_init ();
    cmsg_proxy_counter_init ();
    cmsg_proxy_tree_init ();
#ifndef HAVE_UNITTEST
    cmsg_proxy_streaming_init ();
#endif /* HAVE_UNITTEST */
}

/**
 * Set a callback that is called before making a request to the API.
 * This can be used to prevent a call to the API based on some condition.
 *
 * @param cb - This is called before making a request to the API.
 */
void
cmsg_proxy_set_pre_api_http_check_callback (pre_api_http_check_callback cb)
{
    pre_api_check_callback = cb;
}

/**
 * Deinitialize the cmsg proxy library
 */
void
cmsg_proxy_deinit (void)
{
    cmsg_proxy_tree_deinit ();

    cmsg_proxy_counter_deinit ();

    pre_api_check_callback = NULL;
}


/**
 * Free data returned in the "output" pointer in a cmsg_proxy call.
 * @param output pointer previously passed to cmsg_proxy
 */
void
cmsg_proxy_free_output_contents (cmsg_proxy_output *output)
{
    if (output)
    {
        if (output->extra_headers)
        {
            if (output->extra_headers->headers)
            {
                for (int i = 0; i < output->extra_headers->num_headers; i++)
                {
                    CMSG_PROXY_FREE (output->extra_headers->headers[i].value);
                }
                CMSG_PROXY_FREE (output->extra_headers->headers);
            }
            CMSG_PROXY_FREE (output->extra_headers);
        }

        /* Uses free rather than CMSG_PROXY_FREE because this is usually generated by
         * json_dumps, so all places that allocate response use regular allocation.
         */
        free (output->response_body);
    }
}

/**
 * Proxy an HTTP request into the AW+ CMSG internal API. Uses the HttpRules defined
 * for each rpc defined in the CMSG .proto files.
 *
 * @param input. Input data for the request
 * @param output. output data for the response
 *
 * @return - true if the CMSG proxy actioned the request (i.e. it knew about the URL
 *           because it is defined on an rpc in the .proto files).
 *           false if the CMSG proxy performed no action (i.e. it could not map the URL
 *           to a CMSG API).
 */
bool
cmsg_proxy (const cmsg_proxy_input *input, cmsg_proxy_output *output)
{
    ProtobufCMessage *input_proto_message = NULL;
    ProtobufCMessage *output_proto_message = NULL;
    ant_code result = ANT_CODE_OK;
    cmsg_proxy_processing_info processing_info = {
        .is_file_input = false,
        .client = NULL,
        .service_info = NULL,
        .streaming_id = 0,
    };

    /* By default handle responses with MIME type "application/json"
     */
    output->mime_type = cmsg_mime_application_json;

    if (strcmp (input->url, "/v1/index") == 0 && input->http_verb == CMSG_HTTP_GET)
    {
        output->http_status = cmsg_proxy_index (input->query_string, output);
        return true;
    }

    input_proto_message = cmsg_proxy_input_process (input, output, &processing_info);
    if (!input_proto_message)
    {
        return true;
    }

    result = _cmsg_proxy_call_cmsg_api (processing_info.client, input_proto_message,
                                        &output_proto_message,
                                        processing_info.service_info);

    if (processing_info.is_file_input)
    {
        // Clear message CMSG_PROXY_SPECIAL_FIELD_FILE field pointer so that we don't
        // attempt to free input_data
        _cmsg_proxy_file_data_strip (input_proto_message);
    }

    if (result != ANT_CODE_OK)
    {
        /* Something went wrong calling the CMSG api */
        CMSG_FREE_RECV_MSG (input_proto_message);
        _cmsg_proxy_generate_ant_result_error (result, NULL, output);
        CMSG_PROXY_SESSION_COUNTER_INC (processing_info.service_info,
                                        cntr_error_api_failure);
        return true;
    }

    CMSG_FREE_RECV_MSG (input_proto_message);

    if (!_cmsg_proxy_set_http_status (&output->http_status, input->http_verb,
                                      &output_proto_message))
    {
        syslog (LOG_ERR, "_error_info is not set for %s",
                processing_info.service_info->url_string);
        CMSG_PROXY_SESSION_COUNTER_INC (processing_info.service_info,
                                        cntr_error_missing_error_info);
    }

    if (output->stream_response)
    {
        if (output->http_status == HTTP_CODE_OK)
        {
            /* We're streaming the response so it will be sent back asynchronously */
            CMSG_FREE_RECV_MSG (output_proto_message);
            return true;
        }
        else
        {
            /* The IMPL has rejected/failed the request to stream
             * the response. */
            output->stream_response = false;
            cmsg_proxy_remove_stream_by_id (processing_info.streaming_id);
        }
    }

    if (output_proto_message)
    {
        if (!_cmsg_proxy_generate_response_body (output_proto_message, output))
        {
            /* This should not occur (the ProtobufCMessage structure returned
             * by the CMSG api should always be well formed) but check for it */
            output->http_status = HTTP_CODE_INTERNAL_SERVER_ERROR;
            CMSG_PROXY_SESSION_COUNTER_INC (processing_info.service_info,
                                            cntr_error_protobuf_to_json);
        }
        CMSG_FREE_RECV_MSG (output_proto_message);
    }

    return true;
}
