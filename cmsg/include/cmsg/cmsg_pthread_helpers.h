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
cmsg_sub *cmsg_pthread_tipc_subscriber_init (pthread_t *thread,
                                             const ProtobufCService *service,
                                             const char **events,
                                             const char *subscriber_service_name,
                                             const char *publisher_service_name,
                                             int this_node_id, int scope);
cmsg_pub *cmsg_pthread_unix_publisher_init (pthread_t *thread,
                                            const ProtobufCServiceDescriptor *service_desc);
cmsg_pub *cmsg_pthread_tipc_publisher_init (pthread_t *thread,
                                            const ProtobufCServiceDescriptor *service_desc,
                                            const char *publisher_service_name,
                                            int this_node_id, int scope);

#endif /* __CMSG_PTHREAD_HELPERS_H_ */
