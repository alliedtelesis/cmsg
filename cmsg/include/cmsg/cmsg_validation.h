/**
 * cmsg_validation.h
 *
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_VALIDATION_H_
#define __CMSG_VALIDATION_H_

#include <stdint.h>
#include <stdbool.h>

bool cmsg_validate_int_ge (int64_t input_value, int64_t compare_value,
                           const char *field_name,
                           const char *supplied_error_message, char *err_str,
                           uint32_t err_str_len);
bool cmsg_validate_int_le (int64_t input_value, int64_t compare_value,
                           const char *field_name,
                           const char *supplied_error_message, char *err_str,
                           uint32_t err_str_len);
bool cmsg_validate_ip_address (const char *input_string, const char *field_name,
                               const char *supplied_error_message, char *err_str,
                               uint32_t err_str_len);
bool
cmsg_validate_utc_timestamp (const char *input_string, const char *field_name,
                             const char *supplied_error_message,
                             char *err_str, uint32_t err_str_len);

#endif /* __CMSG_VALIDATION_H_ */
