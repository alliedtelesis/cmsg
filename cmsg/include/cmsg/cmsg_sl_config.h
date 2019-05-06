/**
 * cmsg_sl_config.h
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_SL_CONFIG_H_
#define __CMSG_SL_CONFIG_H_

#include <stdbool.h>
#include <netinet/in.h>

int32_t cmsg_service_listener_address_set (struct in_addr addr);
int32_t cmsg_service_listener_add_host (struct in_addr addr);
int32_t cmsg_service_listener_delete_host (struct in_addr addr);

#endif /* __CMSG_SL_CONFIG_H_ */
