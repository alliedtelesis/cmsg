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

    ret = cmsg_pssd_configuration_api_address_set (client, &send_msg);
    cmsg_destroy_client_and_transport (client);

    return (ret == CMSG_RET_OK);
}
