/*
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_PROXY_INPUT_H_
#define __CMSG_PROXY_INPUT_H_

#include "cmsg_proxy.h"
#include "cmsg_proxy_private.h"

ProtobufCMessage *cmsg_proxy_input_process (const cmsg_proxy_input *input,
                                            cmsg_proxy_output *output,
                                            cmsg_proxy_processing_info *processing_info);
const cmsg_service_info *cmsg_proxy_get_service_and_parameters (const char *url,
                                                                const char *query_string,
                                                                cmsg_http_verb verb,
                                                                GList **url_parameters);
#endif /* __CMSG_PROXY_INPUT_H_ */
