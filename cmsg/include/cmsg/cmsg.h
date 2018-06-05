/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#ifndef __CMSG_H_
#define __CMSG_H_

#include <protobuf-c/protobuf-c.h>
#include <stdbool.h>

// Return codes
#define CMSG_RET_OK                 0
#define CMSG_RET_QUEUED             1
#define CMSG_RET_DROPPED            2
#define CMSG_RET_ERR                -1
#define CMSG_RET_METHOD_NOT_FOUND   -2
#define CMSG_RET_CLOSED             -3

#define CMSG_COUNTER_APP_NAME_PREFIX    "CMSG "

typedef protobuf_c_boolean cmsg_bool_t;

void cmsg_malloc_init (int mtype);

/* note - use CMSG_MSG_ALLOC()/_FREE() instead of calling these directly */
void *cmsg_msg_alloc (size_t struct_size, const char *file, int line);
void cmsg_msg_free (void *msg_struct, const char *file, int line);
/* note - use CMSG_MSG_ARRAY_ALLOC()/_FREE() instead of calling these directly */
void **cmsg_msg_array_alloc (size_t struct_size, uint32_t num_structs,
                             const char *file, int line);
void cmsg_msg_array_free (void *msg_array, const char *file, int line);
/* note - use CMSG_REPEATED_APPEND() instead of calling this directly */
void cmsg_repeated_append (void ***msg_ptr_array, size_t *num_elems, const void *ptr,
                           const char *file, int line);
void cmsg_repeated_append_uint32 (uint32_t **msg_ptr_array, size_t *num_elems,
                                  uint32_t value, const char *file, int line);
void cmsg_repeated_append_int32 (int32_t **msg_ptr_array, size_t *num_elems,
                                 int32_t value, const char *file, int line);
/* note - use CMSG_UPDATE_RECV_MSG_STRING_FIELD() instead of calling this directly */
void cmsg_update_recv_msg_string_field (char **field, const char *new_val,
                                        const char *file, int line);

extern ProtobufCAllocator cmsg_memory_allocator;

// macro to free messages returned back to the API
#define CMSG_FREE_RECV_MSG(_name)                                                                      \
    do {                                                                                               \
        protobuf_c_message_free_unpacked ((ProtobufCMessage *)(_name), &cmsg_memory_allocator);       \
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
        (_name)->has_##_field = true;               \
    } while (0)

#define CMSG_UNSET_AND_ZERO_FIELD_VALUE(_name, _field) \
    do {                                               \
        (_name)->_field = 0;                           \
        (_name)->has_##_field = false;                 \
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

#define CMSG_SET_FIELD_BYTES(_name, _field, _data, _len) \
    do {                                                 \
        (_name)->_field.len = (_len);                    \
        (_name)->_field.data = (_data);                  \
        (_name)->has_##_field = true;                    \
    } while (0)

#define CMSG_SET_FIELD_ONEOF(_name, _field, _ptr,      \
                             _oneof_name, _oneof_type) \
    do {                                               \
        (_name)->_field = (_ptr);                      \
        (_name)->_oneof_name##_case = (_oneof_type);   \
    } while (0)

#define CMSG_SET_FIELD_ONEOF_BYTES(_name, _field, _data, _len, \
                                   _oneof_name, _oneof_type)   \
    do {                                                       \
        (_name)->_field.len = (_len);                          \
        (_name)->_field.data = (_data);                        \
        (_name)->_oneof_name##_case = (_oneof_type);           \
    } while (0)

