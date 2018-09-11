/**
 * cmsg_validation.c
 *
 * The validation functions used by the validation extension.
 *
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */

#include "cmsg_private.h"
#include "cmsg_validation.h"
#include <arpa/inet.h>
#include <inttypes.h>

/**
 * Validate an integer value is greater than or equal to the specified value.
 *
 * @param input_value - The integer value in the input message field.
 * @param compare_value - The value specified in the validation definition to compare against.
 * @param field_name - The name of the field in the message containing the integer value.
 * @param supplied_error_message - Prints a custom error message if supplied.
 * @param err_str - Pointer to a char array to store an error message, this may be NULL.
 * @param err_str_len - The length of the array to store the error message in.
 */
bool
cmsg_validate_int_ge (int64_t input_value, int64_t compare_value,
                      const char *field_name, const char *supplied_error_message,
                      char *err_str, uint32_t err_str_len)
{
    if (input_value < compare_value)
    {
        if (err_str)
        {
            if (supplied_error_message)
            {
                snprintf (err_str, err_str_len, supplied_error_message);
            }
            else
            {
                snprintf (err_str, err_str_len,
                          "Field '%s' must be greater than or equal to %" PRId64 ".",
                          field_name, compare_value);
            }
        }
        return false;
    }

    return true;
}

/**
 * Validate an integer value is less than or equal to the specified value.
 *
 * @param input_value - The integer value in the input message field.
 * @param compare_value - The value specified in the validation definition to compare against.
 * @param field_name - The name of the field in the message containing the integer value.
 * @param supplied_error_message - Prints a custom error message if supplied.
 * @param err_str - Pointer to a char array to store an error message, this may be NULL.
 * @param err_str_len - The length of the array to store the error message in.
 */
bool
cmsg_validate_int_le (int64_t input_value, int64_t compare_value,
                      const char *field_name, const char *supplied_error_message,
                      char *err_str, uint32_t err_str_len)
{
    if (input_value > compare_value)
    {
        if (err_str)
        {
            if (supplied_error_message)
            {
                snprintf (err_str, err_str_len, supplied_error_message);
            }
            else
            {
                snprintf (err_str, err_str_len,
                          "Field '%s' must be less than or equal to %" PRId64 ".",
                          field_name, compare_value);
            }
        }
        return false;
    }

    return true;
}

/**
 * Validate a string is in IP address format (either IPv4 or IPv6).
 *
 * @param input_string - The string in the input message field.
 * @param field_name - The name of the field in the message containing the string.
 * @param supplied_error_message - Prints a custom error message if supplied.
 * @param err_str - Pointer to a char array to store an error message, this may be NULL.
 * @param err_str_len - The length of the array to store the error message in.
 */
bool
cmsg_validate_ip_address (const char *input_string, const char *field_name,
                          const char *supplied_error_message,
                          char *err_str, uint32_t err_str_len)
{
    struct in_addr ipv4;
    struct in6_addr ipv6;

    if ((inet_pton (AF_INET, input_string, &ipv4) != 1) &&
        (inet_pton (AF_INET6, input_string, &ipv6) != 1))
    {
        if (err_str)
        {
            if (supplied_error_message)
            {
                snprintf (err_str, err_str_len, supplied_error_message);
            }
            else
            {
                snprintf (err_str, err_str_len, "Field '%s' must be in IP address format.",
                          field_name);
            }
        }
        return false;
    }

    return true;
}
