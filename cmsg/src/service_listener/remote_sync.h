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

void remote_sync_debug_dump (FILE *fp);
void remote_sync_address_set (struct in_addr addr);
void remote_sync_add_host (struct in_addr addr);
void remote_sync_delete_host (struct in_addr addr);
void remote_sync_server_added (const cmsg_service_info *server_info);
void remote_sync_server_removed (const cmsg_service_info *server_info);

#endif /* __REMOTE_SYNC_H_ */
