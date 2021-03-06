/*
 * Unit tests for cmsg.c
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include <np.h>
#include <cmsg.h>
#include "cmsg_unit_tests_types_auto.h"

#define BOOL_VALUE true
#define UINT32_VALUE 123456
#define STRING_VALUE "This is a test message"
#define INTERNAL_UINT32_VALUE 987654
#define ARRAY_SIZE 4
static uint32_t uint32_array[ARRAY_SIZE] = { 123, 234, 0, 345 };

#define TEST_FILE_NAME "/tmp/dump_file"

void
test_cmsg_dump_msg_to_file (void)
{
    cmsg_dump_msg dump_msg = CMSG_DUMP_MSG_INIT;
    cmsg_internal_dump_msg internal_dump_msg = CMSG_INTERNAL_DUMP_MSG_INIT;
    cmsg_dump_msg *read_data_msg = NULL;
    int i;

    CMSG_SET_FIELD_VALUE (&internal_dump_msg, internal_uint32_value, INTERNAL_UINT32_VALUE);

    CMSG_SET_FIELD_VALUE (&dump_msg, bool_value, BOOL_VALUE);
    CMSG_SET_FIELD_VALUE (&dump_msg, uint32_value, UINT32_VALUE);
    CMSG_SET_FIELD_PTR (&dump_msg, string_value, STRING_VALUE);
    CMSG_SET_FIELD_REPEATED (&dump_msg, uint32_array, uint32_array, ARRAY_SIZE);
    CMSG_SET_FIELD_PTR (&dump_msg, internal_message, &internal_dump_msg);

    NP_ASSERT_EQUAL (cmsg_dump_msg_to_file ((struct ProtobufCMessage *) &dump_msg,
                                            TEST_FILE_NAME), CMSG_RET_OK);

    read_data_msg =
        (cmsg_dump_msg *) cmsg_get_msg_from_file (CMSG_MSG_DESCRIPTOR (cmsg_dump_msg),
                                                  TEST_FILE_NAME);
    NP_ASSERT_NOT_NULL (read_data_msg);

    NP_ASSERT_EQUAL (read_data_msg->bool_value, BOOL_VALUE);
    NP_ASSERT_EQUAL (read_data_msg->uint32_value, UINT32_VALUE);
    NP_ASSERT_STR_EQUAL (read_data_msg->string_value, STRING_VALUE);

    for (i = 0; i < ARRAY_SIZE; i++)
    {
        NP_ASSERT_EQUAL (read_data_msg->uint32_array[i], uint32_array[i]);
    }

    NP_ASSERT_EQUAL (read_data_msg->internal_message->internal_uint32_value,
                     INTERNAL_UINT32_VALUE);

    CMSG_FREE_RECV_MSG (read_data_msg);
}

/**
 * Test that CMSG_REPEATED_FOREACH_INT works as advertised.
 */
void
test_cmsg_repeated_foreach_int (void)
{
    cmsg_dump_msg dump_msg = CMSG_DUMP_MSG_INIT;
    cmsg_dump_msg *dump_msg_ptr = &dump_msg;
    int i;
    int loop_counter = 0;
    uint32_t node;

    CMSG_SET_FIELD_REPEATED (dump_msg_ptr, uint32_array, uint32_array, ARRAY_SIZE);

    CMSG_REPEATED_FOREACH_INT (dump_msg_ptr, uint32_array, node, i)
    {
        NP_ASSERT_EQUAL (i, loop_counter);
        NP_ASSERT_EQUAL (node, dump_msg_ptr->uint32_array[loop_counter]);
        loop_counter++;
    }
    NP_ASSERT_EQUAL (loop_counter, ARRAY_SIZE);
}

/**
 * Test that cmsg_enum_to_name() works as expected.
 */
void
test_cmsg_enum_to_name (void)
{
    const ProtobufCEnumDescriptor *desc = CMSG_ENUM_DESCRIPTOR (cmsg_number);

    NP_ASSERT_STR_EQUAL (cmsg_enum_to_name (desc, CMSG_NUMBER_ZERO), "NUMBER_ZERO");
    NP_ASSERT_STR_EQUAL (cmsg_enum_to_name (desc, CMSG_NUMBER_ONE), "NUMBER_ONE");
    NP_ASSERT_STR_EQUAL (cmsg_enum_to_name (desc, CMSG_NUMBER_TWO), "NUMBER_TWO");
    NP_ASSERT_STR_EQUAL (cmsg_enum_to_name (desc, CMSG_NUMBER_2), "NUMBER_TWO");
    NP_ASSERT_STR_EQUAL (cmsg_enum_to_name (desc, CMSG_NUMBER_MINUS_ONE),
                         "NUMBER_MINUS_ONE");
    NP_ASSERT_STR_EQUAL (cmsg_enum_to_name (desc, CMSG_NUMBER__2), "NUMBER__2");
    NP_ASSERT_STR_EQUAL (cmsg_enum_to_name (desc, CMSG_NUMBER_MINUS_TWO), "NUMBER__2");
    NP_ASSERT_STR_EQUAL (cmsg_enum_to_name (desc, 10), "NUMBER_TEN");
    NP_ASSERT_STR_EQUAL (cmsg_enum_to_name (desc, -10), "NUMBER_MINUS_TEN");
    NP_ASSERT_NULL (cmsg_enum_to_name (desc, 3));
    NP_ASSERT_NULL (cmsg_enum_to_name (desc, 200));
    NP_ASSERT_NULL (cmsg_enum_to_name (desc, -3));
    NP_ASSERT_NULL (cmsg_enum_to_name (desc, -200));
}
