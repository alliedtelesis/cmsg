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
#include "cmsg_sl.h"

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
cmsg_pthread_multithreaded_server_info *cmsg_pthread_multithreaded_server_init (cmsg_server
                                                                                *server,
                                                                                uint32_t
                                                                                timeout);
void cmsg_pthread_multithreaded_server_destroy (cmsg_pthread_multithreaded_server_info
                                                *info);

bool cmsg_pthread_service_listener_listen (pthread_t *thread, const char *service_name,
                                           cmsg_sl_event_handler_t handler,
                                           void *user_data);

#endif /* __CMSG_PTHREAD_HELPERS_H_ */
