/**
 * remote_sync.h
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __REMOTE_SYNC_H_
#define __REMOTE_SYNC_H_

#include <stdio.h>
#include <netinet/in.h>

void remote_sync_debug_dump (FILE *fp);
void remote_sync_address_set (struct in_addr addr);
bool remote_sync_address_is_set (void);
uint32_t remote_sync_get_local_ip (void);

#endif /* __REMOTE_SYNC_H_ */
