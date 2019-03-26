/**
 * cmsg_pss_config.h
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_PSS_CONFIG_H_
#define __CMSG_PSS_CONFIG_H_

#include <stdbool.h>
#include <netinet/in.h>

bool cmsg_pss_address_set (struct in_addr addr);

#endif /* __CMSG_PSS_CONFIG_H_ */
