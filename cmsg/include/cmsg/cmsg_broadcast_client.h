/**
 * cmsg_broadcast_client.h
 *
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_BROADCAST_CLIENT_H_
#define __CMSG_BROADCAST_CLIENT_H_

#include "cmsg.h"
#include "cmsg_client.h"

typedef void (*cmsg_broadcast_event_handler_t) (uint32_t node_id, bool joined);

cmsg_client *cmsg_broadcast_client_new (const ProtobufCServiceDescriptor *descriptor,
                                        const char *service_entry_name,
                                        uint32_t my_node_id, uint32_t lower_node_id,
                                        uint32_t upper_node_id,
                                        bool connect_to_self, bool oneway,
                                        cmsg_broadcast_event_handler_t event_handler);
cmsg_client *cmsg_broadcast_client_new_tcp (const ProtobufCServiceDescriptor *descriptor,
                                            const char *service_entry_name,
                                            struct in_addr my_node_addr,
                                            bool connect_to_self, bool oneway);
void cmsg_broadcast_client_destroy (cmsg_client *client);

int32_t cmsg_broadcast_client_add_loopback (cmsg_client *broadcast_client,
                                            cmsg_client *loopback_client);

int cmsg_broadcast_client_get_event_fd (cmsg_client *_broadcast_client);
void cmsg_broadcast_event_queue_process (cmsg_client *_broadcast_client);

#endif /* __CMSG_BROADCAST_CLIENT_H_ */
