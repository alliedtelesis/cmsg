/**
 * cmsg_ps_api.c
 *
 * Implements the functions that can be used to interact with the publisher
 * subscriber storage daemon.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include "configuration_api_auto.h"
#include "publish_api_auto.h"
#include "cmsg_ps_config.h"
#include "cmsg_ps_api_private.h"
#include "transport/cmsg_transport_private.h"

static struct in_addr local_addr;

/**
 * Configure the IP address of the server running in cmsg_psd.
 * This is the address that remote hosts can connect to.
 *
 * @param addr - The address to configure.
 *
 * @returns CMSG_RET_OK on success, related error code on failure.
 */
int32_t
cmsg_ps_address_set (struct in_addr addr)
{
    cmsg_client *client = NULL;
    cmsg_uint32 send_msg = CMSG_UINT32_INIT;
    int ret;

    client = cmsg_create_client_unix (CMSG_DESCRIPTOR (cmsg_psd, configuration));
    if (!client)
    {
        return CMSG_RET_ERR;
    }

    CMSG_SET_FIELD_VALUE (&send_msg, value, addr.s_addr);
    local_addr.s_addr = addr.s_addr;

    ret = cmsg_psd_configuration_api_address_set (client, &send_msg);
    cmsg_destroy_client_and_transport (client);

    return ret;
}

/**
 * Helper function for calling the required API to cmsg_psd to register/unregister
 * the subscription.
 *
 * @param sub_server - The CMSG server structure used by the subscriber to receive
 *                     published notifications.
 * @param pub_transport - The transport to connect to the publisher with.
 * @param method_name - The method name to subscribe for.
 * @param add - Whether we are adding the subscription or removing it.
 *
 * @returns CMSG_RET_OK on success, related error code on failure.
 */
static int32_t
cmsg_ps_subscription_add_remove (cmsg_server *sub_server, const char *method_name,
                                 bool add, bool remote, uint32_t remote_addr)
{
    cmsg_client *client = NULL;
    int ret;
    cmsg_transport_info *transport_info = NULL;
    cmsg_subscription_info send_msg = CMSG_SUBSCRIPTION_INFO_INIT;

    transport_info = cmsg_transport_info_create (sub_server->_transport);
    if (!transport_info)
    {
        return CMSG_RET_ERR;
    }
    CMSG_SET_FIELD_PTR (&send_msg, transport_info, transport_info);
    CMSG_SET_FIELD_PTR (&send_msg, service,
                        (char *) cmsg_service_name_get (sub_server->service->descriptor));
    CMSG_SET_FIELD_PTR (&send_msg, method_name, (char *) method_name);

    if (remote)
    {
        CMSG_SET_FIELD_VALUE (&send_msg, remote_addr, remote_addr);
    }

    client = cmsg_create_client_unix (CMSG_DESCRIPTOR (cmsg_psd, configuration));
    if (!client)
    {
        cmsg_transport_info_free (transport_info);
        return CMSG_RET_ERR;
    }

    if (add)
    {
        ret = cmsg_psd_configuration_api_add_subscription (client, &send_msg);
    }
    else
    {
        ret = cmsg_psd_configuration_api_remove_subscription (client, &send_msg);
    }

    cmsg_destroy_client_and_transport (client);
    cmsg_transport_info_free (transport_info);

    return ret;
}

/**
 * Register a local subscription with cmsg_psd.
 *
 * @param sub_server - The CMSG server structure used by the subscriber to receive
 *                     published notifications.
 * @param method_name - The method name to subscribe for.
 *
 * @returns CMSG_RET_OK on success, related error code on failure.
 */
int32_t
cmsg_ps_subscription_add_local (cmsg_server *sub_server, const char *method_name)
{
    return cmsg_ps_subscription_add_remove (sub_server, method_name, true, false, 0);

}

/**
 * Register a remote subscription with cmsg_psd.
 *
 * @param sub_server - The CMSG server structure used by the subscriber to receive
 *                     published notifications.
 * @param method_name - The method name to subscribe for.
 * @param remote_addr - The address of the remote node to subscribe to.
 *
 * @returns CMSG_RET_OK on success, related error code on failure.
 */
int32_t
cmsg_ps_subscription_add_remote (cmsg_server *sub_server, const char *method_name,
                                 struct in_addr remote_addr)
{
    return cmsg_ps_subscription_add_remove (sub_server, method_name, true, true,
                                            remote_addr.s_addr);
}

/**
 * Unregister a local subscription from cmsg_psd.
 *
 * @param sub_server - The CMSG server structure used by the subscriber to receive
 *                     published notifications.
 * @param method_name - The method name to subscribe for.
 *
 * @returns CMSG_RET_OK on success, related error code on failure.
 */
int32_t
cmsg_ps_subscription_remove_local (cmsg_server *sub_server, const char *method_name)
{
    return cmsg_ps_subscription_add_remove (sub_server, method_name, false, false, 0);
}

