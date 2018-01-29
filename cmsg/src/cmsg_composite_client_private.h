/**
 * cmsg_composite_client_private.h
 *
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_COMPOSITE_CLIENT_PRIVATE_H_
#define __CMSG_COMPOSITE_CLIENT_PRIVATE_H_

#include "cmsg_client.h"

typedef struct _cmsg_composite_client_s
{
    cmsg_client base_client;

    // composite client information
    GList *child_clients;
    pthread_mutex_t child_mutex;
} cmsg_composite_client;

bool cmsg_composite_client_init (cmsg_composite_client *comp_client,
                                 const ProtobufCServiceDescriptor *descriptor);
void cmsg_composite_client_deinit (cmsg_composite_client *comp_client);

#endif /* __CMSG_COMPOSITE_CLIENT_PRIVATE_H_ */
