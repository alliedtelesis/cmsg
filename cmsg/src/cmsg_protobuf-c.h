/*
 * cmsg_protobuf-c.h
 *
 * Copyright 2020, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_PROTOBUF_C_H_
#define __CMSG_PROTOBUF_C_H_

#include <protobuf-c/protobuf-c.h>

#define UNDEFINED_METHOD 0xffffffff
#define IS_METHOD_DEFINED(x)  (x == UNDEFINED_METHOD ? false : true)

void protobuf_c_message_free_unknown_fields (ProtobufCMessage *message,
                                             ProtobufCAllocator *allocator);
unsigned protobuf_c_service_descriptor_get_method_index_by_name (const
                                                                 ProtobufCServiceDescriptor
                                                                 *desc, const char *name);

#endif /* __CMSG_PROTOBUF_C_H_ */
