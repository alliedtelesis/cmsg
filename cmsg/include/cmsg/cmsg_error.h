/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#ifndef __CMSG_ERROR_H_
#define __CMSG_ERROR_H_

#include <syslog.h>
#include "cmsg.h"

/* For client object errors */
#define CMSG_LOG_CLIENT_ERROR(client, msg, ...) \
    do { \
       /* if the client doesn't care abut errors, just downgrade them to debug */ \
       if (client->suppress_errors) { \
         CMSG_LOG_OBJ_DEBUG (client, client->_transport, msg, ## __VA_ARGS__); \
       } else { \
         CMSG_LOG_OBJ_ERROR (client, client->_transport, msg, ## __VA_ARGS__); \
       } \
    } while (0)

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

/* For publisher object debug messages */
#define CMSG_LOG_PUBLISHER_DEBUG(publisher, msg, ...) CMSG_LOG_OBJ_DEBUG (publisher, publisher->sub_server->_transport, msg, ## __VA_ARGS__)
/* For client object debug messages */
#define CMSG_LOG_CLIENT_DEBUG(client, msg, ...) CMSG_LOG_OBJ_DEBUG (client, client->_transport, msg, ## __VA_ARGS__)

/* General object debug messages */
#define CMSG_LOG_OBJ_DEBUG(obj, tport, msg, ...) \
    do { \
        if (obj) { \
            syslog (LOG_DEBUG | LOG_LOCAL7, "CMSG(%d).%s%s: " msg, __LINE__, obj->self.obj_id, tport ? (tport)->tport_id : "", ## __VA_ARGS__); \
        } \
    } while (0)

/* User this error for general messages */
#define CMSG_LOG_GEN_ERROR(msg, ...)  syslog (LOG_ERR | LOG_LOCAL6, "CMSG(%d): " msg, __LINE__, ## __VA_ARGS__)

#define CMSG_LOG_GEN_INFO(msg, ...)  syslog (LOG_INFO | LOG_LOCAL6, "CMSG(%d): " msg, __LINE__, ## __VA_ARGS__)

/* These errors are intended to assert preconditions.  Failure should be unexpected. */
#define CMSG_ASSERT_RETURN_VAL(cond, retval) \
    do { \
       if (!(cond)) { \
           syslog (LOG_ERR | LOG_LOCAL7, "CMSG(%s:%d): Condition failed: " #cond, __FUNCTION__, __LINE__); \
           return (retval); \
       } \
    } while (0)


#define CMSG_ASSERT_RETURN_VOID(cond) \
    do { \
       if (!(cond)) { \
           syslog (LOG_ERR | LOG_LOCAL7, "CMSG(%s:%d): Condition failed: " #cond, __FUNCTION__, __LINE__); \
           return; \
       } \
    } while (0)
#endif /* __CMSG_ERROR_H_ */
