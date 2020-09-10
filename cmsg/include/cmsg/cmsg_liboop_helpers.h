/*
 * Copyright 2020, Allied Telesis Labs New Zealand, Ltd
 */
#ifndef __CMSG_LIBOOP_HELPERS_H_
#define __CMSG_LIBOOP_HELPERS_H_

#include <cmsg/cmsg.h>
#include <cmsg/cmsg_server.h>

cmsg_server *cmsg_liboop_unix_server_init (ProtobufCService *service);
void cmsg_liboop_server_destroy (cmsg_server *server);


#endif /* __CMSG_LIBOOP_HELPERS_H_ */
