/**
 * cmsg_pub_private.h
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_PUB_PRIVATE_H_
#define __CMSG_PUB_PRIVATE_H_

#include "cmsg_server.h"
#include "cmsg_client.h"

typedef struct
{
    char *method_name;
    cmsg_client *comp_client;   /* Client to subscribers of this method */
} subscribed_method_entry;

struct cmsg_publisher
{
    //this is a hack to get around a check when a client method is called
    //to not change the order of the first two
    const ProtobufCServiceDescriptor *descriptor;
    int32_t (*invoke) (ProtobufCService *service,
                       uint32_t method_index,
                       const ProtobufCMessage *input,
                       ProtobufCClosure closure, void *closure_data);
    cmsg_client *client;
    cmsg_object self;
    cmsg_object parent;

    GHashTable *subscribed_methods;
    pthread_mutex_t subscribed_methods_mutex;

    cmsg_server *update_server;
    pthread_t update_thread;
    bool update_thread_running;
};

#endif /* __CMSG_PUB_PRIVATE_H_ */
