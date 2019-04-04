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
    cmsg_server *pub_server;    //receiving messages
    cmsg_server_accept_thread_info *pub_server_thread_info;
};

#endif /* __CMSG_SUB_PRIVATE_H_ */
