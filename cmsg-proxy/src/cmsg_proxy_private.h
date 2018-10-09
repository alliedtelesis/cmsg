/*
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_PROXY_PRIVATE_H_
#define __CMSG_PROXY_PRIVATE_H_

#include "ant_result.pb-c.h"

typedef struct _cmsg_proxy_processing_info
{
    bool is_file_input;
    const cmsg_service_info *service_info;
    const cmsg_client *client;
    uint32_t streaming_id;
    ant_code cmsg_api_result;
    cmsg_http_verb http_verb;
} cmsg_proxy_processing_info;

void cmsg_proxy_set_internal_api_value (const char *internal_info_value,
                                        json_t **json_obj,
                                        const ProtobufCMessageDescriptor *msg_descriptor,
                                        const char *field_name);
void cmsg_proxy_parse_query_parameters (const char *query_string, GList **url_parameters);

void cmsg_proxy_json_t_to_output (json_t *json_data, size_t json_flags,
                                  cmsg_proxy_output *output);

bool cmsg_proxy_msg_has_file (const ProtobufCMessageDescriptor *msg_descriptor);

json_t *cmsg_proxy_json_value_to_object (const ProtobufCFieldDescriptor *field_descriptor,
                                         const char *value);

void cmsg_proxy_generate_ant_result_error (ant_code code, char *message,
                                           cmsg_proxy_output *output);

void cmsg_proxy_file_data_to_message (const char *input_data, size_t input_length,
                                      ProtobufCMessage *msg);

int cmsg_proxy_ant_code_to_http_code (int ant_code);

bool cmsg_proxy_protobuf2json_object (ProtobufCMessage *input_protobuf,
                                      json_t **output_json);

void cmsg_proxy_strip_details_from_ant_result (json_t *ant_result_json_object);

bool cmsg_proxy_generate_response_body (ProtobufCMessage *output_proto_message,
                                        cmsg_proxy_output *output);

void cmsg_proxy_strip_ant_result (ProtobufCMessage **msg);

#endif /* __CMSG_PROXY_PRIVATE_H_ */
