/*
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */

#include <protobuf2json.h>
#include "cmsg_proxy_input.h"
#include "cmsg_proxy_private.h"
#include "cmsg_proxy_tree.h"
#include "cmsg_proxy_mem.h"
#include "cmsg_proxy_counters.h"
#include "cmsg_proxy_http_streaming.h"
#include <cmsg/ant_result.pb-c.h>

#define MSG_BUF_LEN 200

extern pre_api_http_check_callback pre_api_check_callback;

/**
 * This calls the pre-API check callback provided by the application.
 *
 * @param http_verb - The HTTP request method
 * @param message - This is assigned an error message if ANT_CODE_UNAVAILABLE
 *                  is returned. The message should be freed by the caller.
 * @return - ANT_CODE_UNAVAILABLE if the pre-check fails, otherwise ANT_CODE_OK.
 */
static ant_code
cmsg_proxy_pre_api_check (cmsg_http_verb http_verb, char **message)
{
    char *error_msg = NULL;

    if (pre_api_check_callback && !pre_api_check_callback (http_verb, &error_msg))
    {
        if (!error_msg)
        {
            error_msg = CMSG_PROXY_STRDUP ("Pre-API check failed");
        }
        *message = error_msg;
        return ANT_CODE_UNAVAILABLE;
    }

    return ANT_CODE_OK;
}

/**
 * Convert the input json string into a protobuf message structure.
 *
 * @param input_json - The json string to convert.
 * @param msg_descriptor - The message descriptor that defines the protobuf
 *                         message to convert the json string to.
 * @param output_protobuf - A pointer to store the output protobuf message.
 *                          If the conversion succeeds then this pointer must
 *                          be freed by the caller.
 *
 * @return - ANT_CODE_OK on success, relevant ANT code on failure.
 */
static ant_code
cmsg_proxy_convert_json_to_protobuf (json_t *json_obj,
                                     const ProtobufCMessageDescriptor *msg_descriptor,
                                     ProtobufCMessage **output_protobuf, char **message)
{
    char conversion_message[MSG_BUF_LEN] = { 0 };
    char *error_msg = NULL;
    ant_code ret = ANT_CODE_OK;
    int res = json2protobuf_object (json_obj, msg_descriptor, output_protobuf,
                                    conversion_message, MSG_BUF_LEN);

    /* Only report messages deemed user-friendly */
    switch (res)
    {
    case PROTOBUF2JSON_ERR_REQUIRED_IS_MISSING:
    case PROTOBUF2JSON_ERR_UNKNOWN_FIELD:
    case PROTOBUF2JSON_ERR_IS_NOT_OBJECT:
    case PROTOBUF2JSON_ERR_IS_NOT_ARRAY:
    case PROTOBUF2JSON_ERR_IS_NOT_INTEGER:
    case PROTOBUF2JSON_ERR_IS_NOT_INTEGER_OR_REAL:
    case PROTOBUF2JSON_ERR_IS_NOT_BOOLEAN:
    case PROTOBUF2JSON_ERR_IS_NOT_STRING:
    case PROTOBUF2JSON_ERR_UNKNOWN_ENUM_VALUE:
    case PROTOBUF2JSON_ERR_CANNOT_PARSE_STRING:
    case PROTOBUF2JSON_ERR_CANNOT_PARSE_FILE:
    case PROTOBUF2JSON_ERR_UNSUPPORTED_FIELD_TYPE:
        error_msg = CMSG_PROXY_STRDUP (conversion_message);
        ret = ANT_CODE_INVALID_ARGUMENT;
        break;
    case PROTOBUF2JSON_ERR_CANNOT_DUMP_STRING:
    case PROTOBUF2JSON_ERR_CANNOT_DUMP_FILE:
    case PROTOBUF2JSON_ERR_JANSSON_INTERNAL:
    case PROTOBUF2JSON_ERR_CANNOT_ALLOCATE_MEMORY:
        error_msg = CMSG_PROXY_STRDUP ("JSON to Protobuf conversion failed");
        ret = ANT_CODE_INTERNAL;
    }

    *message = error_msg;

    return ret;
}

