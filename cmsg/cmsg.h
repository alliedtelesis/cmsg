#ifndef __CMSG_H_
#define __CMSG_H_

#include "protobuf-c.h"

// Return codes
#define CMSG_RET_OK                 0
#define CMSG_RET_QUEUED             1
#define CMSG_RET_DROPPED            2
#define CMSG_RET_ERR                -1
#define CMSG_RET_METHOD_NOT_FOUND   -2
#define CMSG_RET_CLOSED             -3

#define CMSG_COUNTER_APP_NAME_PREFIX    "CMSG "

void cmsg_malloc_init (int mtype);

/* note - use CMSG_MSG_ARRAY_ALLOC()/_FREE() instead of calling these directly */
void **cmsg_msg_array_alloc (size_t struct_size, uint32_t num_structs,
                             const char *file, int line);
void cmsg_msg_array_free (void *msg_array, const char *file, int line);

// macro to free messages returned back to the API
#define CMSG_FREE_RECV_MSG(_name)                                                                      \
    do {                                                                                               \
        protobuf_c_message_free_unpacked ((ProtobufCMessage *)(_name), &protobuf_c_default_allocator); \
        (_name) = NULL;                                                                                \
    } while (0)

#define CMSG_FREE_RECV_MSG_ARRAY(_array)                                                               \
    do {                                                                                               \
        int _i;                                                                                         \
        for (_i = 0; _array[_i] != NULL; _i++)                                                     \
        {                                                                                              \
            CMSG_FREE_RECV_MSG(_array[_i]);                                                            \
        }                                                                                              \
    } while (0)


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

/**
 * Helper macro to allocate an array of message structs used to send a CMSG
 * message. This is designed to be used with CMSG_SET_FIELD_REPEATED(), where
 * the repeated field is a message struct.
 * @param __msg_struct the name of the message struct being used
 * @param __num the number of message structs we need to allocate
 * @return a single block of malloc'd memory, which is an array of pointers setup
 * to point to the message structs. Use CMSG_MSG_ARRAY_FREE() to free this memory
 * @note that this does not handle mallocing memory for any sub-fields within
 * the message struct (e.g. strings, MAC addresses, etc). You still need to malloc
 * and free these yourself.
 */
#define CMSG_MSG_ARRAY_ALLOC(__msg_struct, __num) \
    (__msg_struct **) cmsg_msg_array_alloc (sizeof (__msg_struct), __num, \
                                            __FILE__, __LINE__)

/**
 * Frees a message array allocated by CMSG_MSG_ARRAY_ALLOC()
 * @note that this does not handle freeing memory for any sub-fields within the
 * message struct (e.g. strings, MAC addresses, etc). You still need to free
 * these yourself before calling this macro.
 */
#define CMSG_MSG_ARRAY_FREE(__msg_array) \
    cmsg_msg_array_free (__msg_array, __FILE__, __LINE__)

int cmsg_service_port_get (const char *name, const char *proto);

#endif
