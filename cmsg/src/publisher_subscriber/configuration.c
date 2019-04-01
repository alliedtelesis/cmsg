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
#include "data.h"

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
 * Registers a new subscription.
 */
void
cmsg_pssd_configuration_impl_add_subscription (const void *service,
                                               const cmsg_pssd_subscription_info *recv_msg)
{
    data_add_subscription (recv_msg);
    cmsg_pssd_configuration_server_add_subscriptionSend (service);
}

/**
 * Unregisters an existing subscription.
 */
void
cmsg_pssd_configuration_impl_remove_subscription (const void *service,
                                                  const cmsg_pssd_subscription_info
                                                  *recv_msg)
{
    data_remove_subscription (recv_msg);
    cmsg_pssd_configuration_server_remove_subscriptionSend (service);
}

/**
 * Unregisters all subscriptions for a given subscriber.
 */
void
cmsg_pssd_configuration_impl_remove_subscriber (const void *service,
                                                const cmsg_transport_info *recv_msg)
{
    data_remove_subscriber (recv_msg);
    cmsg_pssd_configuration_server_remove_subscriberSend (service);
}

/**
 * Initialise the configuration functionality.
 */
void
configuration_server_init (void)
{
    cmsg_server *server = NULL;

    server = cmsg_create_server_unix_oneway (CMSG_SERVICE (cmsg_pssd, configuration));
    if (!server)
    {
        syslog (LOG_ERR, "Failed to initialize configuration server");
        return;
    }

    info = cmsg_glib_server_init (server);
    if (!info)
    {
        syslog (LOG_ERR, "Failed to initialize configuration server");
    }
}
