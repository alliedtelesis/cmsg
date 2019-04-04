/**
 * cmsg_pub.h
 *
 * Header file for the CMSG publisher functionality.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_PUB_H_
#define __CMSG_PUB_H_

#include <cmsg/cmsg.h>

typedef struct cmsg_publisher cmsg_publisher;

cmsg_publisher *cmsg_publisher_create (const ProtobufCServiceDescriptor *service);
void cmsg_publisher_destroy (cmsg_publisher *publisher);

#endif /* __CMSG_PUB_H_ */
