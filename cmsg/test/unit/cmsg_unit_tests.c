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
#define ARRAY_SIZE 3
static uint32_t uint32_array[ARRAY_SIZE] = { 123, 234, 345 };

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
