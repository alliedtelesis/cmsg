#ifndef __CMSG_H_
#define __CMSG_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <time.h>
#include <glib.h>
#include <netdb.h>
#include <syslog.h>
#include <sys/select.h>
#include <sys/time.h>
#include <string.h>

#ifndef LOCAL_INSTALL
#include <utility/tracelog.h>
#else
#define tracelog(tracelog_string, fmt, ...)   printf(fmt, ## __VA_ARGS__);
#endif

#include "protobuf-c.h"

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
#define CMSG_LOG_DEBUG(fmt, ...)                                                    \
    do {                                                                            \
        printf ("DEBUG(CMSG):%s %u: " fmt, __FUNCTION__, __LINE__, ## __VA_ARGS__); \
    } while (0)
#else
#define CMSG_LOG_DEBUG(fmt, ...)                                                    \
    do {                                                                            \
        syslog (LOG_DEBUG, "DEBUG(CMSG):%s %u: " fmt, __FUNCTION__, __LINE__, ## __VA_ARGS__); \
    } while (0)
#endif

#if defined(DEBUG_WORKSTATION)
#define CMSG_DEBUG(level, fmt, ARGS...)                                                 \
    do {                                                                                \
        if ((level) <= DEBUG_LEVEL)                                                     \
            printf ("%s:%d "fmt, __FUNCTION__, __LINE__, ##ARGS);                       \
    } while (0)
#elif defined(DEBUG_SWITCH)
#define CMSG_DEBUG(level, fmt, ARGS...)                                                 \
    do {                                                                                \
        if ((level) <= DEBUG_LEVEL)                                                     \
            syslog (LOG_CRIT | LOG_LOCAL6, "%s:%d "fmt, __FUNCTION__, __LINE__, ##ARGS);\
    } while (0)
#elif defined(DEBUG_DISABLED)
#define CMSG_DEBUG(ARGS...)  /* do nothing */
#endif

#define CMSG_RECV_BUFFER_SZ                        512
#define CMSG_TRANSPORT_TIPC_PUB_CONNECT_TIMEOUT    3000  //ms
#define CMSG_TRANSPORT_CLIENT_SEND_TRIES           10

// Return codes
#define CMSG_RET_OK   0
#define CMSG_RET_ERR -1
#define CMSG_RET_METHOD_NOT_FOUND -2
#define CMSG_RET_QUEUED 1
#define CMSG_RET_DROPPED 2

#define CMSG_TLV_SIZE(x) ((2 * sizeof (uint32_t)) + (x))

#define UNDEFINED_METHOD 0xffffffff

#define IS_METHOD_DEFINED(x)  (x == UNDEFINED_METHOD ? FALSE : TRUE)

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


typedef enum _cmsg_object_type_e
{
    CMSG_OBJ_TYPE_NONE,
    CMSG_OBJ_TYPE_CLIENT,
    CMSG_OBJ_TYPE_SERVER,
    CMSG_OBJ_TYPE_PUB,
    CMSG_OBJ_TYPE_SUB,
} cmsg_object_type;

#define CMSG_MAX_OBJ_ID_LEN 10

typedef struct _cmsg_object_s
{
    cmsg_object_type object_type;
    void *object;
    char obj_id[CMSG_MAX_OBJ_ID_LEN + 1];
} cmsg_object;

typedef enum _cmsg_msg_type_e
{
    CMSG_MSG_TYPE_METHOD_REQ = 0,   // Request to server to call a method
    CMSG_MSG_TYPE_METHOD_REPLY,     // Reply from server in response to a method request
    CMSG_MSG_TYPE_ECHO_REQ,         // Request to server for a reply - used for a ping/healthcheck
    CMSG_MSG_TYPE_ECHO_REPLY,       // Reply from server in response to an echo request
} cmsg_msg_type;

typedef enum _cmsg_status_code_e
{
    CMSG_STATUS_CODE_UNSET,
    CMSG_STATUS_CODE_SUCCESS,
    CMSG_STATUS_CODE_SERVICE_FAILED,
    CMSG_STATUS_CODE_TOO_MANY_PENDING,
    CMSG_STATUS_CODE_SERVICE_QUEUED,
    CMSG_STATUS_CODE_SERVICE_DROPPED,
    CMSG_STATUS_CODE_SERVER_CONNRESET,
    CMSG_STATUS_CODE_SERVER_METHOD_NOT_FOUND,
} cmsg_status_code;

/**
 * Warning: This header is only to allow backwards compatibility for
 * rolling reboot between 5.4.4.0 and 5.4.4.1
 * Do not change any of these fields.
 */
typedef struct _cmsg_old_header_s
{
    cmsg_msg_type msg_type;         // Do NOT change this field
    uint32_t header_length;         // Do NOT change this field
    uint32_t message_length;        // Do NOT change this field
    uint32_t method_index;          // Only for METHOD_xxx
    cmsg_status_code status_code;   // Only for METHOD_REPLY
} cmsg_old_header;

/**
 * WARNING: Changing this header in anyway will break ISSU for this release.
 * Consider whether or not it would be better to add any new fields as a TLV header,
 * like the method header 'cmsg_tlv_method_header'. If you do need to change
 * this header, _everyone_ will need to be made aware that ISSU won't work
 * to go "up to" or "down from" the first release that includes this change.
 */
typedef struct _cmsg_header_s
{
    cmsg_msg_type msg_type;         // Do NOT change this field
    uint32_t header_length;         // Do NOT change this field
    uint32_t message_length;        // Do NOT change this field
    cmsg_status_code status_code;   // Only for METHOD_REPLY
} cmsg_header;

typedef enum _cmsg_tlv_header_type_s
{
    CMSG_TLV_METHOD_TYPE,
} cmsg_tlv_header_type;

typedef struct cmsg_tlv_method_header_s
{
    cmsg_tlv_header_type type;
    uint32_t method_length;
    char method[0];
} cmsg_tlv_method_header;

typedef struct cmsg_tlv_header_s
{
    cmsg_tlv_header_type type;
    uint32_t tlv_value_length;
} cmsg_tlv_header;

typedef enum _cmsg_method_processing_reason_e
{
    CMSG_METHOD_OK_TO_INVOKE,
    CMSG_METHOD_QUEUED,
    CMSG_METHOD_DROPPED,
    CMSG_METHOD_INVOKING_FROM_QUEUE
} cmsg_method_processing_reason;

typedef enum _cmsg_error_code_e
{
    CMSG_ERROR_CODE_HOST_NOT_FOUND,
    CMSG_ERROR_CODE_CONNECTION_REFUSED,
    CMSG_ERROR_CODE_CLIENT_TERMINATED,
    CMSG_ERROR_CODE_BAD_REQUEST,
    CMSG_ERROR_CODE_PROXY_PROBLEM
} cmsg_error_code;

typedef enum _cmsg_queue_state_e
{
    CMSG_QUEUE_STATE_ENABLED,
    CMSG_QUEUE_STATE_TO_DISABLED,
    CMSG_QUEUE_STATE_DISABLED,
} cmsg_queue_state;

typedef enum _cmsg_queue_filter_type_e
{
    CMSG_QUEUE_FILTER_PROCESS,
    CMSG_QUEUE_FILTER_DROP,
    CMSG_QUEUE_FILTER_QUEUE,
    CMSG_QUEUE_FILTER_ERROR,
} cmsg_queue_filter_type;

typedef struct _cmsg_server_request_s
{
    cmsg_msg_type msg_type;
    uint32_t message_length;
    uint32_t method_index;
    char method_name_recvd[128];
} cmsg_server_request;

uint32_t cmsg_common_uint32_to_le (uint32_t le);

#define cmsg_common_uint32_from_le cmsg_common_uint32_to_le

void cmsg_buffer_print (void *buffer, uint32_t size);

cmsg_header cmsg_header_create (cmsg_msg_type msg_type, uint32_t extra_header_size,
                                uint32_t packed_size, cmsg_status_code status_code);
cmsg_old_header cmsg_old_header_create (cmsg_msg_type msg_type, uint32_t packed_size,
                                        uint32_t method_index,
                                        cmsg_status_code status_code);

void cmsg_tlv_method_header_create (uint8_t *buf, cmsg_header header, uint32_t type,
                                    uint32_t length, const char *method_name);

int32_t cmsg_header_process (cmsg_header *header_received, cmsg_header *header_converted);

int
cmsg_tlv_header_process (uint8_t *buf, cmsg_server_request *server_request,
                         uint32_t extra_header_size,
                         const ProtobufCServiceDescriptor *descriptor);

int cmsg_service_port_get (const char *name, const char *proto);

#define CMSG_MALLOC(size)           cmsg_malloc ((size), __FILE__, __LINE__)
#define CMSG_CALLOC(nmemb,size)     cmsg_calloc ((nmemb), (size), __FILE__,  __LINE__)
#define CMSG_FREE(ptr)              cmsg_free ((ptr),  __FILE__,  __LINE__)
void *cmsg_malloc (size_t size, const char *filename, int line);
void *cmsg_calloc (size_t nmemb, size_t size, const char *filename, int line);
void cmsg_free (void *ptr, const char *filename, int line);
void cmsg_malloc_init (int mtype);

#ifdef HAVE_CMSG_PROFILING
typedef struct _cmsg_prof_s
{
    int enable;
    FILE *file_ptr;
    struct timeval start;
    struct timeval start_tic;
    struct timeval now;
    char text[2048];
    char *text_ptr;
} cmsg_prof;

uint32_t cmsg_prof_diff_time_in_us (struct timeval start, struct timeval end);
void cmsg_prof_time_tic (cmsg_prof *perf);
uint32_t cmsg_prof_time_toc (cmsg_prof *perf);
void cmsg_prof_time_log_start (cmsg_prof *perf, char *filename);
void cmsg_prof_time_log_add_time (cmsg_prof *perf, char *description, uint32_t time);
void cmsg_prof_time_log_stop (cmsg_prof *perf, char *type, int msg_size);
void cmsg_prof_enable (cmsg_prof *perf);
void cmsg_prof_disable (cmsg_prof *perf);
#define CMSG_PROF_TIME_TIC(ARGS...) cmsg_prof_time_tic(ARGS)
#define CMSG_PROF_TIME_TOC(ARGS...) cmsg_prof_time_toc(ARGS)
#define CMSG_PROF_TIME_LOG_START(ARGS...) cmsg_prof_time_log_start(ARGS)
#define CMSG_PROF_TIME_LOG_ADD_TIME(ARGS...) cmsg_prof_time_log_add_time(ARGS)
#define CMSG_PROF_TIME_LOG_STOP(ARGS...) cmsg_prof_time_log_stop(ARGS)
#define CMSG_PROF_ENABLE(ARGS...) cmsg_prof_enable(ARGS)
#define CMSG_PROF_DISABLE(ARGS...) cmsg_prof_disable(ARGS)
#else //ifndef HAVE_CMSG_PROFILING
#define CMSG_PROF_TIME_TIC(ARGS...)
#define CMSG_PROF_TIME_TOC(ARGS...)
#define CMSG_PROF_TIME_LOG_START(ARGS...)
#define CMSG_PROF_TIME_LOG_ADD_TIME(ARGS...)
#define CMSG_PROF_TIME_LOG_STOP(ARGS...)
#define CMSG_PROF_ENABLE(ARGS...)
#define CMSG_PROF_DISABLE(ARGS...)
#endif //HAVE_CMSG_PROFILING

#endif
