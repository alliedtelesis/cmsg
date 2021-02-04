/**
 * cmsg_broadcast_client.h
 *
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_BROADCAST_CLIENT_H_
#define __CMSG_BROADCAST_CLIENT_H_

#include "cmsg.h"
#include "cmsg_client.h"

typedef void (*cmsg_broadcast_event_handler_t) (struct in_addr node_addr, bool joined);

cmsg_client *cmsg_broadcast_client_new (const ProtobufCServiceDescriptor *descriptor,
                                        const char *service_entry_name,
                                        struct in_addr my_node_addr,
                                        bool connect_to_self, bool oneway,
                                        cmsg_broadcast_event_handler_t event_handler);
void cmsg_broadcast_client_destroy (cmsg_client *client);

int32_t cmsg_broadcast_client_add_loopback (cmsg_client *broadcast_client,
                                            cmsg_client *loopback_client);
int32_t cmsg_broadcast_client_add_unix (cmsg_client *_broadcast_client,
                                        cmsg_client *unix_client);

int cmsg_broadcast_client_get_event_fd (cmsg_client *_broadcast_client);
void cmsg_broadcast_event_queue_process (cmsg_client *_broadcast_client);

#endif /* __CMSG_BROADCAST_CLIENT_H_ */
