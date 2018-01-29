/**
 * cmsg_broadcast_client.h
 *
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_BROADCAST_CLIENT_H_
#define __CMSG_BROADCAST_CLIENT_H_

#include "cmsg.h"
#include "cmsg_client.h"
#include "cmsg_server.h"

/**
 * Type of CMSG broadcast client to use.
 *
 * CMSG_BROADCAST_LOCAL_NONE - Messages are not broadcast back to the sending node
 * CMSG_BROADCAST_LOCAL_LOOPBACK - Messages are broadcast back to the sending node via
 *                                 a loopback client (i.e. in the same thread that is sending).
 * CMSG_BROADCAST_LOCAL_TIPC - Messages are broadcast back to the sending node via a
 *                             TIPC client. This assumes the required TIPC server is running
 *                             in a separate thread to the one that is sending.
 */
typedef enum _cmsg_broadcast_local_e
{
    CMSG_BROADCAST_LOCAL_NONE,
    CMSG_BROADCAST_LOCAL_LOOPBACK,
    CMSG_BROADCAST_LOCAL_TIPC,
} cmsg_broadcast_local_type;

cmsg_client *cmsg_broadcast_client_new (ProtobufCService *service,
                                        const char *service_entry_name, uint32_t my_node_id,
                                        uint32_t lower_node_id, uint32_t upper_node_id,
                                        cmsg_broadcast_local_type type, bool oneway);
void cmsg_broadcast_client_destroy (cmsg_client *client);

#endif /* __CMSG_BROADCAST_CLIENT_H_ */
