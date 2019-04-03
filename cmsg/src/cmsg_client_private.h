/**
 * cmsg_client_private.h
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_CLIENT_PRIVATE_H_
#define __CMSG_CLIENT_PRIVATE_H_

#include <cmsg/cmsg_client.h>

cmsg_client *cmsg_client_create (cmsg_transport *transport,
                                 const ProtobufCServiceDescriptor *descriptor);
int32_t cmsg_client_send_bytes (cmsg_client *client, uint8_t *buffer, uint32_t buffer_len);

#endif /* __CMSG_CLIENT_PRIVATE_H_ */
