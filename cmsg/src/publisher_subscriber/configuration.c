/**
 * configuration.c
 *
 * Implements the APIs for configuring the service listener daemon.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include <cmsg/cmsg_glib_helpers.h>
#include "configuration_impl_auto.h"
#include "remote_sync.h"

static cmsg_server_accept_thread_info *info = NULL;

/**
 * Configures the IP address of the CMSG server running in this daemon
 * for syncing to remote hosts.
 */
void
cmsg_pssd_configuration_impl_address_set (const void *service, const cmsg_uint32 *recv_msg)
{
    struct in_addr addr = { };

    addr.s_addr = recv_msg->value;

    remote_sync_address_set (addr);
    cmsg_pssd_configuration_server_address_setSend (service);
}

/**
 * Initialise the configuration functionality.
 */
void
configuration_server_init (void)
{
    info = cmsg_glib_unix_server_init (CMSG_SERVICE (cmsg_pssd, configuration));
    if (!info)
    {
        syslog (LOG_ERR, "Failed to initialize configuration server");
    }
}
