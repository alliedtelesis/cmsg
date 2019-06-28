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
#include "cmsg_types_auto.h"

int32_t cmsg_ps_subscription_add_local (cmsg_server *sub_server, const char *method_name);
int32_t cmsg_ps_subscription_add_remote (cmsg_server *sub_server, const char *method_name,
                                         struct in_addr remote_addr);
int32_t cmsg_ps_subscription_remove_local (cmsg_server *sub_server,
                                           const char *method_name);
int32_t cmsg_ps_subscription_remove_remote (cmsg_server *sub_server,
                                            const char *method_name,
                                            struct in_addr remote_addr);
int32_t cmsg_ps_remove_subscriber (cmsg_server *sub_server);
cmsg_server *cmsg_ps_create_publisher_update_server (void);
int32_t cmsg_ps_register_publisher (const char *service, cmsg_server *server,
                                    cmsg_subscription_methods **subscribed_methods);
int32_t cmsg_ps_deregister_publisher (const char *service, cmsg_server *server);

#endif /* __CMSG_PS_API_PRIVATE_H_ */