/**
 * Unregister a remote subscription from cmsg_psd.
 *
 * @param sub_server - The CMSG server structure used by the subscriber to receive
 *                     published notifications.
 * @param method_name - The method name to subscribe for.
 * @param remote_addr - The address of the remote node to unsubscribe from.
 *
 * @returns CMSG_RET_OK on success, related error code on failure.
 */
int32_t
cmsg_ps_subscription_remove_remote (cmsg_server *sub_server, const char *method_name,
                                    struct in_addr remote_addr)
{
    return cmsg_ps_subscription_add_remove (sub_server, method_name, false, true,
                                            remote_addr.s_addr);
}

/**
 * Unregister a subscriber from cmsg_psd. This will remove all subscriptions for the
 * given subscriber.
 *
 * @param sub_server - The CMSG server structure used by the subscriber to receive
 *                     published notifications.
 *
 * @returns CMSG_RET_OK on success, related error code on failure.
 */
int32_t
cmsg_ps_remove_subscriber (cmsg_server *sub_server)
{
    cmsg_client *client = NULL;
    int ret;
    cmsg_transport_info *transport_info = NULL;

    transport_info = cmsg_transport_info_create (sub_server->_transport);
    if (!transport_info)
    {
        return CMSG_RET_ERR;
    }

    client = cmsg_create_client_unix (CMSG_DESCRIPTOR (cmsg_psd, configuration));
    if (!client)
    {
        cmsg_transport_info_free (transport_info);
        return CMSG_RET_ERR;
    }

    ret = cmsg_psd_configuration_api_remove_subscriber (client, transport_info);

    cmsg_destroy_client_and_transport (client);
    cmsg_transport_info_free (transport_info);

    return ret;
}

/**
 * Create the client that can be used by a cmsg publisher to send messages
 * for publishing by cmsg_psd.
 *
 * This client must be freed by the caller using 'cmsg_destroy_client_and_transport'.
 *
 * @returns A pointer to a client that can be used to send messages to cmsg_psd on success,
 *          NULL otherwise.
 */
cmsg_client *
cmsg_ps_create_publisher_client (void)
{
    return cmsg_create_client_unix_oneway (CMSG_DESCRIPTOR (cmsg_psd, publish));
}

/**
 * Send a packet to cmsg_psd so that it can be sent to all interested subscribers.
 *
 * @param client - The client connected to cmsg_psd (previously returned by a call to
 *                 'cmsg_ps_create_publisher_client')
 * @param service - The service the packet is for.
 * @param method - The method the packet is for.
 * @param packet - The packet to send.
 * @param packet_len - The length of the packet.
 *
 * @returns CMSG_RET_OK if the packet was successfully sent to cmsg_psd,
 *          related error code on failure.
 */
int32_t
cmsg_ps_publish_message (cmsg_client *client, const char *service, const char *method,
                         uint8_t *packet, uint32_t packet_len)
{
    cmsg_psd_publish_data send_msg = CMSG_PSD_PUBLISH_DATA_INIT;

    CMSG_SET_FIELD_PTR (&send_msg, service, (char *) service);
    CMSG_SET_FIELD_PTR (&send_msg, method_name, (char *) method);
    CMSG_SET_FIELD_BYTES (&send_msg, packet, packet, packet_len);

    return cmsg_psd_publish_api_send_data (client, &send_msg);
}

/**
 * Register a publisher with cmsg_psd.
 *
 * @param service - The service name the publisher is publishing events for.
 * @param methods - Pointer to return a GList of the methods that currently
 *                  have subscriptions for the service the publisher is
 *                  publishing for.
 *
 * @returns CMSG_RET_OK on success, CMSG_RET_ERR on failure.
 */
int32_t
cmsg_ps_register_publisher (const char *service, GList **methods)
{
    cmsg_client *client = NULL;
    cmsg_service_info send_msg = CMSG_SERVICE_INFO_INIT;
    cmsg_subscription_methods *recv_msg = NULL;
    int ret;
    GList *method_list = NULL;
    int i;
    char *method = NULL;

    client = cmsg_create_client_unix (CMSG_DESCRIPTOR (cmsg_psd, configuration));
    if (!client)
    {
        return CMSG_RET_ERR;
    }

    CMSG_SET_FIELD_PTR (&send_msg, service, (char *) service);

    ret = cmsg_psd_configuration_api_add_publisher (client, &send_msg, &recv_msg);
    cmsg_destroy_client_and_transport (client);

    if (ret != CMSG_RET_OK)
    {
        return CMSG_RET_ERR;
    }

    CMSG_REPEATED_FOREACH (recv_msg, methods, method, i)
    {
        method_list = g_list_append (method_list, CMSG_STRDUP (method));
    }
    CMSG_FREE_RECV_MSG (recv_msg);

    *methods = method_list;
    return CMSG_RET_OK;
}
