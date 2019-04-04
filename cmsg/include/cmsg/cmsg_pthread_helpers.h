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

typedef struct _cmsg_pthread_multithreaded_server_info
{
    cmsg_server *server;
    uint32_t timeout;
    int shutdown_eventfd;
    uint32_t num_threads;
    bool exiting;
    pthread_mutex_t lock;
    pthread_cond_t wakeup_cond;
} cmsg_pthread_multithreaded_server_info;

bool cmsg_pthread_server_init (pthread_t *thread, cmsg_server *server);
cmsg_subscriber *cmsg_pthread_unix_subscriber_init (pthread_t *thread,
                                                    const ProtobufCService *service,
                                                    const char **events);
cmsg_subscriber *cmsg_pthread_tipc_subscriber_init (pthread_t *thread,
                                                    const ProtobufCService *service,
                                                    const char **events,
                                                    const char *subscriber_service_name,
                                                    const char *publisher_service_name,
                                                    int this_node_id, int scope,
                                                    struct in_addr remote_addr);

cmsg_pthread_multithreaded_server_info *cmsg_pthread_multithreaded_server_init (cmsg_server
                                                                                *server,
                                                                                uint32_t
                                                                                timeout);
void cmsg_pthread_multithreaded_server_destroy (cmsg_pthread_multithreaded_server_info
                                                *info);

#endif /* __CMSG_PTHREAD_HELPERS_H_ */
