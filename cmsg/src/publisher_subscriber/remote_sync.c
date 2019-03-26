/**
 * remote_sync.c
 *
 * Implements the functionality for syncing the subscriptions between
 * the daemons running on multiple remote hosts.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include "remote_sync_api_auto.h"
#include "remote_sync_impl_auto.h"
#include "remote_sync.h"

/**
 * Create the CMSG server for remote daemons to connect to and
 * sync their local subscriptions to.
 *
 * @param addr - The address to use for the CMSG server.
 */
void
remote_sync_address_set (struct in_addr addr)
{
    /* todo */
}