/**
 * Lookup a cmsg_service_info entry based on URL and HTTP verb and parse
 * URL and query parameters. The URL parameters must always be parsed before
 * the query parameters so that we can prevent query parameters from overwriting
 * the URL parameters.
 *
 * @param url - The encoded URL to use for the lookup.
 * @param query_string - Query string provided with the URL
 * @param http_verb - HTTP verb to use for the lookup.
 * @param url_parameters - List to populate with parameters parsed from the URL
 *                         and query string.
 *
 * @return - Pointer to the cmsg_service_info entry if found, NULL otherwise.
 */
const cmsg_service_info *
cmsg_proxy_get_service_and_parameters (const char *url, const char *query_string,
                                       cmsg_http_verb verb, GList **url_parameters)
{
    const cmsg_service_info *service_info = NULL;

    service_info = cmsg_proxy_find_service_from_url_and_verb (url, verb, url_parameters);
    if (service_info && query_string)
    {
        cmsg_proxy_parse_query_parameters (query_string, url_parameters);
    }

    return service_info;
}

/**
 * Checks whether the given field name corresponds to a hidden field.
 *
 * @param field_name - The field name to check.
 *
 * @returns true if the field is hidden, false otherwise.
 */
bool
cmsg_proxy_field_is_hidden (const char *field_name)
{
    return (strncmp (field_name, "_", 1) == 0);
}

/**
 * Find the field that was not parsed from the URL
 *
 * @param msg_descriptor - Protobuf-c message descriptor for the input message
 *                         that should be called with the CMSG API
 * @param url_parameters - List of parameters that were parsed out of the input URL
 *
 * @returns - The protobuf-c field descriptor for the field that was not parsed
 *            from the URL parameters
 */
static const ProtobufCFieldDescriptor *
cmsg_proxy_find_unparsed_field (const ProtobufCMessageDescriptor *msg_descriptor,
                                const GList *url_parameters)
{
    const ProtobufCFieldDescriptor *field_desc = NULL;
    int i = 0;
    const GList *list = NULL;
    const cmsg_url_parameter *list_data = NULL;
    bool found = false;
    const ProtobufCFieldDescriptor *ret = NULL;
    const char *field_name = NULL;

    for (i = 0; i < msg_descriptor->n_fields; i++)
    {
        field_desc = &(msg_descriptor->fields[i]);
        field_name = field_desc->name;

        /* The hidden fields should never be set in the input path */
        if (cmsg_proxy_field_is_hidden (field_name))
        {
            continue;
        }

        for (list = url_parameters; list != NULL; list = list->next)
        {
            list_data = (cmsg_url_parameter *) list->data;
            if (strcmp (list_data->key, field_name) == 0)
            {
                found = true;
                break;
            }
        }

        if (!found)
        {
            ret = field_desc;
            break;
        }

        found = false;
    }

    return ret;
}

/**
 * Sanity checks for a JSON object input to the web API
 * by the user.
 *
 * @param json_obj - Converted JSON object input by the user
 * @param error - Error structure to hold error information
 *
 * @returns - true if sanity checks pass, false otherwise
 */
static bool
cmsg_proxy_json_object_sanity_check (json_t *json_obj, json_error_t *error)
{
    const char *key;
    json_t *value;

    if (!json_is_object (json_obj))
    {
        snprintf (error->text, JSON_ERROR_TEXT_LENGTH,
                  "JSON object expected but JSON value or array given");
        return false;
    }

    /* Sanity check the user hasn't attempted to give any hidden fields */
    json_object_foreach (json_obj, key, value)
    {
        if (cmsg_proxy_field_is_hidden (key))
        {
            snprintf (error->text, JSON_ERROR_TEXT_LENGTH, "Invalid JSON");
            return false;
        }
    }

    return true;
}

/**
 * Sanity checks for a JSON object array input to the web API
 * by the user.
 *
 * @param json_array - Converted JSON array input by the user
 * @param error - Error structure to hold error information
 *
 * @returns - true if sanity checks pass, false otherwise
 */
static bool
cmsg_proxy_json_object_array_sanity_check (json_t *json_array, json_error_t *error)
{
    size_t index;
    json_t *value;

    if (!json_is_array (json_array))
    {
        snprintf (error->text, JSON_ERROR_TEXT_LENGTH,
                  "JSON array expected but JSON value or object given");
        return false;
    }

    json_array_foreach (json_array, index, value)
    {
        if (!cmsg_proxy_json_object_sanity_check (value, error))
        {
            snprintf (error->text, JSON_ERROR_TEXT_LENGTH, "Invalid JSON");
            return false;
        }
    }

    return true;
}

