/**
 * cmsg_ps_config.h
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_PS_CONFIG_H_
#define __CMSG_PS_CONFIG_H_

#include <stdbool.h>
#include <netinet/in.h>

int32_t cmsg_ps_address_set (struct in_addr addr);

#endif /* __CMSG_PS_CONFIG_H_ */
