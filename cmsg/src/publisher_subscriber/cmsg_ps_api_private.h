/**
 * cmsg_ps_api_private.h
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_PS_API_PRIVATE_H_
#define __CMSG_PS_API_PRIVATE_H_

#include <stdbool.h>
#include "cmsg_server.h"
#include "cmsg_client.h"

bool cmsg_ps_subscription_add_local (cmsg_server *sub_server, const char *method_name);
bool cmsg_ps_subscription_add_remote (cmsg_server *sub_server, const char *method_name,
                                      struct in_addr remote_addr);
bool cmsg_ps_subscription_remove_local (cmsg_server *sub_server, const char *method_name);
bool cmsg_ps_subscription_remove_remote (cmsg_server *sub_server, const char *method_name,
                                         struct in_addr remote_addr);
bool cmsg_ps_remove_subscriber (cmsg_server *sub_server);
cmsg_client *cmsg_ps_create_publisher_client (void);
bool cmsg_ps_publish_message (cmsg_client *client, const char *service, const char *method,
                              uint8_t *packet, uint32_t packet_len);

#endif /* __CMSG_PS_API_PRIVATE_H_ */