/**
 * Check that no input data is received if we don't expect any or that input data is
 * received when we do expect it. If input data does not match expectations, generate an
 * appropriate error message.
 * @param data data we received as input
 * @param body_string What the API states we expect as data
 * @param error_message pointer to hold returned error if input data presence does not meet
 *                      expectations. free with CMSG_PROXY_FREE
 * @returns true if data presence is as expected, else false.
 */
static bool
cmsg_proxy_input_data_presence_as_expected (const char *data, const char *body_string,
                                            char **error_message)
{
    char *error_msg = NULL;
    bool ret = false;

    if (body_string[0] == '\0')
    {
        if (data)
        {
            error_msg =
                CMSG_PROXY_STRDUP
                ("Invalid JSON: No JSON data expected for API, but JSON data input");
        }
        else
        {
            ret = true;
        }
    }
    else if (body_string[0] != '*' && !data)
    {
        /* This is a compromise. We don't reject empty input if body string is set to '*'.
         * This means that for primitive fields, we can reject empty input by setting
         * the body string to a value. for more complex messages this can still be done by
         * putting the body data fields in a sub-message and setting the sub-message field
         * as the body string.
         * There are existing APIs both with primitive and non-primitive fields that are
         * documented as being input optional.
         */
        error_msg = CMSG_PROXY_STRDUP ("Invalid JSON: Input expected but not provided.");
    }
    else
    {
        ret = true;
    }

    *error_message = error_msg;

    return ret;
}

/**
 * Create a new json object from the json string that was given as
 * input with the cmsg proxy.
 *
 * @param input_data  - Input data as received. This is most likely the json string to
 *                      create the json object, but could be file data. If it is intended
 *                      for file input, an early return will be done with an empty
 *                      JSON object
 * @param input_length  - Length of the input data.
 * @param msg_descriptor - Protobuf-c message descriptor for the input message
 *                         that should be called with the CMSG API
 * @param body_string - A string that describes the input body mapping
 * @param url_parameters - List of parameters that were parsed out of the input URL
 * @param error       - Place holder for error occurred in the creation
 *
 * @returns - The created json object or NULL on failure
 */
