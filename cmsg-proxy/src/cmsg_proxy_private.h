/*
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_PROXY_PRIVATE_H_
#define __CMSG_PROXY_PRIVATE_H_

#include "ant_result.pb-c.h"

void _cmsg_proxy_set_internal_api_value (const char *internal_info_value,
                                         json_t **json_obj,
                                         const ProtobufCMessageDescriptor *msg_descriptor,
                                         const char *field_name);
void _cmsg_proxy_parse_query_parameters (const char *query_string, GList **url_parameters);

void _cmsg_proxy_json_t_to_output (json_t *json_data, size_t json_flags,
                                   cmsg_proxy_output *output);

bool _cmsg_proxy_msg_has_file (const ProtobufCMessageDescriptor *msg_descriptor);

json_t *_cmsg_proxy_json_value_to_object (const ProtobufCFieldDescriptor *field_descriptor,
                                          const char *value);

void _cmsg_proxy_generate_ant_result_error (ant_code code, char *message,
                                            cmsg_proxy_output *output);

void _cmsg_proxy_file_data_to_message (const char *input_data, size_t input_length,
                                       ProtobufCMessage *msg);

#endif /* __CMSG_PROXY_PRIVATE_H_ */
