/**
 * cmsg_sl_api.c
 *
 * Implements the functions that can be used to interact with the service
 * listener daemon.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include "configuration_api_auto.h"
#include "cmsg_sl_config.h"
#include "cmsg_server_private.h"

/**
 * Configure the IP address of the server running in the service listener
 * daemon. This is the address that remote hosts can connect to.
 *
 * @param addr - The address to configure.
 *
 * @returns true on success, false on error.
 */
bool
cmsg_service_listener_address_set (struct in_addr addr)
{
    cmsg_client *client = NULL;
    cmsg_uint32 send_msg = CMSG_UINT32_INIT;
    int ret;

    client = cmsg_create_client_unix_oneway (CMSG_DESCRIPTOR_NOPACKAGE (configuration));
    if (!client)
    {
        return false;
    }

    CMSG_SET_FIELD_VALUE (&send_msg, value, addr.s_addr);

    ret = configuration_api_address_set (client, &send_msg);
    cmsg_destroy_client_and_transport (client);

    return (ret == CMSG_RET_OK);
}

/**
 * Add a remote host to the service listener daemon. The daemon will then
 * connect to the service listener daemon running on the remote host and sync
 * the local service information to it.
 *
 * @param addr - The address of the remote node.
 *
 * @returns true on success, false on error.
 */
bool
cmsg_service_listener_add_host (struct in_addr addr)
{
    cmsg_client *client = NULL;
    cmsg_uint32 send_msg = CMSG_UINT32_INIT;
    int ret;

    client = cmsg_create_client_unix_oneway (CMSG_DESCRIPTOR_NOPACKAGE (configuration));
    if (!client)
    {
        return false;
    }

    CMSG_SET_FIELD_VALUE (&send_msg, value, addr.s_addr);

    ret = configuration_api_add_host (client, &send_msg);
    cmsg_destroy_client_and_transport (client);

    return (ret == CMSG_RET_OK);
}

/**
 * Removes a remote host from the service listener daemon. The daemon will then
 * remove the connection to the service listener daemon running on the remote host
 * and remove all service information for it.
 *
 * @param addr - The address of the remote node.
 *
 * @returns true on success, false on error.
 */
bool
cmsg_service_listener_delete_host (struct in_addr addr)
{
    cmsg_client *client = NULL;
    cmsg_uint32 send_msg = CMSG_UINT32_INIT;
    int ret;

    client = cmsg_create_client_unix_oneway (CMSG_DESCRIPTOR_NOPACKAGE (configuration));
    if (!client)
    {
        return false;
    }

    CMSG_SET_FIELD_VALUE (&send_msg, value, addr.s_addr);

    ret = configuration_api_delete_host (client, &send_msg);
    cmsg_destroy_client_and_transport (client);

    return (ret == CMSG_RET_OK);
}

void
cmsg_service_listener_subscribe (void)
{
    /* todo */
}

void
cmsg_service_listener_unsubscribe (void)
{
    /* todo */
}

/**
 * Tell the service listener daemon that a server implementing a specific service
 * is now running.
 *
 * @param server - The newly created server.
 */
void
cmsg_service_listener_add_server (cmsg_server *server)
{
    cmsg_client *client = NULL;
    cmsg_service_info *send_msg = NULL;

    send_msg = cmsg_server_service_info_create (server);
    if (send_msg)
    {
        client = cmsg_create_client_unix_oneway (CMSG_DESCRIPTOR_NOPACKAGE (configuration));
        configuration_api_add_server (client, send_msg);
        cmsg_destroy_client_and_transport (client);
        cmsg_server_service_info_free (send_msg);
    }
}

/**
 * Tell the service listener daemon that a server implementing a specific service
 * is no longer running.
 *
 * @param server - The server that is being deleted.
 */
void
cmsg_service_listener_remove_server (cmsg_server *server)
{
    cmsg_client *client = NULL;
    cmsg_service_info *send_msg = NULL;

    send_msg = cmsg_server_service_info_create (server);
    if (send_msg)
    {
        client = cmsg_create_client_unix_oneway (CMSG_DESCRIPTOR_NOPACKAGE (configuration));
        configuration_api_remove_server (client, send_msg);
        cmsg_destroy_client_and_transport (client);
        cmsg_server_service_info_free (send_msg);
    }
}
