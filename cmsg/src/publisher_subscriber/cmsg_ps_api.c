/**
 * cmsg_ps_api.c
 *
 * Implements the functions that can be used to interact with the publisher
 * subscriber storage daemon.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include "configuration_api_auto.h"
#include "cmsg_ps_config.h"
#include "cmsg_ps_api_private.h"
#include "transport/cmsg_transport_private.h"
#include "update_impl_auto.h"

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
 * Helper function for calling the required API to cmsg_psd to register/deregister
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
    cmsg_service_info send_msg = CMSG_SERVICE_INFO_INIT;
    cmsg_transport_info *transport_info = NULL;
    const char *service = cmsg_service_name_get (sub_server->service->descriptor);

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

    CMSG_SET_FIELD_PTR (&send_msg, service, (char *) service);
    CMSG_SET_FIELD_PTR (&send_msg, server_info, transport_info);

    ret = cmsg_psd_configuration_api_remove_subscriber (client, &send_msg);

    cmsg_destroy_client_and_transport (client);
    cmsg_transport_info_free (transport_info);

    return ret;
}

/**
 * Create the server that can be used by a cmsg publisher to receive subscription
 * update messages from cmsg_psd.
 *
 * This server must be freed by the caller using 'cmsg_destroy_server_and_transport'.
 *
 * @returns A pointer to a server that can be used to receive subscription update
 *          messages from cmsg_psd on success,
 *          NULL otherwise.
 */
cmsg_server *
cmsg_ps_create_publisher_update_server (void)
{
    cmsg_transport *transport = NULL;
    static uint32_t id = 1; /* Required for multiple publishers in one process */

    transport = cmsg_transport_new (CMSG_TRANSPORT_RPC_UNIX);
    transport->config.socket.family = AF_UNIX;
    transport->config.socket.sockaddr.un.sun_family = AF_UNIX;
    snprintf (transport->config.socket.sockaddr.un.sun_path,
              sizeof (transport->config.socket.sockaddr.un.sun_path) - 1,
              "/tmp/%s.%u.%u", cmsg_service_name_get (CMSG_DESCRIPTOR (cmsg_psd, update)),
              getpid (), id++);

    return cmsg_server_new (transport, CMSG_SERVICE (cmsg_psd, update));
}

/**
 * Register a publisher with cmsg_psd.
 *
 * @param service - The service name the publisher is publishing events for.
 * @param server - The server the publisher is using to listen for updates from cmsg_psd.
 * @param subscribed_methods - Pointer to a 'cmsg_subscription_methods' message that stores
 *                             the subscriber information returned from cmsg_psd. This should
 *                             be freed using CMSG_FREE_RECV_MSG by the caller.
 *
 * @returns CMSG_RET_OK on success, CMSG_RET_ERR on failure.
 */
int32_t
cmsg_ps_register_publisher (const char *service, cmsg_server *server,
                            cmsg_subscription_methods **subscribed_methods)
{
    cmsg_client *client = NULL;
    cmsg_service_info send_msg = CMSG_SERVICE_INFO_INIT;
    int ret;
    cmsg_transport_info *transport_info = NULL;

    transport_info = cmsg_transport_info_create (server->_transport);
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

    CMSG_SET_FIELD_PTR (&send_msg, service, (char *) service);
    CMSG_SET_FIELD_PTR (&send_msg, server_info, transport_info);

    *subscribed_methods = NULL;
    ret = cmsg_psd_configuration_api_add_publisher (client, &send_msg, subscribed_methods);
    cmsg_destroy_client_and_transport (client);
    cmsg_transport_info_free (transport_info);

    return ret;
}

/**
 * Unregister a publisher from cmsg_psd.
 *
 * @param service - The service name the publisher is publishing events for.
 * @param server - The server the publisher is using to listen for updates from cmsg_psd.
 *
 * @returns CMSG_RET_OK on success, CMSG_RET_ERR on failure.
 */
int32_t
cmsg_ps_deregister_publisher (const char *service, cmsg_server *server)
{
    cmsg_client *client = NULL;
    cmsg_service_info send_msg = CMSG_SERVICE_INFO_INIT;
    int ret;
    cmsg_transport_info *transport_info = NULL;

    transport_info = cmsg_transport_info_create (server->_transport);
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

    CMSG_SET_FIELD_PTR (&send_msg, service, (char *) service);
    CMSG_SET_FIELD_PTR (&send_msg, server_info, transport_info);

    ret = cmsg_psd_configuration_api_remove_publisher (client, &send_msg);
    cmsg_destroy_client_and_transport (client);
    cmsg_transport_info_free (transport_info);

    return ret;
}
