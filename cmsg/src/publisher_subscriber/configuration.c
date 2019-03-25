/**
 * configuration.c
 *
 * Implements the APIs for configuring the service listener daemon.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include <cmsg/cmsg_glib_helpers.h>
#include "configuration_impl_auto.h"

static cmsg_server_accept_thread_info *info = NULL;

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
