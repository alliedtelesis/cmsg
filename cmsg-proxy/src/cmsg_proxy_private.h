/*
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_PROXY_PRIVATE_H_
#define __CMSG_PROXY_PRIVATE_H_

void _cmsg_proxy_set_internal_api_value (const char *internal_info_value,
                                         json_t **json_obj,
                                         const ProtobufCMessageDescriptor *msg_descriptor,
                                         const char *field_name);

#endif /* __CMSG_PROXY_PRIVATE_H_ */
