/*
 * cmsg_protobuf-c.c
 *
 * Implements the extra protobuf-c functionality and knowledge required
 * by the CMSG library.
 *
 * Copyright 2020, Allied Telesis Labs New Zealand, Ltd
 */

#include <string.h>
#include <protobuf-c/protobuf-c.h>
#include "cmsg_protobuf-c.h"

void
protobuf_c_message_free_unknown_fields (ProtobufCMessage *message,
                                        ProtobufCAllocator *allocator)
{
    unsigned f;

    if (message == NULL)
    {
        return;
    }

    for (f = 0; f < message->n_unknown_fields; f++)
    {
        allocator->free (allocator->allocator_data, message->unknown_fields[f].data);
    }

    if (message->unknown_fields != NULL)
    {
        allocator->free (allocator->allocator_data, message->unknown_fields);
    }

    message->n_unknown_fields = 0;
    message->unknown_fields = NULL;
}

unsigned
protobuf_c_service_descriptor_get_method_index_by_name (const ProtobufCServiceDescriptor
                                                        *desc, const char *name)
{
    unsigned start = 0;
    unsigned count;

    if (desc == NULL || desc->method_indices_by_name == NULL)
    {
        return UNDEFINED_METHOD;
    }

    count = desc->n_methods;

    while (count > 1)
    {
        unsigned mid = start + count / 2;
        unsigned mid_index = desc->method_indices_by_name[mid];
        const char *mid_name = desc->methods[mid_index].name;
        int rv = strcmp (mid_name, name);

        if (rv == 0)
        {
            return desc->method_indices_by_name[mid];
        }

        if (rv < 0)
        {
            count = start + count - (mid + 1);
            start = mid + 1;
        }
        else
        {
            count = mid - start;
        }
    }

    if (count == 0)
    {
        return UNDEFINED_METHOD;
    }

    if (strcmp (desc->methods[desc->method_indices_by_name[start]].name, name) == 0)
    {
        return desc->method_indices_by_name[start];
    }

    return UNDEFINED_METHOD;
}
