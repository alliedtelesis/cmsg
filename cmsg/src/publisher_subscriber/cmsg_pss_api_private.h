/**
 * cmsg_pss_api_private.h
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_PSS_API_PRIVATE_H_
#define __CMSG_PSS_API_PRIVATE_H_

bool cmsg_pss_subscription_add_local (cmsg_server *sub_server, const char *method_name);
bool cmsg_pss_subscription_add_remote (cmsg_server *sub_server, const char *method_name,
                                       struct in_addr remote_addr);
bool cmsg_pss_subscription_remove_local (cmsg_server *sub_server, const char *method_name);
bool cmsg_pss_subscription_remove_remote (cmsg_server *sub_server, const char *method_name,
                                          struct in_addr remote_addr);
bool cmsg_pss_remove_subscriber (cmsg_server *sub_server);

#endif /* __CMSG_PSS_API_PRIVATE_H_ */