static json_t *
cmsg_proxy_json_object_create (const char *input_data, size_t input_length,
                               const ProtobufCMessageDescriptor *msg_descriptor,
                               const char *body_string, GList *url_parameters,
                               json_error_t *error)
{
    json_t *converted_json = NULL;
    const ProtobufCFieldDescriptor *field_desc = NULL;
    json_t *json_obj = NULL;
    const char *stripped_string;
    int expected_input_fields = msg_descriptor->n_fields - g_list_length (url_parameters);
    int i;

    if (!input_data || cmsg_proxy_msg_has_file (msg_descriptor))
    {
        /* Create an empty JSON object if no JSON input was provided or if we expect
         * file input (file will be added later). */
        return json_object ();
    }

    /* Hidden fields should never be set in the input path */
    for (i = 0; i < msg_descriptor->n_fields; i++)
    {
        if (cmsg_proxy_field_is_hidden (msg_descriptor->fields[i].name))
        {
            expected_input_fields--;
        }
    }

    converted_json = json_loads (input_data, JSON_DECODE_ANY, error);
    if (!converted_json)
    {
        return NULL;
    }

    /* If the expected input is a single field, assume that input_data is the
     * value of that specific field. */
    if (strcmp (body_string, "*") != 0 || expected_input_fields == 1)
    {
        if (strcmp (body_string, "*") == 0)
        {
            field_desc = cmsg_proxy_find_unparsed_field (msg_descriptor, url_parameters);
        }
        else
        {
            field_desc = protobuf_c_message_descriptor_get_field_by_name (msg_descriptor,
                                                                          body_string);
        }

        if (!field_desc)
        {
            /* This could occur if the HttpRule 'body' field was not assigned correctly,
             * but should never happen in a production build. */
            json_decref (converted_json);
            snprintf (error->text, JSON_ERROR_TEXT_LENGTH, "Internal proxy error");
            return NULL;
        }

        if (field_desc->type == PROTOBUF_C_TYPE_MESSAGE)
        {
            if (field_desc->label == PROTOBUF_C_LABEL_REPEATED)
            {
                if (!cmsg_proxy_json_object_array_sanity_check (converted_json, error))
                {
                    json_decref (converted_json);
                    return NULL;
                }
            }
            else
            {
                if (!cmsg_proxy_json_object_sanity_check (converted_json, error))
                {
                    json_decref (converted_json);
                    return NULL;
                }
            }

            json_obj = json_pack ("{so}", field_desc->name, converted_json);
            /* json_pack with 'o' steals the reference to converted_json.
             * Therefore we don't call decref for converted_json. */
            return json_obj;
        }
        else if (json_is_object (converted_json))
        {
            json_decref (converted_json);
            snprintf (error->text, JSON_ERROR_TEXT_LENGTH,
                      "JSON value or array expected but JSON object given");
            return NULL;
        }

        if (json_is_array (converted_json))
        {
            json_obj = json_pack ("{so}", field_desc->name, converted_json);
            /* json_pack with 'o' steals the reference to converted_json.
             * Therefore we don't call decref for converted_json. */
            return json_obj;
        }
        else if (json_is_string (converted_json))
        {
            /* Ensure the enclosing "" characters are stripped from the input */
            stripped_string = json_string_value (converted_json);
            json_obj = cmsg_proxy_json_value_to_object (field_desc, stripped_string);
        }
        else if (field_desc->type == PROTOBUF_C_TYPE_ENUM ||
                 field_desc->type == PROTOBUF_C_TYPE_STRING)
        {
            /* Don't allow non-string JSON values to be accepted when the
             * field expects an ENUM or STRING value. */
            json_obj = NULL;
            snprintf (error->text, JSON_ERROR_TEXT_LENGTH, "JSON string value expected");
        }
        else
        {
            json_obj = cmsg_proxy_json_value_to_object (field_desc, input_data);
        }

        json_decref (converted_json);
        return json_obj;
    }
    else
    {
        if (!cmsg_proxy_json_object_sanity_check (converted_json, error))
        {
            json_decref (converted_json);
            return NULL;
        }

        return converted_json;
    }
}

/**
 *
 * Destroy the given json object
 *
 * @param json_obj - json object to be destroyed
 */
static void
cmsg_proxy_json_object_destroy (json_t *json_obj)
{
    if (json_obj)
    {
        json_decref (json_obj);
    }
}

/**
 * Set any required internal api info fields in the input message descriptor.
 *
 * @param web_api_info - The structure holding the web api request information
 * @param file_info - The structure holding the uploaded file information
 * @param json_obj - Pointer to the message body.
 * @param msg_descriptor - Used to determine the target field type when converting to JSON
 */
static void
cmsg_proxy_set_internal_api_info (const cmsg_proxy_api_request_info *web_api_info,
                                  json_t **json_obj,
                                  const ProtobufCMessageDescriptor *msg_descriptor)
{
    if (web_api_info)
    {
        cmsg_proxy_set_internal_api_value (web_api_info->api_request_ip_address,
                                           json_obj, msg_descriptor,
                                           "_api_request_ip_address");
        cmsg_proxy_set_internal_api_value (web_api_info->api_request_username,
                                           json_obj, msg_descriptor,
                                           "_api_request_username");
    }
}


/**
 * Sets fields describing the file upload information in the input message descriptor.
 * Only sets fields if a file has been uploaded and the input message name matches the
 * input name for the upload rpc defined in fs.proto.
 *
 * @param file_info - The structure holding the uploaded file information.
 * @param msg_descriptor - Used to determine the target field type when converting to JSON.
 * @param json_obj - Pointer to the message body.
 * @param error_message - Error message populated if ANT_CODE_INVALID_ARGUMENT is returned.
 * @return ANT_CODE_OK on success or ANT_CODE_INVALID_ARGUMENT on failure.
 * The caller will need to free memory allocated to error_message.
 */
