/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#ifndef __CMSG_COMPOSITE_CLIENT_H_
#define __CMSG_COMPOSITE_CLIENT_H_

#include "cmsg_client.h"

int32_t cmsg_composite_client_add_child (cmsg_client *composite_client,
                                         cmsg_client *client);
int32_t cmsg_composite_client_delete_child (cmsg_client *composite_client,
                                            cmsg_client *client);
cmsg_client *cmsg_composite_client_new (const ProtobufCServiceDescriptor *descriptor);

cmsg_client *cmsg_composite_client_lookup_by_tipc_id (cmsg_client *composite_client,
                                                      uint32_t id);

#endif /* __CMSG_COMPOSITE_CLIENT_H_ */
