/*
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */
#ifndef _CMSG_DEBUG_H_
#define _CMSG_DEBUG_H_
#include <stdio.h>
#include <protobuf-c/protobuf-c.h>

void cmsg_dump_protobuf_msg (FILE *fp, const ProtobufCMessage *protobuf_message,
                             int indent);

#endif /* _CMSG_DEBUG_H_ */
