/*
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */
#ifndef __CMSG_PTHREAD_HELPERS_H_
#define __CMSG_PTHREAD_HELPERS_H_

#include <stdbool.h>
#include <pthread.h>
#include "cmsg_server.h"
#include "cmsg_sub.h"
#include "cmsg_pub.h"

bool cmsg_pthread_server_init (pthread_t *thread, cmsg_server *server);
cmsg_sub *cmsg_pthread_unix_subscriber_init (pthread_t *thread,
                                             const ProtobufCService *service,
                                             const char **events);
cmsg_pub *cmsg_pthread_unix_publisher_init (pthread_t *thread,
                                            const ProtobufCServiceDescriptor *service_desc);

#endif /* __CMSG_PTHREAD_HELPERS_H_ */
