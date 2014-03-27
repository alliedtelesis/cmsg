#ifndef __PROTOBUF_C_CMSG_ERROR__
#define __PROTOBUF_C_CMSG_ERROR__

#include <syslog.h>
#include "protobuf-c-cmsg.h"

/* For client object errors */
#define CMSG_LOG_CLIENT_ERROR(client, msg, ...)  CMSG_LOG_OBJ_ERROR (client, client->_transport, msg, ## __VA_ARGS__)

/* For server object errors */
#define CMSG_LOG_SERVER_ERROR(server, msg, ...) CMSG_LOG_OBJ_ERROR (server, server->_transport, msg, ## __VA_ARGS__)

/* For publisher object errors */
#define CMSG_LOG_PUBLISHER_ERROR(publisher, msg, ...) CMSG_LOG_OBJ_ERROR (publisher, publisher->sub_server->_transport, msg, ## __VA_ARGS__)

/* General object errors */
#define CMSG_LOG_OBJ_ERROR(obj, tport, msg, ...) \
    do { \
       if (obj) { \
         syslog (LOG_ERR | LOG_LOCAL6, "CMSG(%d).%s%s: " msg, __LINE__, obj->self.obj_id, tport ? (tport)->tport_id : "", ## __VA_ARGS__); \
       } \
    } while (0)

/* User this error for general messages */
#define CMSG_LOG_GEN_ERROR(msg, ...)  syslog (LOG_ERR | LOG_LOCAL6, "CMSG(%d): " msg, __LINE__, ## __VA_ARGS__)

#endif /* __PROTOBUF_C_CMSG_ERROR__ */
