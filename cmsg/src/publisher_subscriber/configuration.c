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

static cmsg_server *server = NULL;

/**
 * Configures the IP address of the CMSG server running in this daemon
 * for syncing to remote hosts.
 */
void
cmsg_psd_configuration_impl_address_set (const void *service, const cmsg_uint32 *recv_msg)
{
    struct in_addr addr = { };

    addr.s_addr = recv_msg->value;

    remote_sync_address_set (addr);
    cmsg_psd_configuration_server_address_setSend (service);
}

/**
 * Registers a new subscription.
 */
void
cmsg_psd_configuration_impl_add_subscription (const void *service,
                                              const cmsg_subscription_info *recv_msg)
{
    if (data_add_subscription (recv_msg))
    {
        /* The memory of the message was stolen so do not free the message. */
        cmsg_server_app_owns_current_msg_set (server);
    }
    cmsg_psd_configuration_server_add_subscriptionSend (service);
}

/**
 * Unregisters an existing subscription.
 */
void
cmsg_psd_configuration_impl_remove_subscription (const void *service,
                                                 const cmsg_subscription_info *recv_msg)
{
    data_remove_subscription (recv_msg);
    cmsg_psd_configuration_server_remove_subscriptionSend (service);
}

/**
 * Unregisters all subscriptions for a given subscriber.
 */
void
cmsg_psd_configuration_impl_remove_subscriber (const void *service,
                                               const cmsg_transport_info *recv_msg)
{
    data_remove_subscriber (recv_msg);
    cmsg_psd_configuration_server_remove_subscriberSend (service);
}

/**
 * Registers a new publisher with cmsg_psd and returns the methods that currently
 * have subscriptions for the service the publisher is publishing for.
 */
void
cmsg_psd_configuration_impl_add_publisher (const void *service,
                                           const cmsg_service_info *recv_msg)
{
    cmsg_subscription_methods send_msg = CMSG_SUBSCRIPTION_METHODS_INIT;

    data_add_publisher (recv_msg->service, recv_msg->server_info);

    data_get_subscription_info_for_service (recv_msg->service, &send_msg);

    cmsg_psd_configuration_server_add_publisherSend (service, &send_msg);
    data_get_subscription_info_for_service_free (&send_msg);
}

/**
 * Unregisters a publisher from cmsg_psd.
 */
void
cmsg_psd_configuration_impl_remove_publisher (const void *service,
                                              const cmsg_service_info *recv_msg)
{
    data_remove_publisher (recv_msg->service, recv_msg->server_info);

    cmsg_psd_configuration_server_remove_publisherSend (service);
}


/**
 * Initialise the configuration functionality.
 */
void
configuration_server_init (void)
{
    /* The server must be synchronous (i.e. RPC/two-way communication) as subscribers
     * expect that once they subscribe they should receive all events that are then
     * published. */
    server = cmsg_glib_unix_server_init (CMSG_SERVICE (cmsg_psd, configuration));
    if (!server)
    {
        syslog (LOG_ERR, "Failed to initialize configuration server");
    }
}
