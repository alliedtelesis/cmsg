/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#include "cmsg_private.h"
#include "cmsg_sub.h"
#include "cmsg_error.h"
#include "cmsg_pss_api_private.h"
#include "cmsg_client_private.h"

#ifdef HAVE_COUNTERD
#include "cntrd_app_defines.h"
#endif

extern int32_t cmsg_client_counter_create (cmsg_client *client, char *app_name);


cmsg_sub *
cmsg_sub_new (cmsg_transport *pub_server_transport, const ProtobufCService *pub_service)
{
    cmsg_sub *subscriber = (cmsg_sub *) CMSG_CALLOC (1, sizeof (cmsg_sub));
    if (!subscriber)
    {
        CMSG_LOG_GEN_ERROR ("[%s%s] Unable to allocate memory for subscriber.",
                            pub_service->descriptor->name, pub_server_transport->tport_id);
        return NULL;
    }

    subscriber->pub_server = cmsg_server_new (pub_server_transport, pub_service);
    if (!subscriber->pub_server)
    {
        CMSG_LOG_GEN_ERROR ("[%s%s] Unable to create pub_server.",
                            pub_service->descriptor->name, pub_server_transport->tport_id);
        CMSG_FREE (subscriber);
        return NULL;
    }

    return subscriber;
}


int
cmsg_sub_get_server_socket (cmsg_sub *subscriber)
{
    CMSG_ASSERT_RETURN_VAL (subscriber != NULL, -1);

    return (cmsg_server_get_socket (subscriber->pub_server));
}


int32_t
cmsg_sub_server_receive_poll (cmsg_sub *sub, int32_t timeout_ms, fd_set *master_fdset,
                              int *fdmax)
{
    return cmsg_server_receive_poll (sub->pub_server, timeout_ms, master_fdset, fdmax);
}


int32_t
cmsg_sub_server_receive (cmsg_sub *subscriber, int32_t server_socket)
{
    CMSG_DEBUG (CMSG_INFO, "[SUB]\n");

    CMSG_ASSERT_RETURN_VAL (subscriber != NULL, CMSG_RET_ERR);

    return cmsg_server_receive (subscriber->pub_server, server_socket);
}


int32_t
cmsg_sub_server_accept (cmsg_sub *subscriber, int32_t listen_socket)
{
    CMSG_ASSERT_RETURN_VAL (subscriber != NULL, -1);

    return cmsg_server_accept (subscriber->pub_server, listen_socket);
}


/**
 * Callback function for CMSG subscriber when a new socket is accepted.
 * This function is for applications that accept sockets by other than CMSG API,
 * cmsg_sub_server_accept() (e.g. by using liboop socket utility functions).
 */
void
cmsg_sub_server_accept_callback (cmsg_sub *subscriber, int32_t sock)
{
    if (subscriber != NULL)
    {
        cmsg_server_accept_callback (subscriber->pub_server, sock);
    }
}

int32_t
cmsg_sub_subscribe_local (cmsg_sub *subscriber, cmsg_transport *sub_client_transport,
                          const char *method_name)
{
    cmsg_pss_subscription_add_local (subscriber->pub_server, method_name);
    return CMSG_RET_OK;
}

int32_t
cmsg_sub_subscribe_remote (cmsg_sub *subscriber, cmsg_transport *sub_client_transport,
                           const char *method_name, struct in_addr remote_addr)
{
    cmsg_pss_subscription_add_remote (subscriber->pub_server, method_name, remote_addr);
    return CMSG_RET_OK;
}

int32_t
cmsg_sub_subscribe_events_local (cmsg_sub *subscriber, cmsg_transport *sub_client_transport,
                                 const char **events)
{
    int32_t ret;
    int32_t return_value = CMSG_RET_OK;
    const char **event = events;

    while (*event)
    {
        ret = cmsg_sub_subscribe_local (subscriber, sub_client_transport, (char *) *event);
        if (ret < 0)
        {
            return_value = ret;
        }
        event++;
    }

    return return_value;
}

int32_t
cmsg_sub_subscribe_events_remote (cmsg_sub *subscriber,
                                  cmsg_transport *sub_client_transport, const char **events,
                                  struct in_addr remote_addr)
{
    int32_t ret;
    int32_t return_value = CMSG_RET_OK;
    const char **event = events;

    while (*event)
    {
        ret = cmsg_sub_subscribe_remote (subscriber, sub_client_transport, (char *) *event,
                                         remote_addr);
        if (ret < 0)
        {
            return_value = ret;
        }
        event++;
    }

    return return_value;
}

int32_t
cmsg_sub_unsubscribe_local (cmsg_sub *subscriber, cmsg_transport *sub_client_transport,
                            const char *method_name)
{
    cmsg_pss_subscription_remove_local (subscriber->pub_server, method_name);
    return CMSG_RET_OK;
}