static ant_code
cmsg_proxy_set_file_upload_info (const cmsg_proxy_api_file_info *file_info,
                                 const ProtobufCMessageDescriptor *msg_descriptor,
                                 json_t **json_obj, char **error_message)
{
    const char *upload_message_name = "file_upload_info";

    if (file_info->upload_request)
    {
        if (strcmp (msg_descriptor->name, upload_message_name) == 0)
        {
            cmsg_proxy_set_internal_api_value (file_info->temp_filename,
                                               json_obj, msg_descriptor, "_temp_filename");
            cmsg_proxy_set_internal_api_value (file_info->client_filename,
                                               json_obj, msg_descriptor,
                                               "_client_filename");
            return ANT_CODE_OK;
        }
        else
        {
            *error_message =
                CMSG_PROXY_STRDUP ("API does not accept uploaded files as input data");
            return ANT_CODE_INVALID_ARGUMENT;
        }
    }

    /* File has not been uploaded as upload_request field is not set */
    return ANT_CODE_OK;
}

/**
 * Convert parameters embedded in the URL into the correct format for the protobuf messages
 *
 * If the target protobuf is an integer type: attempt to convert the parameter. If the parameter
 * cannot be converted, leave as is. The protobuf2json library will raise an error. No sign or overflow
 * checking is yet performed.
 *
 * If the target field is repeated, the parameter will be stored as the first and only
 * element.
 *
 * @param parameters - list of parameter-name & parameter pairs
 * @param json_obj - the message body
 * @param msg_descriptor - used to determine the target field type when converting to JSON
 */
