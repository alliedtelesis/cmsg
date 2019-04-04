/**
 * cmsg_pss_api.c
 *
 * Implements the functions that can be used to interact with the publisher
 * subscriber storage daemon.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include "configuration_api_auto.h"
#include "cmsg_pss_config.h"
#include "cmsg_pss_api_private.h"
#include "transport/cmsg_transport_private.h"

static struct in_addr local_addr;

/**
 * Configure the IP address of the server running in cmsg_pssd.
 * This is the address that remote hosts can connect to.
 *
 * @param addr - The address to configure.
 *
 * @returns true on success, false on error.
 */
bool
cmsg_pss_address_set (struct in_addr addr)
{
    cmsg_client *client = NULL;
    cmsg_uint32 send_msg = CMSG_UINT32_INIT;
    int ret;

    client = cmsg_create_client_unix_oneway (CMSG_DESCRIPTOR (cmsg_pssd, configuration));
    if (!client)
    {
        return false;
    }

    CMSG_SET_FIELD_VALUE (&send_msg, value, addr.s_addr);
    local_addr.s_addr = addr.s_addr;

    ret = cmsg_pssd_configuration_api_address_set (client, &send_msg);
    cmsg_destroy_client_and_transport (client);

    return (ret == CMSG_RET_OK);
}

/**
 * Helper function for calling the required API to cmsg_pssd to register/unregister
 * the subscription.
 *
 * @param sub_server - The CMSG server structure used by the subscriber to receive
 *                     published notifications.
 * @param pub_transport - The transport to connect to the publisher with.
 * @param method_name - The method name to subscribe for.
 * @param add - Whether we are adding the subscription or removing it.
 *
 * @returns true on success, false otherwise.
 */
static bool
cmsg_pss_subscription_add_remove (cmsg_server *sub_server, const char *method_name,
                                  bool add, bool remote, uint32_t remote_addr)
{
    cmsg_client *client = NULL;
    int ret;
    cmsg_transport_info *transport_info = NULL;
    cmsg_subscription_info send_msg = CMSG_SUBSCRIPTION_INFO_INIT;

    transport_info = cmsg_transport_info_create (sub_server->_transport);
    if (!transport_info)
    {
        return false;
    }
    CMSG_SET_FIELD_PTR (&send_msg, transport_info, transport_info);
    CMSG_SET_FIELD_PTR (&send_msg, service,
                        (char *) cmsg_service_name_get (sub_server->service->descriptor));
    CMSG_SET_FIELD_PTR (&send_msg, method_name, (char *) method_name);

    if (remote)
    {
        CMSG_SET_FIELD_VALUE (&send_msg, remote_addr, remote_addr);
    }

    client = cmsg_create_client_unix_oneway (CMSG_DESCRIPTOR (cmsg_pssd, configuration));
    if (!client)
    {
        cmsg_transport_info_free (transport_info);
        return false;
    }

    if (add)
    {
        ret = cmsg_pssd_configuration_api_add_subscription (client, &send_msg);
    }
    else
    {
        ret = cmsg_pssd_configuration_api_remove_subscription (client, &send_msg);
    }

    cmsg_destroy_client_and_transport (client);
    cmsg_transport_info_free (transport_info);

    return (ret == CMSG_RET_OK);
}

/**
 * Register a local subscription with cmsg_pssd.
 *
 * @param sub_server - The CMSG server structure used by the subscriber to receive
 *                     published notifications.
 * @param method_name - The method name to subscribe for.
 *
 * @returns true on success, false otherwise.
 */
bool
cmsg_pss_subscription_add_local (cmsg_server *sub_server, const char *method_name)
{
    return cmsg_pss_subscription_add_remove (sub_server, method_name, true, false, 0);

}

/**
 * Register a remote subscription with cmsg_pssd.
 *
 * @param sub_server - The CMSG server structure used by the subscriber to receive
 *                     published notifications.
 * @param method_name - The method name to subscribe for.
 * @param remote_addr - The address of the remote node to subscribe to.
 *
 * @returns true on success, false otherwise.
 */
