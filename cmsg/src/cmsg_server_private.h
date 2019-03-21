/**
 * cmsg_server_private.h
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_SERVER_PRIVATE_H_
#define __CMSG_SERVER_PRIVATE_H_

#include "cmsg_types_auto.h"
#include "cmsg_server.h"
#include "cmsg_transport.h"

cmsg_server *cmsg_server_create (cmsg_transport *transport, ProtobufCService *service);
cmsg_service_info *cmsg_server_service_info_create (cmsg_server *server);
void cmsg_server_service_info_free (cmsg_service_info *info);

#endif /* __CMSG_SERVER_PRIVATE_H_ */
