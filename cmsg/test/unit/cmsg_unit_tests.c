/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Unit tests for cmsg.c
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include <np.h>
#include <cmsg.h>
#include "cmsg_private.h"
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

/**
 * Build a wire-format (network byte order) cmsg_header from host values.
 */
static cmsg_header
make_wire_header (cmsg_msg_type msg_type, uint32_t header_length,
                  uint32_t message_length, cmsg_status_code status_code)
{
    cmsg_header header;

    header.msg_type = (cmsg_msg_type) htonl ((uint32_t) msg_type);
    header.header_length = htonl (header_length);
    header.message_length = htonl (message_length);
    header.status_code = (cmsg_status_code) htonl ((uint32_t) status_code);

    return header;
}

/**
 * A well formed header is accepted and converted to host byte order.
 */
void
test_cmsg_header_process_valid (void)
{
    cmsg_header wire = make_wire_header (CMSG_MSG_TYPE_METHOD_REQ,
                                         sizeof (cmsg_header) + 24, 512,
                                         CMSG_STATUS_CODE_SUCCESS);
    cmsg_header converted;

    NP_ASSERT_EQUAL (cmsg_header_process (&wire, &converted), CMSG_RET_OK);
    NP_ASSERT_EQUAL (converted.msg_type, CMSG_MSG_TYPE_METHOD_REQ);
    NP_ASSERT_EQUAL (converted.header_length, sizeof (cmsg_header) + 24);
    NP_ASSERT_EQUAL (converted.message_length, 512);
    NP_ASSERT_EQUAL (converted.status_code, CMSG_STATUS_CODE_SUCCESS);
}

/**
 * A header with no TLV region (header_length == sizeof (cmsg_header)) is the
 * smallest legal value and must be accepted.
 */
void
test_cmsg_header_process_min_header_length (void)
{
    cmsg_header wire = make_wire_header (CMSG_MSG_TYPE_METHOD_REQ,
                                         sizeof (cmsg_header), 0,
                                         CMSG_STATUS_CODE_UNSET);
    cmsg_header converted;

    NP_ASSERT_EQUAL (cmsg_header_process (&wire, &converted), CMSG_RET_OK);
    NP_ASSERT_EQUAL (converted.header_length, sizeof (cmsg_header));
}

/**
 * An unknown msg_type is rejected.
 */
void
test_cmsg_header_process_bad_msg_type (void)
{
    cmsg_header wire = make_wire_header ((cmsg_msg_type) 0xdeadbeef,
                                         sizeof (cmsg_header), 0,
                                         CMSG_STATUS_CODE_UNSET);
    cmsg_header converted;

    np_syslog_match ("Processing header, bad msg type value", 0);

    NP_ASSERT_EQUAL (cmsg_header_process (&wire, &converted), CMSG_RET_ERR);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);
}

/**
 * A header_length below sizeof (cmsg_header) is rejected. A receiver derives
 * the TLV region size as header_length - sizeof (cmsg_header); without this
 * check that subtraction underflows and the TLV parser reads past the buffer.
 */
void
test_cmsg_header_process_short_header_length (void)
{
    cmsg_header converted;
    uint32_t bad_lengths[] = { 0, 1, sizeof (cmsg_header) - 1 };
    unsigned int i;

    np_syslog_match ("Processing header, header length", 0);

    for (i = 0; i < sizeof (bad_lengths) / sizeof (bad_lengths[0]); i++)
    {
        cmsg_header wire = make_wire_header (CMSG_MSG_TYPE_METHOD_REQ,
                                             bad_lengths[i], 512,
                                             CMSG_STATUS_CODE_UNSET);
        NP_ASSERT_EQUAL (cmsg_header_process (&wire, &converted), CMSG_RET_ERR);
    }

    NP_ASSERT_EQUAL (np_syslog_count (0), 3);
}

/**
 * A message_length + header_length sum that wraps uint32 is rejected. Without
 * this check a small receive buffer is allocated while the derived TLV region
 * stays large and the parser reads past the buffer.
 */
void
test_cmsg_header_process_length_overflow (void)
{
    cmsg_header wire = make_wire_header (CMSG_MSG_TYPE_METHOD_REQ,
                                         sizeof (cmsg_header),
                                         UINT32_MAX - 8, CMSG_STATUS_CODE_UNSET);
    cmsg_header converted;

    np_syslog_match ("Processing header, message length", 0);

    NP_ASSERT_EQUAL (cmsg_header_process (&wire, &converted), CMSG_RET_ERR);
    NP_ASSERT_EQUAL (np_syslog_count (0), 1);
}

/**
 * A message_length + header_length sum that reaches exactly UINT32_MAX does not
 * wrap and must be accepted.
 */
void
test_cmsg_header_process_length_no_overflow_boundary (void)
{
    cmsg_header wire = make_wire_header (CMSG_MSG_TYPE_METHOD_REQ,
                                         sizeof (cmsg_header),
                                         UINT32_MAX - sizeof (cmsg_header),
                                         CMSG_STATUS_CODE_UNSET);
    cmsg_header converted;

    NP_ASSERT_EQUAL (cmsg_header_process (&wire, &converted), CMSG_RET_OK);
}