int32_t
cmsg_sub_unsubscribe_remote (cmsg_sub *subscriber, cmsg_transport *sub_client_transport,
                             const char *method_name, struct in_addr remote_addr)
{
    cmsg_pss_subscription_remove_remote (subscriber->pub_server, method_name, remote_addr);
    return CMSG_RET_OK;
}

int32_t
cmsg_sub_unsubscribe_events_local (cmsg_sub *subscriber,
                                   cmsg_transport *sub_client_transport,
                                   const char **events)
{
    int32_t ret;
    int32_t return_value = CMSG_RET_OK;
    const char **event = events;

    while (*event)
    {
        ret = cmsg_sub_unsubscribe_local (subscriber, sub_client_transport,
                                          (char *) *event);
        if (ret < 0)
        {
            return_value = ret;
        }
        event++;
    }

    return return_value;
}

int32_t
cmsg_sub_unsubscribe_events_remote (cmsg_sub *subscriber,
                                    cmsg_transport *sub_client_transport,
                                    const char **events, struct in_addr remote_addr)
{
    int32_t ret;
    int32_t return_value = CMSG_RET_OK;
    const char **event = events;

    while (*event)
    {
        ret = cmsg_sub_unsubscribe_remote (subscriber, sub_client_transport,
                                           (char *) *event, remote_addr);
        if (ret < 0)
        {
            return_value = ret;
        }
        event++;
    }

    return return_value;
}

cmsg_sub *
cmsg_create_subscriber_tipc_oneway (const char *server_name, int member_id, int scope,
                                    const ProtobufCService *service)
{
    cmsg_transport *transport = NULL;
    cmsg_sub *subscriber = NULL;

    CMSG_ASSERT_RETURN_VAL (server_name != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (service != NULL, NULL);

    transport = cmsg_create_transport_tipc (server_name, member_id, scope,
                                            CMSG_TRANSPORT_ONEWAY_TIPC);
    if (transport == NULL)
    {
        return NULL;
    }

    subscriber = cmsg_sub_new (transport, service);
    if (subscriber == NULL)
    {
        cmsg_transport_destroy (transport);
        CMSG_LOG_GEN_ERROR ("[%s%s] No TIPC subscriber to %d",
                            service->descriptor->name, transport->tport_id, member_id);
        return NULL;
    }

    return subscriber;
}

cmsg_sub *
cmsg_create_subscriber_tcp (const char *server_name, struct in_addr addr,
                            const ProtobufCService *service)
{
    cmsg_transport *transport = NULL;
    cmsg_sub *subscriber = NULL;

    CMSG_ASSERT_RETURN_VAL (server_name != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (service != NULL, NULL);

    transport = cmsg_create_transport_tcp_ipv4 (server_name, &addr, true);
    if (transport == NULL)
    {
        CMSG_LOG_GEN_ERROR ("Failed to create TCP subscriber for %s",
                            service->descriptor->name);
        return NULL;
    }

    subscriber = cmsg_sub_new (transport, service);
    if (subscriber == NULL)
    {
        cmsg_transport_destroy (transport);
        CMSG_LOG_GEN_ERROR ("Failed to create TCP subscriber for %s",
                            service->descriptor->name);
        return NULL;
    }

    return subscriber;
}

cmsg_sub *
cmsg_create_subscriber_unix_oneway (const ProtobufCService *service)
{
    cmsg_sub *subscriber = NULL;
    cmsg_transport *transport = NULL;

    /* Create the subscriber transport */
    transport = cmsg_transport_new (CMSG_TRANSPORT_ONEWAY_UNIX);
    transport->config.socket.family = AF_UNIX;
    transport->config.socket.sockaddr.un.sun_family = AF_UNIX;
    snprintf (transport->config.socket.sockaddr.un.sun_path,
              sizeof (transport->config.socket.sockaddr.un.sun_path) - 1,
              "/tmp/%s.%u", service->descriptor->name, getpid ());

    subscriber = cmsg_sub_new (transport, service);
    if (!subscriber)
    {
        cmsg_transport_destroy (transport);
        CMSG_LOG_GEN_ERROR ("Failed to initialize CMSG subscriber for %s",
                            cmsg_service_name_get (service->descriptor));
        return NULL;
    }

    return subscriber;
}

void
cmsg_destroy_subscriber_and_transport (cmsg_sub *subscriber)
{
    if (subscriber)
    {
        cmsg_pss_remove_subscriber (subscriber->pub_server);
        cmsg_destroy_server_and_transport (subscriber->pub_server);
        CMSG_FREE (subscriber);
    }
}
