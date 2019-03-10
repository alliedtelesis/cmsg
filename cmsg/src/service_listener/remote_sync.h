/**
 * remote_sync.h
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __REMOTE_SYNC_H_
#define __REMOTE_SYNC_H_

void remote_sync_address_set (struct in_addr addr);
void remote_sync_add_host (struct in_addr addr);
void remote_sync_delete_host (struct in_addr addr);

#endif /* __REMOTE_SYNC_H_ */
