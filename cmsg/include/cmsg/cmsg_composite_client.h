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
cmsg_client *cmsg_composite_client_lookup_by_tcp_ipv4_addr (cmsg_client *_composite_client,
                                                            struct in_addr addr);
cmsg_client *cmsg_composite_client_lookup_by_tcp_ipv6_addr (cmsg_client *_composite_client,
                                                            struct in6_addr *addr);
cmsg_client *cmsg_composite_client_lookup_by_transport (cmsg_client *_composite_client,
                                                        const cmsg_transport *transport);
int cmsg_composite_client_num_children (cmsg_client *_composite_client);
GList *cmsg_composite_client_get_children (cmsg_client *_composite_client);
void cmsg_composite_client_free_all_children (cmsg_client *_composite_client);
void cmsg_composite_client_destroy_full (cmsg_client *_composite_client);

#endif /* __CMSG_COMPOSITE_CLIENT_H_ */
