/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * cmsg_pub_private.h
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_PUB_PRIVATE_H_
#define __CMSG_PUB_PRIVATE_H_

#include "cmsg_server.h"
#include "cmsg_client.h"

struct cmsg_publisher
{
    //this is a hack to get around a check when a client method is called
    //to not change the order of the first two
    const ProtobufCServiceDescriptor *descriptor;
    void (*invoke) (ProtobufCService *service,
                    uint32_t method_index,
                    const ProtobufCMessage *input,
                    ProtobufCClosure closure, void *closure_data);
    cmsg_object self;
    cmsg_object parent;

    GHashTable *subscribed_methods;
    pthread_mutex_t subscribed_methods_mutex;

    pthread_mutex_t send_queue_mutex;
    GQueue *send_queue;
    pthread_cond_t send_queue_process_cond;
    pthread_t send_thread;
    bool send_thread_running;

    cmsg_server *update_server;
    pthread_t update_thread;
    bool update_thread_running;
};

cmsg_client *cmsg_publisher_get_client_for_method (struct cmsg_publisher *publisher,
                                                   const char *method_name, bool create);

#endif /* __CMSG_PUB_PRIVATE_H_ */
