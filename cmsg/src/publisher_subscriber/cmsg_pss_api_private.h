/**
 * cmsg_pss_api_private.h
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_PSS_API_PRIVATE_H_
#define __CMSG_PSS_API_PRIVATE_H_

bool cmsg_pss_subscription_add (cmsg_server *sub_server, cmsg_transport *pub_transport,
                                const char *method_name);
bool cmsg_pss_subscription_remove (cmsg_server *sub_server, cmsg_transport *pub_transport,
                                   const char *method_name);

#endif /* __CMSG_PSS_API_PRIVATE_H_ */
