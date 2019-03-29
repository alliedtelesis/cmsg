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

    client = cmsg_create_client_unix (CMSG_DESCRIPTOR (cmsg_pssd, configuration));
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
cmsg_pss_subscription_add_remove (cmsg_server *sub_server, cmsg_transport *pub_transport,
                                  const char *method_name, bool add)
{
    cmsg_client *client = NULL;
    int ret;
    cmsg_transport_info *transport_info = NULL;
    cmsg_pssd_subscription_info send_msg = CMSG_PSSD_SUBSCRIPTION_INFO_INIT;

    transport_info = cmsg_transport_info_create (sub_server->_transport);
    if (!transport_info)
    {
        return false;
    }
    CMSG_SET_FIELD_PTR (&send_msg, transport_info, transport_info);
    CMSG_SET_FIELD_PTR (&send_msg, service,
                        (char *) cmsg_service_name_get (sub_server->service->descriptor));
    CMSG_SET_FIELD_PTR (&send_msg, method_name, (char *) method_name);

    if (pub_transport->type == CMSG_TRANSPORT_RPC_TCP ||
        pub_transport->type == CMSG_TRANSPORT_ONEWAY_TCP)
    {
        if (pub_transport->config.socket.family != PF_INET6)
        {
            CMSG_SET_FIELD_VALUE (&send_msg, remote_addr,
                                  pub_transport->config.socket.sockaddr.in.sin_addr.s_addr);
            CMSG_SET_FIELD_VALUE (&send_msg, remote_addr_is_tcp, true);
        }
    }
    else if (pub_transport->type == CMSG_TRANSPORT_RPC_TIPC ||
             pub_transport->type == CMSG_TRANSPORT_ONEWAY_TIPC)
    {
        CMSG_SET_FIELD_VALUE (&send_msg, remote_addr,
                              pub_transport->config.socket.sockaddr.tipc.addr.name.
                              name.instance);
        CMSG_SET_FIELD_VALUE (&send_msg, remote_addr_is_tcp, false);
    }

    client = cmsg_create_client_unix (CMSG_DESCRIPTOR (cmsg_pssd, configuration));
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
 * Register a subscription with cmsg_pssd.
 *
 * @param sub_server - The CMSG server structure used by the subscriber to receive
 *                     published notifications.
 * @param pub_transport - The transport to connect to the publisher with.
 * @param method_name - The method name to subscribe for.
 *
 * @returns true on success, false otherwise.
 */
bool
cmsg_pss_subscription_add (cmsg_server *sub_server, cmsg_transport *pub_transport,
                           const char *method_name)
{
    return cmsg_pss_subscription_add_remove (sub_server, pub_transport, method_name, true);
}

/**
 * Unregister a subscription from cmsg_pssd.
 *
 * @param sub_server - The CMSG server structure used by the subscriber to receive
 *                     published notifications.
 * @param pub_transport - The transport to connect to the publisher with.
 * @param method_name - The method name to subscribe for.
 *
 * @returns true on success, false otherwise.
 */
bool
cmsg_pss_subscription_remove (cmsg_server *sub_server, cmsg_transport *pub_transport,
                              const char *method_name)
{
    return cmsg_pss_subscription_add_remove (sub_server, pub_transport, method_name, false);
}
