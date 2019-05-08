/**
 * cmsg_sub_private.h
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_SUB_PRIVATE_H_
#define __CMSG_SUB_PRIVATE_H_

#include "cmsg_server.h"

struct cmsg_subscriber
{
    cmsg_server *local_server;  /* The unix server used for local subscriptions */
    cmsg_server *remote_server; /* The tcp server used for remote subscriptions */
};

#endif /* __CMSG_SUB_PRIVATE_H_ */
