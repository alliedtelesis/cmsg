/*
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_PROXY_PRIVATE_H_
#define __CMSG_PROXY_PRIVATE_H_

void _cmsg_proxy_set_internal_api_value (const char *internal_info_value,
                                         json_t **json_obj,
                                         const ProtobufCMessageDescriptor *msg_descriptor,
                                         const char *field_name);
void _cmsg_proxy_parse_query_parameters (const char *query_string, GList **url_parameters);

void _cmsg_proxy_json_t_to_output (json_t *json_data, size_t json_flags,
                                   cmsg_proxy_output *output);

#endif /* __CMSG_PROXY_PRIVATE_H_ */
