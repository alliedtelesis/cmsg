/**
 * cmsg_sl_api_private.h
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_SL_API_PRIVATE_H_
#define __CMSG_SL_API_PRIVATE_H_

#include "cmsg_server.h"

void cmsg_service_listener_add_server (cmsg_server *server);
void cmsg_service_listener_remove_server (cmsg_server *server);

#endif /* __CMSG_SL_API_PRIVATE_H_ */
