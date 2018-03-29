/*
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_PROXY_INPUT_H_
#define __CMSG_PROXY_INPUT_H_

#include "cmsg_proxy.h"

ProtobufCMessage *cmsg_proxy_input_process (const cmsg_proxy_input *input,
                                            cmsg_proxy_output *output, bool *is_file_input,
                                            const cmsg_service_info **service_info_ptr,
                                            const cmsg_client **client,
                                            uint32_t *streaming_id);
#endif /* __CMSG_PROXY_INPUT_H_ */
