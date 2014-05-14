#ifndef __CMSG_H_
#define __CMSG_H_

#include "protobuf-c.h"

// Return codes
#define CMSG_RET_OK   0
#define CMSG_RET_ERR -1
#define CMSG_RET_METHOD_NOT_FOUND -2
#define CMSG_RET_QUEUED 1
#define CMSG_RET_DROPPED 2

void cmsg_malloc_init (int mtype);

// macro to free messages returned back to the API
#define CMSG_FREE_RECV_MSG(_name) \
    protobuf_c_message_free_unpacked ((ProtobufCMessage *)(_name), &protobuf_c_default_allocator)

// macro to free _only_ the unknown fields that may be present
// in a message returned back to the API
#define CMSG_FREE_RECV_MSG_UNKNOWN_FIELDS(_name) \
    protobuf_c_message_free_unknown_fields ((ProtobufCMessage *)(_name), &protobuf_c_default_allocator)

/* Macros for setting the fields in a structure, and the associated sub-fields */
#define CMSG_SET_FIELD_VALUE(_name, _field, _value) \
    do {                                            \
        (_name)->_field = (_value);                 \
        (_name)->has_##_field = TRUE;               \
    } while (0)

#define CMSG_SET_FIELD_PTR(_name, _field, _ptr) \
    do {                                        \
        (_name)->_field = (_ptr);               \
    } while (0)

#define CMSG_SET_FIELD_REPEATED(_name, _field, _ptr, _n_elem) \
    do {                                                      \
        (_name)->_field = (_ptr);                             \
        (_name)->n_##_field = (_n_elem);                      \
    } while (0)

#define CMSG_IS_FIELD_PRESENT(_msg, _field) \
    ((_msg)->has_##_field ? TRUE : FALSE)

#define CMSG_IS_PTR_PRESENT(_msg, _ptr) \
    ((_msg)->_ptr ? TRUE : FALSE)

#define CMSG_IS_REPEATED_PRESENT(_msg, _field) \
    ((_msg)->n_##_field ? TRUE : FALSE)

typedef enum _cmsg_old_msg_type_e
{
    CMSG_OLD_MSG_TYPE_METHOD_REQ = 0,   // Request to server to call a method
    CMSG_OLD_MSG_TYPE_METHOD_REPLY,     // Reply from server in response to a method request
} cmsg_old_msg_type;

/**
 * Warning: This header is only to allow backwards compatibility for
 * rolling reboot between 5.4.4.0 and 5.4.4.1
 * Do not change any of these fields.
 */
typedef struct _cmsg_old_header_s
{
    cmsg_old_msg_type msg_type;     // Do NOT change this field
    uint32_t header_length;         // Do NOT change this field
    uint32_t message_length;        // Do NOT change this field
    uint32_t method_index;          // Only for METHOD_xxx
    uint32_t status_code;           // Only for METHOD_REPLY - unused
} cmsg_old_header;

cmsg_old_header cmsg_old_header_create (cmsg_old_msg_type msg_type, uint32_t packed_size,
                                        uint32_t method_index,
                                        uint32_t status_code);

int cmsg_service_port_get (const char *name, const char *proto);

#endif
