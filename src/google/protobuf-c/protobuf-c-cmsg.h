#ifndef __CMSG_H_
#define __CMSG_H_

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <glib.h>

#include "protobuf-c.h"
#include "protobuf-c-cmsg-transport.h"

#define CMSG_ERROR 1
#define CMSG_WARN  2
#define CMSG_INFO  3

//#define DEBUG_WORKSTATION
//#define DEBUG_SWITCH
#define DEBUG_DISABLED

#define DEBUG_BUFFER 0
#define DEBUG_LEVEL  CMSG_ERROR

#if defined DEBUG_WORKSTATION
#define DEBUG(level, ...) (level <= DEBUG_LEVEL) ? printf(__VA_ARGS__) : 0
#elif defined DEBUG_SWITCH
#include <syslog.h>
#define DEBUG(level, fmt, ARGS...) (level <= DEBUG_LEVEL) ? syslog(LOG_CRIT | LOG_LOCAL6, fmt, ##ARGS) : 0
#elif defined DEBUG_DISABLED
#define DEBUG(ARGS...) (0)
#endif

#define RECV_TIMEOUT 10


// Protocol is:
//    client requests with header:
//         method_index              32-bit little-endian
//         message_length            32-bit little-endian
//         request_id                32-bit any-endian
//
//    server responds with header:
//         status_code               32-bit little-endian
//         method_index              32-bit little-endian
//         message_length            32-bit little-endian
//         request_id                32-bit any-endian


typedef enum   _cmsg_parent_type_e      cmsg_parent_type;
typedef struct _cmsg_header_request_s   cmsg_header_request;
typedef struct _cmsg_header_response_s  cmsg_header_response;
typedef enum   _cmsg_status_code_e      cmsg_status_code;
typedef enum   _cmsg_error_code_e       cmsg_error_code;

enum _cmsg_parent_type_e
{
    CMSG_PARENT_TYPE_CLIENT,
    CMSG_PARENT_TYPE_SERVER,
    CMSG_PARENT_TYPE_PUB,
    CMSG_PARENT_TYPE_SUB,
};

struct _cmsg_header_request_s
{
    uint32_t method_index;
    uint32_t message_length;
    uint32_t request_id;
};

struct _cmsg_header_response_s
{
    uint32_t status_code;
    uint32_t method_index;
    uint32_t message_length;
    uint32_t request_id;
};

enum _cmsg_status_code_e
{
    CMSG_STATUS_CODE_SUCCESS,
    CMSG_STATUS_CODE_SERVICE_FAILED,
    CMSG_STATUS_CODE_TOO_MANY_PENDING
};

enum _cmsg_error_code_e
{
    CMSG_ERROR_CODE_HOST_NOT_FOUND,
    CMSG_ERROR_CODE_CONNECTION_REFUSED,
    CMSG_ERROR_CODE_CLIENT_TERMINATED,
    CMSG_ERROR_CODE_BAD_REQUEST,
    CMSG_ERROR_CODE_PROXY_PROBLEM
};

uint32_t
cmsg_common_uint32_to_le (uint32_t le);

#define cmsg_common_uint32_from_le cmsg_common_uint32_to_le

void
cmsg_buffer_print (void *buffer, unsigned int size);

#endif