void
cmsg_proxy_parse_url_parameters (GList *parameters, json_t **json_obj,
                                 const ProtobufCMessageDescriptor *msg_descriptor)
{
    GList *iter = NULL;
    const ProtobufCFieldDescriptor *field_descriptor = NULL;
    cmsg_url_parameter *p = NULL;
    json_t *new_object = NULL;

    for (iter = parameters; iter; iter = g_list_next (iter))
    {
        p = (cmsg_url_parameter *) iter->data;

        if (!p || !p->key)
        {
            continue;
        }

        /* Find the target type */
        field_descriptor = protobuf_c_message_descriptor_get_field_by_name (msg_descriptor,
                                                                            p->key);

        if (!field_descriptor)
        {
            continue;   /* TODO: add to json as strings (for unexpected argument error) */
        }

        new_object = cmsg_proxy_json_value_to_object (field_descriptor, p->value);

        if (!new_object)
        {
            continue;
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
 * Perform the input path processing for the cmsg proxy functionality.
 * This function takes the input HTTP information (JSON data, URL and method)
 * and transforms this into the required CMSG information (client, API function and
 * ProtobufCMessage structure).
 *
 * @param input - Pointer to a cmsg_proxy_input structure storing the input information.
 * @param output - Pointer to a cmsg_proxy_output structure storing the output information.
 * @param processing_info - Pointer to a cmsg_proxy_processing_info structure to store information
 *                          deduced in the input path that is required in the output path.
 *
 * @returns The ProtobufCMessage structure transformed from the input JSON, or NULL if the
 *          input processing fails for any reason.
 */
ProtobufCMessage *
cmsg_proxy_input_process (const cmsg_proxy_input *input, cmsg_proxy_output *output,
                          cmsg_proxy_processing_info *processing_info)
{
    json_t *json_obj = NULL;
    GList *url_parameters = NULL;
    char *message = NULL;
    json_error_t error;
    const cmsg_service_info *service_info = NULL;
    ProtobufCMessage *input_proto_message = NULL;
    const ProtobufCMessageDescriptor *input_desc = NULL;
    const ProtobufCMessageDescriptor *output_desc = NULL;
    ant_code result = ANT_CODE_OK;

    service_info = cmsg_proxy_get_service_and_parameters (input->url, input->query_string,
                                                          input->http_verb,
                                                          &url_parameters);
    if (service_info == NULL)
    {
        /* The cmsg proxy does not know about this url and verb combination */
        cmsg_proxy_generate_ant_result_error (ANT_CODE_UNIMPLEMENTED,
                                              "Unknown url and verb combination", output);
        g_list_free_full (url_parameters, cmsg_proxy_free_url_parameter);
        CMSG_PROXY_COUNTER_INC (cntr_unknown_service);
        return NULL;
    }
    processing_info->service_info = service_info;
    processing_info->http_verb = input->http_verb;
    input_desc = CMSG_PROXY_INPUT_MSG_DESCRIPTOR (service_info);
    output_desc = CMSG_PROXY_OUTPUT_MSG_DESCRIPTOR (service_info);

    CMSG_PROXY_SESSION_COUNTER_INC (service_info, cntr_api_calls);

    /* Do the pre-API check */
    result = cmsg_proxy_pre_api_check (input->http_verb, &message);
    if (result != ANT_CODE_OK)
    {
        cmsg_proxy_generate_ant_result_error (result, message, output);
        CMSG_PROXY_FREE (message);
        CMSG_PROXY_SESSION_COUNTER_INC (service_info, cntr_error_api_failure);
        return NULL;
    }

    if (!cmsg_proxy_input_data_presence_as_expected (input->data, service_info->body_string,
                                                     &message))
    {
        cmsg_proxy_generate_ant_result_error (ANT_CODE_INVALID_ARGUMENT, message, output);
        CMSG_PROXY_FREE (message);
        g_list_free_full (url_parameters, cmsg_proxy_free_url_parameter);
        return NULL;
    }

    json_obj = cmsg_proxy_json_object_create (input->data, input->data_length,
                                              input_desc,
                                              service_info->body_string, url_parameters,
                                              &error);
    if (input->data && !json_obj)
    {
        /* No json object created, report the error */
        CMSG_PROXY_ASPRINTF (&message, "Invalid JSON: %s", error.text);
        cmsg_proxy_generate_ant_result_error (ANT_CODE_INVALID_ARGUMENT,
                                              (message) ? message : "Invalid JSON", output);
        CMSG_PROXY_FREE (message);
        g_list_free_full (url_parameters, cmsg_proxy_free_url_parameter);
        return NULL;
    }

    cmsg_proxy_parse_url_parameters (url_parameters, &json_obj, input_desc);

    g_list_free_full (url_parameters, cmsg_proxy_free_url_parameter);

    cmsg_proxy_set_internal_api_info (&input->web_api_info, &json_obj, input_desc);

    result = cmsg_proxy_set_file_upload_info (&input->file_info,
                                              input_desc, &json_obj, &message);
    if (result != ANT_CODE_OK)
    {
        cmsg_proxy_generate_ant_result_error (result, message, output);
        CMSG_PROXY_FREE (message);
        return NULL;
    }

    processing_info->client =
        cmsg_proxy_find_client_by_service (service_info->cmsg_desc->service_desc);
    if (processing_info->client == NULL)
    {
        /* This should not occur but check for it */
        cmsg_proxy_json_object_destroy (json_obj);
        cmsg_proxy_generate_ant_result_error (ANT_CODE_INTERNAL,
                                              "Client not found in proxy_clients_list",
                                              output);
        CMSG_PROXY_SESSION_COUNTER_INC (service_info, cntr_error_missing_client);
        return NULL;
    }

    output->stream_response =
        cmsg_proxy_streaming_create_conn (input->connection, &json_obj,
                                          input_desc, output_desc,
                                          &processing_info->streaming_id);

    /* Always create an input_proto_message to ensure that if the API call
     * requires an input it has one, even if it is empty. */
    result = cmsg_proxy_convert_json_to_protobuf (json_obj,
                                                  input_desc,
                                                  &input_proto_message, &message);

    cmsg_proxy_json_object_destroy (json_obj);
    if (result != ANT_CODE_OK)
    {
        /* The JSON sent with the request is malformed */
        cmsg_proxy_generate_ant_result_error (result, message, output);
        CMSG_PROXY_FREE (message);
        CMSG_PROXY_SESSION_COUNTER_INC (service_info, cntr_error_malformed_input);
        if (output->stream_response)
        {
            cmsg_proxy_streaming_delete_conn_by_id (processing_info->streaming_id);
            output->stream_response = false;
        }
        return NULL;
    }
    CMSG_PROXY_FREE (message);
    message = NULL;

    processing_info->is_file_input = cmsg_proxy_msg_has_file (input_desc);
    if (processing_info->is_file_input)
    {
        // Set message "_file" field to point directly to input_data (without copying)
        cmsg_proxy_file_data_to_message (input->data, input->data_length,
                                         input_proto_message);
    }

    return input_proto_message;
}