bool
cmsg_pss_subscription_add_remote (cmsg_server *sub_server, const char *method_name,
                                  struct in_addr remote_addr)
{
    return cmsg_pss_subscription_add_remove (sub_server, method_name, true, true,
                                             remote_addr.s_addr);
}

/**
 * Unregister a local subscription from cmsg_pssd.
 *
 * @param sub_server - The CMSG server structure used by the subscriber to receive
 *                     published notifications.
 * @param method_name - The method name to subscribe for.
 *
 * @returns true on success, false otherwise.
 */
bool
cmsg_pss_subscription_remove_local (cmsg_server *sub_server, const char *method_name)
{
    return cmsg_pss_subscription_add_remove (sub_server, method_name, false, false, 0);
}

/**
 * Unregister a remote subscription from cmsg_pssd.
 *
 * @param sub_server - The CMSG server structure used by the subscriber to receive
 *                     published notifications.
 * @param method_name - The method name to subscribe for.
 * @param remote_addr - The address of the remote node to unsubscribe from.
 *
 * @returns true on success, false otherwise.
 */
bool
cmsg_pss_subscription_remove_remote (cmsg_server *sub_server, const char *method_name,
                                     struct in_addr remote_addr)
{
    return cmsg_pss_subscription_add_remove (sub_server, method_name, false, true,
                                             remote_addr.s_addr);
}

/**
 * Unregister a subscriber from cmsg_pssd. This will remove all subscriptions for the
 * given subscriber.
 *
 * @param sub_server - The CMSG server structure used by the subscriber to receive
 *                     published notifications.
 *
 * @returns true on success, false otherwise.
 */
bool
cmsg_pss_remove_subscriber (cmsg_server *sub_server)
{
    cmsg_client *client = NULL;
    int ret;
    cmsg_transport_info *transport_info = NULL;

    transport_info = cmsg_transport_info_create (sub_server->_transport);
    if (!transport_info)
    {
        return false;
    }

    client = cmsg_create_client_unix_oneway (CMSG_DESCRIPTOR (cmsg_pssd, configuration));
    if (!client)
    {
        cmsg_transport_info_free (transport_info);
        return false;
    }

    ret = cmsg_pssd_configuration_api_remove_subscriber (client, transport_info);

    cmsg_destroy_client_and_transport (client);
    cmsg_transport_info_free (transport_info);

    return (ret == CMSG_RET_OK);
}

/**
 * Create the client that can be used by a cmsg publisher to send messages
 * for publishing by cmsg_pssd.
 *
 * This client must be freed by the caller using 'cmsg_destroy_client_and_transport'.
 *
 * @returns A pointer to a client that can be used to send messages to cmsg_pssd on success,
 *          NULL otherwise.
 */
cmsg_client *
cmsg_pss_create_publisher_client (void)
{
    return cmsg_create_client_unix_oneway (CMSG_DESCRIPTOR (cmsg_pssd, configuration));
}

/**
 * Send a packet to cmsg_pssd so that it can be sent to all interested subscribers.
 *
 * @param client - The client connected to cmsg_pssd (previously returned by a call to
 *                 'cmsg_pss_create_publisher_client')
 * @param service - The service the packet is for.
 * @param method - The method the packet is for.
 * @param packet - The packet to send.
 * @param packet_len - The length of the packet.
 *
 * @returns true if the packet was successfully sent to cmsg_pssd, false otherwise.
 */
bool
cmsg_pss_publish_message (cmsg_client *client, const char *service, const char *method,
                          uint8_t *packet, uint32_t packet_len)
{
    int ret;
    cmsg_pssd_publish_data send_msg = CMSG_PSSD_PUBLISH_DATA_INIT;

    CMSG_SET_FIELD_PTR (&send_msg, service, (char *) service);
    CMSG_SET_FIELD_PTR (&send_msg, method_name, (char *) method);
    CMSG_SET_FIELD_BYTES (&send_msg, packet, packet, packet_len);

    ret = cmsg_pssd_configuration_api_publish (client, &send_msg);

    return (ret == CMSG_RET_OK);
}
