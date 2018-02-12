/**
 * cmsg_broadcast_client_private.h
 *
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_BROADCAST_PRIVATE_H_
#define __CMSG_BROADCAST_PRIVATE_H_

#include "cmsg.h"
#include "../cmsg_composite_client_private.h"
#include "cmsg_broadcast_client.h"

typedef struct _cmsg_broadcast_client_s
{
    cmsg_composite_client base_client;

    /* Whether to use oneway or RPC child clients */
    bool oneway_children;

    /* The name of the service in the /etc/services file */
    const char *service_entry_name;

    /* The TIPC node id of this node */
    uint32_t my_node_id;

    /* The range of TIPC node ids we are listening for */
    uint32_t lower_node_id;
    uint32_t upper_node_id;

    /* Connect to the TIPC server running on this node if it exists */
    bool connect_to_self;

    /* Socket descriptor for the TIPC topology events service */
    int tipc_subscription_sd;

    /* Thread for monitoring the TIPC topology and creating clients as required */
    pthread_t topology_thread;
} cmsg_broadcast_client;

int32_t cmsg_broadcast_conn_mgmt_init (cmsg_broadcast_client *broadcast_client);
void cmsg_broadcast_conn_mgmt_deinit (cmsg_broadcast_client *broadcast_client);

#endif /* __CMSG_BROADCAST_PRIVATE_H_ */
