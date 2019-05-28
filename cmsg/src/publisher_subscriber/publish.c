/**
 * publish.c
 *
 * Implements the functionality for publishing events to the
 * interested subscribers.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include <cmsg/cmsg_glib_helpers.h>
#include "publish_impl_auto.h"
#include "publish.h"
#include "data.h"

static cmsg_server *server = NULL;

/**
 * Publishes a CMSG packet for a specific service and method to all subscribers.
 */
void
cmsg_psd_publish_impl_send_data (const void *service, const cmsg_psd_publish_data *recv_msg)
{
    data_publish_message (recv_msg->service, recv_msg->method_name, recv_msg->packet.data,
                          recv_msg->packet.len);
    cmsg_psd_publish_server_send_dataSend (service);
}

/**
 * Initialise the publish functionality.
 */
void
publish_server_init (void)
{
    server = cmsg_create_server_unix_oneway (CMSG_SERVICE (cmsg_psd, publish));
    if (!server)
    {
        syslog (LOG_ERR, "Failed to initialize publish server");
        return;
    }

    if (cmsg_glib_server_init (server) != CMSG_RET_OK)
    {
        syslog (LOG_ERR, "Failed to initialize publish server");
    }
}
