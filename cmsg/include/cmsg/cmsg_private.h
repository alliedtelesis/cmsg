/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#ifndef __CMSG_PRIVATE_H_
#define __CMSG_PRIVATE_H_

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <time.h>
#include <netdb.h>
#include <syslog.h>
#include <sys/select.h>
#include <sys/time.h>
#include <string.h>
#include <utility/tracelog.h>
#include <glib.h>
#include <protobuf-c/protobuf-c.h>

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
#define CMSG_DEBUG(ARGS...) /* do nothing */
#endif

#define CMSG_RECV_BUFFER_SZ                        512
#define CMSG_TRANSPORT_CLIENT_SEND_TRIES           10
#define CMSG_SERVER_REQUEST_MAX_NAME_LENGTH        128

#define CMSG_TLV_SIZE(x) ((2 * sizeof (uint32_t)) + (x))

typedef enum _cmsg_object_type_e
{
    CMSG_OBJ_TYPE_NONE,
    CMSG_OBJ_TYPE_CLIENT,
    CMSG_OBJ_TYPE_SERVER,
    CMSG_OBJ_TYPE_PUB,
    CMSG_OBJ_TYPE_SUB,
    CMSG_OBJ_TYPE_COMPOSITE_CLIENT,
} cmsg_object_type;

#define CMSG_MAX_OBJ_ID_LEN 10

typedef struct _cmsg_object_s
{
    cmsg_object_type object_type;
    void *object;
    char obj_id[CMSG_MAX_OBJ_ID_LEN + 1];
} cmsg_object;

// Protocol has different msg types which reflect which fields are in use:
// METHOD_REQ - client request to the server to invoke a method
// METHOD_REPLY - server reply to a client for a method request
// ECHO_REQ - client asking the server to reply if running
// ECHO_REPLY - server replying to client that it is running
// CONN_OPEN - client request to open the connection

// NOTE: ECHO is used to implement a healthcheck of the server.
// Header is sent big-endian/network byte order.
// NOTE: CONN_OPEN is unused but previously was used to signify a pkt that
// is being sent that the client needed to.  No response is to be sent.

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

//    client connection open header:
//         msg_type          CMSG_MSG_TYPE_CONN_OPEN
//         header_length     length of this header - may change in the future
//         message_length    0 as nothing else is sent
//         method_index      0
//         status_code       0

typedef enum _cmsg_msg_type_e
{
    CMSG_MSG_TYPE_METHOD_REQ = 0,   // Request to server to call a method
    CMSG_MSG_TYPE_METHOD_REPLY,     // Reply from server in response to a method request
    CMSG_MSG_TYPE_ECHO_REQ,         // Request to server for a reply - used for a ping/healthcheck
    CMSG_MSG_TYPE_ECHO_REPLY,       // Reply from server in response to an echo request
    CMSG_MSG_TYPE_CONN_OPEN,        // Request from client to open the connection (unused)
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
    CMSG_STATUS_CODE_CONNECTION_CLOSED,
} cmsg_status_code;

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
    char method_name_recvd[CMSG_SERVER_REQUEST_MAX_NAME_LENGTH];
} cmsg_server_request;

void cmsg_buffer_print (void *buffer, uint32_t size);

cmsg_header cmsg_header_create (cmsg_msg_type msg_type, uint32_t extra_header_size,
                                uint32_t packed_size, cmsg_status_code status_code);

void cmsg_tlv_method_header_create (uint8_t *buf, cmsg_header header, uint32_t type,
                                    uint32_t length, const char *method_name);

int32_t cmsg_header_process (cmsg_header *header_received, cmsg_header *header_converted);

int
cmsg_tlv_header_process (uint8_t *buf, cmsg_server_request *server_request,
                         uint32_t extra_header_size,
                         const ProtobufCServiceDescriptor *descriptor);

#define CMSG_MALLOC(size)           cmsg_malloc ((size), __FILE__, __LINE__)
#define CMSG_CALLOC(nmemb,size)     cmsg_calloc ((nmemb), (size), __FILE__,  __LINE__)
#define CMSG_ASPRINTF(strp,fmt,...) cmsg_asprintf (__FILE__,  __LINE__, (strp),\
                                                   (fmt), ##__VA_ARGS__)
#define CMSG_STRDUP(strp) cmsg_strdup ((strp), __FILE__,  __LINE__)
#define CMSG_FREE(ptr)              _cmsg_free ((ptr),  __FILE__,  __LINE__)
void *cmsg_malloc (size_t size, const char *filename, int line);
void *cmsg_calloc (size_t nmemb, size_t size, const char *filename, int line);
int cmsg_asprintf (const char *filename, int line, char **strp, const char *fmt, ...);
char *cmsg_strdup (const char *strp, const char *filename, int line);
void _cmsg_free (void *ptr, const char *filename, int line);
void cmsg_free (void *ptr);
void cmsg_malloc_init (int mtype);

#ifdef HAVE_COUNTERD
#define CMSG_COUNTER_INC(x, t) cntrd_app_inc_ctr(x->cntr_session, x->t)
#else
#define CMSG_COUNTER_INC(x, t)
#endif //HAVE_COUNTERD

#define CMSG_BC_CLIENT_PREFIX  "cmbc:"
#define CMSG_SERVER_PREFIX     "cmsr:"
#define CMSG_PUBLISHER_PREFIX  "cmpb:"
#define CMSG_ACCEPT_PREFIX     "cmat:"
void cmsg_pthread_setname (pthread_t thread, const char *cmsg_name, const char *prefix);
#endif /* __CMSG_PRIVATE_H_ */
