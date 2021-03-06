/**
 * remote_sync.h
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __REMOTE_SYNC_H_
#define __REMOTE_SYNC_H_

#include <stdio.h>
#include <netinet/in.h>
#include "cmsg_types_auto.h"
#include <cmsg/cmsg_transport.h>
#include <cmsg/cmsg_client.h>

void remote_sync_debug_dump (FILE *fp);
void remote_sync_address_set (struct in_addr addr);
uint32_t remote_sync_get_local_ip (void);
void remote_sync_subscription_added (const cmsg_subscription_info *subscriber_info);
void remote_sync_subscription_removed (const cmsg_subscription_info *subscriber_info);
bool remote_sync_sl_event_handler (const cmsg_transport *transport, bool added,
                                   void *user_data);
void remote_sync_bulk_sync_subscriptions (cmsg_client *client);
#endif /* __REMOTE_SYNC_H_ */