#define CMSG_IS_FIELD_PRESENT(_msg, _field) \
    ((_msg)->has_##_field ? true : false)

#define CMSG_IS_PTR_PRESENT(_msg, _ptr) \
    ((_msg)->_ptr ? true : false)

#define CMSG_IS_REPEATED_PRESENT(_msg, _field) \
    ((_msg)->n_##_field ? true : false)

#define CMSG_MSG_HAS_FIELD(_msg, _field_name) \
    (protobuf_c_message_descriptor_get_field_by_name ((_msg)->base.descriptor, _field_name) ? true : false)

/**
 * Helper macro to check whether a given message has a field with the
 * given name.
 * @param _msg the message structure to check
 * @param _field_name the field name to be checked
 */
#define CMSG_MSG_HAS_FIELD(_msg, _field_name) \
    (protobuf_c_message_descriptor_get_field_by_name ((_msg)->base.descriptor, _field_name) ? true : false)

/**
 * Helper macro to allocate a message struct using the CMSG memory allocator.
 * @param __msg_struct the name of the message struct being used
 * @return a pointer to the allocated message struct.  The message must still be
 * initialised using the appropriate init function for the message. No sub-fields
 * in the message are allocated. You still need to malloc and free these yourself.
 */
#define CMSG_MSG_ALLOC(__msg_struct) \
    (__msg_struct *) cmsg_msg_alloc (sizeof (__msg_struct), __FILE__, __LINE__)

/**
 * Frees a message struct allocated by CMSG_MSG_ALLOC()
 * @param __msg_ptr Pointer to the message created by CMSG_MSG_ALLOC
 * @note that this does not handle freeing memory for any sub-fields within the
 * message struct (e.g. strings, MAC addresses, etc). You still need to free
 * these yourself before calling this macro. If all fields in the message are allocated
 * using the CMSG allocator, CMSG_FREE_RECV_MSG can be used to free the whole message.
 */
#define CMSG_MSG_FREE(__msg_ptr) \
    cmsg_msg_free (__msg_ptr, __FILE__, __LINE__)

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

/**
 * Frees an array of pointers created by CMSG_REPEATED_APPEND. The contents of the pointers
 * needs to be freed by the user.
 */
#define CMSG_REPEATED_FREE(_ptr_array) CMSG_MSG_ARRAY_FREE (_ptr_array)

/**
 * Helper macro to append an element to a repeated field in a message.
 * If this macro is used, it MUST be the ONLY method used to add elements to that field.
 * This is because internal optimisations are used to avoid excessive re-allocations.
 *
 * This macro MUST NOT be used with CMSG_MSG_ARRAY_ALLOC or any other allocator for the
 * same field.
 *
 * If _ptr is NULL, the field is not updated and the original values are kept.
 *
 * Otherwise, the pointer array is created/extended if necessary, the pointer to the new
 * element is added and the number of elements in the repeated field is updated.
 *
 * Freeing memory used by the elements in the array after the message has been sent is left
 * up to the user.
 * The array itself should be freed with CMSG_REPEATED_FREE.
 * @param _name name of the message being modified
 * @param _field name of the repeated field
 * @param _ptr pointer to append to repeated field
 */
#define CMSG_REPEATED_APPEND(_name, _field, _ptr)                                \
    cmsg_repeated_append ((void ***) &((_name)->_field), &((_name)->n_##_field), \
                          (const void *) _ptr, __FILE__, __LINE__)

#define CMSG_REPEATED_APPEND_UINT32(_name, _field, _value)                                 \
    cmsg_repeated_append_uint32 ((uint32_t **) &((_name)->_field), &((_name)->n_##_field), \
                                 (uint32_t) _value, __FILE__, __LINE__)

#define CMSG_REPEATED_APPEND_INT32(_name, _field, _value)                                 \
    cmsg_repeated_append_int32 ((int32_t **) &((_name)->_field), &((_name)->n_##_field),  \
                                 (int32_t) _value, __FILE__, __LINE__)

/**
 * Helper macro to iterate over the pointers in a repeated field of a CMSG message
 * @param _name name of message ptr variable.
 * @param _field name of the repeated field
 * @param _node pointer of the type of the repeated field.
 * @param _idx integer variable to use as loop counter.
 */
#define CMSG_REPEATED_FOREACH(_name, _field, _node, _idx)  \
    if ((_name) && (_name)->_field)                         \
        for (_idx = 0; _idx < (_name)->n_##_field; _idx++) \
            if ((_node = (_name)->_field[_idx]) != NULL)

/**
 * Replace a string field in a received message with a different value using the
 * CMSG memory allocator.
 * This is useful if the message needs a slight modification before sending on
 * to another destination so that the memory allocation tracing is kept happy,
 * and CMSG_FREE_RECV_MSG can still be used to free the whole message.
 * @param _name name of message ptr variable.
 * @param _field name of the repeated field
 * @param _new_value string to be copied to field.
 */
#define CMSG_UPDATE_RECV_MSG_STRING_FIELD(_name, _field, _new_value) \
    cmsg_update_recv_msg_string_field (&((_name)->_field), _new_value, __FILE__, __LINE__)

int cmsg_service_port_get (const char *name, const char *proto);

const char *cmsg_service_name_get (const ProtobufCServiceDescriptor *descriptor);

#endif /* __CMSG_H_ */
