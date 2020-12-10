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

typedef struct _cmsg_broadcast_client_event
{
    /* Node ID of the node that has joined/left the broadcast client. */
    uint32_t node_id;

    /* true if the given node has joined the broadcast client, false if
     * it has left. */
    bool joined;
} cmsg_broadcast_client_event;

typedef struct _cmsg_broadcast_client_event_queue
{
    /* Queue to store node join/leave events. This is used to
     * pass the new socket descriptors back to the server user. */
    GAsyncQueue *queue;

    /* An eventfd object to notify the listener that there is a new
     * event on the queue.  */
    int eventfd;

    /* Function to call on each event */
    cmsg_broadcast_event_handler_t handler;
} cmsg_broadcast_client_event_queue;

typedef struct _cmsg_broadcast_client_s
{
    cmsg_composite_client base_client;

    /* Whether to use oneway or RPC child clients */
    bool oneway_children;

    /* The name of the service in the /etc/services file */
    const char *service_entry_name;

    /* The TIPC node id of this node */
    uint32_t my_node_id;

    /* The IP address of this node */
    struct in_addr my_node_addr;

    /* The range of TIPC node ids we are listening for */
    uint32_t lower_node_id;
    uint32_t upper_node_id;

    /* Connect to the TIPC server running on this node if it exists */
    bool connect_to_self;

    /* Thread for monitoring the TIPC topology and creating clients as required */
    pthread_t topology_thread;

    /* Queue for storing node join/leave events to the broadcast client */
    cmsg_broadcast_client_event_queue event_queue;
} cmsg_broadcast_client;

int32_t cmsg_broadcast_conn_mgmt_init (cmsg_broadcast_client *broadcast_client);
void cmsg_broadcast_conn_mgmt_deinit (cmsg_broadcast_client *broadcast_client);

#endif /* __CMSG_BROADCAST_PRIVATE_H_ */
