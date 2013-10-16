#ifndef __CMSG_H_
#define __CMSG_H_

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <time.h>
#include <glib.h>
#include <syslog.h>
#include <sys/select.h>

#include "protobuf-c.h"
#include "protobuf-c-cmsg-transport.h"

// TODO: Perhaps we can refactor the logic around the debug below
// The logic around when the debug is on/off is a little convoluted.
#define CMSG_ERROR 1
#define CMSG_WARN  2
#define CMSG_INFO  3

//#define DEBUG_WORKSTATION
//#define DEBUG_SWITCH
#define DEBUG_DISABLED

#define DEBUG_BUFFER 0
#define DEBUG_LEVEL  CMSG_ERROR

// Use this for 'expected' user facing friendly errors
#if defined(DEBUG_WORKSTATION)
#define CMSG_LOG_USER_ERROR(fmt, ...)                                                   \
    do {                                                                                \
        printf ("ERR(CMSG):%s %u: " fmt, __FUNCTION__, __LINE__, ## __VA_ARGS__);       \
    } while (0)
#else
#define CMSG_LOG_USER_ERROR(fmt, ...)                                                   \
        do {                                                                            \
        syslog (LOG_ERR | LOG_LOCAL6, "ERR(CMSG):%s %u: " fmt, __FUNCTION__, __LINE__, ## __VA_ARGS__); \
    } while (0)
#endif

#if defined(DEBUG_WORKSTATION)
#define DEBUG(level, fmt, ARGS...)                                                      \
    do {                                                                                \
        if ((level) <= DEBUG_LEVEL)                                                     \
            printf ("%s:%d "fmt, __FUNCTION__, __LINE__, ##ARGS);                       \
    } while (0)
#elif defined(DEBUG_SWITCH)
#define DEBUG(level, fmt, ARGS...)                                                      \
    do {                                                                                \
        if ((level) <= DEBUG_LEVEL)                                                     \
            syslog (LOG_CRIT | LOG_LOCAL6, "%s:%d "fmt, __FUNCTION__, __LINE__, ##ARGS);\
    } while (0)
#elif defined(DEBUG_DISABLED)
#define DEBUG(ARGS...)  /* do nothing */
#endif

#ifdef DEBUG_DISABLED
#define CMSG_ASSERT(E) do { ; } while (0)
#else
#define CMSG_ASSERT(E) (assert (E))
#endif

#define CMSG_RECV_BUFFER_SZ                        512
#define CMSG_TRANSPORT_TIPC_PUB_CONNECT_TIMEOUT    700  //ms
#define CMSG_TRANSPORT_CLIENT_SEND_TRIES           5

// Return codes
#define CMSG_RET_OK   0
#define CMSG_RET_ERR -1

// Protocol has different msg types which reflect which fields are in use:
// METHOD_REQ - client request to the server to invoke a method
// METHOD_REPLY - server reply to a client for a method request
// ECHO_REQ - client asking the server to reply if running
// ECHO_REPLY - server replying to client that it is running

// NOTE: ECHO is used to implement a healthcheck of the server.
// Header is sent big-endian/network byte order.

// The fields involved in the header are:
//    client method request header:
//         msg_type          CMSG_MSG_TYPE_METHOD_REQ
//         header_length     length of this header - may change in the future
//         message_length    length of the msg that has the parameters for the method
//         method_index      index of method to be invoked
//         status_code       NOT USED by request

//    server method reply header:
//         msg_type          CMSG_MSG_TYPE_METHOD_REPLY
//         header_length     length of this header - may change in the future
//         message_length    length of the msg that has the return parameters for the method
//         method_index      index of method that was invoked
//         status_code       whether the method was invoked, queued, dropped or had a
//                           failure (e.g. unknown method index)

//    client echo request header:
//         msg_type          CMSG_MSG_TYPE_ECHO_REQ
//         header_length     length of this header - may change in the future
//         message_length    0 as there are no parameters
//         method_index      0
//         status_code       NOT USED by request

//    server echo reply header:
//         msg_type          CMSG_MSG_TYPE_ECHO_REPLY
//         header_length     length of this header - may change in the future
//         message_length    0 as nothing replied with
//         method_index      0
//         status_code       0

//
//forward declarations
typedef struct _cmsg_client_s cmsg_client;
typedef struct _cmsg_server_s cmsg_server;
typedef struct _cmsg_sub_s cmsg_sub;
typedef struct _cmsg_pub_s cmsg_pub;

typedef struct _cmsg_object_s cmsg_object;
typedef enum _cmsg_object_type_e cmsg_object_type;
typedef enum _cmsg_msg_type_e cmsg_msg_type;
typedef struct _cmsg_sub_header_method_reply_s cmsg_sub_header_method_reply;
typedef struct _cmsg_sub_header_method_req_s cmsg_sub_header_method_req;
typedef struct _cmsg_header_s cmsg_header;
typedef enum _cmsg_status_code_e cmsg_status_code;
typedef enum _cmsg_error_code_e cmsg_error_code;
typedef enum _cmsg_method_processing_reason_e cmsg_method_processing_reason;
typedef enum _cmsg_queue_state_e cmsg_queue_state;

enum _cmsg_object_type_e
{
    CMSG_OBJ_TYPE_NONE,
    CMSG_OBJ_TYPE_CLIENT,
    CMSG_OBJ_TYPE_SERVER,
    CMSG_OBJ_TYPE_PUB,
    CMSG_OBJ_TYPE_SUB,
};

struct _cmsg_object_s
{
    cmsg_object_type object_type;
    void *object;
};

enum _cmsg_msg_type_e
{
    CMSG_MSG_TYPE_METHOD_REQ = 0,  // Request to server to call a method
    CMSG_MSG_TYPE_METHOD_REPLY,  // Reply from server in response to a method request
    CMSG_MSG_TYPE_ECHO_REQ,  // Request to server for a reply - used for a ping/healthcheck
    CMSG_MSG_TYPE_ECHO_REPLY,  // Reply from server in response to an echo request
};

enum _cmsg_status_code_e
{
    CMSG_STATUS_CODE_SUCCESS,
    CMSG_STATUS_CODE_SERVICE_FAILED,
    CMSG_STATUS_CODE_TOO_MANY_PENDING,
    CMSG_STATUS_CODE_SERVICE_QUEUED,
    CMSG_STATUS_CODE_SERVICE_DROPPED,
};

struct _cmsg_header_s
{
    cmsg_msg_type msg_type;  // Do NOT change this field
    uint32_t header_length;  // Do NOT change this field
    uint32_t message_length; // Do NOT change this field
    uint32_t method_index;   // Only for METHOD_xxx
    cmsg_status_code status_code; // Only for METHOD_REPLY
};

enum _cmsg_method_processing_reason_e
{
    CMSG_METHOD_OK_TO_INVOKE,
    CMSG_METHOD_QUEUED,
    CMSG_METHOD_DROPPED,
    CMSG_METHOD_INVOKING_FROM_QUEUE
};

enum _cmsg_error_code_e
{
    CMSG_ERROR_CODE_HOST_NOT_FOUND,
    CMSG_ERROR_CODE_CONNECTION_REFUSED,
    CMSG_ERROR_CODE_CLIENT_TERMINATED,
    CMSG_ERROR_CODE_BAD_REQUEST,
    CMSG_ERROR_CODE_PROXY_PROBLEM
};

enum _cmsg_queue_state_e
{
    CMSG_QUEUE_STATE_ENABLED,
    CMSG_QUEUE_STATE_TO_DISABLED,
    CMSG_QUEUE_STATE_DISABLED,
};

uint32_t cmsg_common_uint32_to_le (uint32_t le);

#define cmsg_common_uint32_from_le cmsg_common_uint32_to_le

void cmsg_buffer_print (void *buffer, unsigned int size);

cmsg_header cmsg_header_create (cmsg_msg_type msg_type, uint32_t packed_size,
                                uint32_t method_index, cmsg_status_code status_code);

int32_t cmsg_header_process (cmsg_header *header_received, cmsg_header *header_converted);

int cmsg_service_port_get (const char *name, const char *proto);

#define CMSG_MALLOC(size)           cmsg_malloc ((size), __FILE__, __LINE__)
#define CMSG_CALLOC(nmemb,size)     cmsg_calloc ((nmemb), (size), __FILE__,  __LINE__)
#define CMSG_FREE(ptr)              cmsg_free ((ptr),  __FILE__,  __LINE__)
void *cmsg_malloc (size_t size, const char *filename, int line);
void *cmsg_calloc (size_t nmemb, size_t size, const char *filename, int line);
void cmsg_free (void *ptr, const char *filename, int line);
void cmsg_malloc_init (int mtype);
#endif
