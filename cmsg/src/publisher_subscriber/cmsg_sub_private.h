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
    cmsg_server *unix_server;
    cmsg_server_accept_thread_info *unix_server_thread_info;
    cmsg_server *tcp_server;
    cmsg_server_accept_thread_info *tcp_server_thread_info;
};

#endif /* __CMSG_SUB_PRIVATE_H_ */
