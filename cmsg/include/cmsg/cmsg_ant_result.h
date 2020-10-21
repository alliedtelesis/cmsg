/**
 * cmsg_ant_result.h
 *
 * Copyright 2020, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_ANT_RESULT_H_
#define __CMSG_ANT_RESULT_H_

#include <protobuf-c/protobuf-c.h>
#include "ant_result_types_auto.h"

ProtobufCMessage *cmsg_create_ant_response (const char *message, ant_code code,
                                            const ProtobufCMessageDescriptor *output_desc);

#endif /* __CMSG_ANT_RESULT_H_ */
