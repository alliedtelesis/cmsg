/*
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_PROXY_OUTPUT_H_
#define __CMSG_PROXY_OUTPUT_H_

#include "cmsg_proxy.h"
#include "cmsg_proxy_private.h"

void
cmsg_proxy_output_process (ProtobufCMessage *output_proto_message,
                           cmsg_proxy_output *output,
                           cmsg_proxy_processing_info *processing_info);

#endif /* __CMSG_PROXY_OUTPUT_H_ */
