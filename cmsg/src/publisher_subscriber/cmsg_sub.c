/**
 * cmsg_sub.c
 *
 * Implements the CMSG subscriber which can be used to subscribe for published
 * messages from a CMSG publisher.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include "cmsg_sub.h"
#include "cmsg_sub_private.h"
#include "cmsg_ps_api_private.h"
#include "cmsg_error.h"

static cmsg_subscriber *
cmsg_sub_new (cmsg_transport *tcp_transport, const ProtobufCService *pub_service)
{
    cmsg_transport *unix_transport = NULL;

    cmsg_subscriber *subscriber = (cmsg_subscriber *) CMSG_CALLOC (1, sizeof (*subscriber));
    if (!subscriber)
    {
        CMSG_LOG_GEN_ERROR ("[%s] Unable to allocate memory for subscriber.",
                            pub_service->descriptor->name);
        return NULL;
    }

    /* Create the subscriber transport */
    unix_transport = cmsg_transport_new (CMSG_TRANSPORT_ONEWAY_UNIX);
    unix_transport->config.socket.family = AF_UNIX;
    unix_transport->config.socket.sockaddr.un.sun_family = AF_UNIX;
    snprintf (unix_transport->config.socket.sockaddr.un.sun_path,
              sizeof (unix_transport->config.socket.sockaddr.un.sun_path) - 1,
              "/tmp/%s.%u", pub_service->descriptor->name, getpid ());

    subscriber->unix_server = cmsg_server_new (unix_transport, pub_service);
    if (!subscriber->unix_server)
    {
        CMSG_LOG_GEN_ERROR ("[%s%s] Unable to create subscriber unix server.",
                            pub_service->descriptor->name, unix_transport->tport_id);
        CMSG_FREE (subscriber);
        return NULL;
    }

    if (tcp_transport)
    {
        subscriber->tcp_server = cmsg_server_new (tcp_transport, pub_service);
        if (!subscriber->tcp_server)
        {
            CMSG_LOG_GEN_ERROR ("[%s%s] Unable to create subscriber tcp server.",
                                pub_service->descriptor->name, tcp_transport->tport_id);
            cmsg_destroy_server_and_transport (subscriber->unix_server);
            CMSG_FREE (subscriber);
            return NULL;
        }
    }

    return subscriber;
}

cmsg_server *
cmsg_sub_unix_server_get (cmsg_subscriber *subscriber)
{
    return subscriber->unix_server;
}

int
cmsg_sub_unix_server_socket_get (cmsg_subscriber *subscriber)
{
    CMSG_ASSERT_RETURN_VAL (subscriber != NULL, -1);

    return (cmsg_server_get_socket (subscriber->unix_server));
}

cmsg_server *
cmsg_sub_tcp_server_get (cmsg_subscriber *subscriber)
{
    return subscriber->tcp_server;
}

int
cmsg_sub_tcp_server_socket_get (cmsg_subscriber *subscriber)
{
    CMSG_ASSERT_RETURN_VAL (subscriber != NULL, -1);

    return (cmsg_server_get_socket (subscriber->tcp_server));
}

int32_t
cmsg_sub_subscribe_local (cmsg_subscriber *subscriber, const char *method_name)
{
    return cmsg_ps_subscription_add_local (subscriber->unix_server, method_name);
}

int32_t
cmsg_sub_subscribe_remote (cmsg_subscriber *subscriber, const char *method_name,
                           struct in_addr remote_addr)
{
    return cmsg_ps_subscription_add_remote (subscriber->tcp_server, method_name,
                                            remote_addr);
}

int32_t
cmsg_sub_subscribe_events_local (cmsg_subscriber *subscriber, const char **events)
{
    int32_t ret;
    int32_t return_value = CMSG_RET_OK;
    const char **event = events;

    while (*event)
    {
        ret = cmsg_sub_subscribe_local (subscriber, (char *) *event);
        if (ret < 0)
        {
            return_value = ret;
        }
        event++;
    }

    return return_value;
}

int32_t
cmsg_sub_subscribe_events_remote (cmsg_subscriber *subscriber, const char **events,
                                  struct in_addr remote_addr)
{
    int32_t ret;
    int32_t return_value = CMSG_RET_OK;
    const char **event = events;

    while (*event)
    {
        ret = cmsg_sub_subscribe_remote (subscriber, (char *) *event, remote_addr);
        if (ret < 0)
        {
            return_value = ret;
        }
        event++;
    }

    return return_value;
}

int32_t
cmsg_sub_unsubscribe_local (cmsg_subscriber *subscriber, const char *method_name)
{
    return cmsg_ps_subscription_remove_local (subscriber->unix_server, method_name);
}

int32_t
cmsg_sub_unsubscribe_remote (cmsg_subscriber *subscriber, const char *method_name,
                             struct in_addr remote_addr)
{
    return cmsg_ps_subscription_remove_remote (subscriber->tcp_server, method_name,
                                               remote_addr);
}

int32_t
cmsg_sub_unsubscribe_events_local (cmsg_subscriber *subscriber, const char **events)
{
    int32_t ret;
    int32_t return_value = CMSG_RET_OK;
    const char **event = events;

    while (*event)
    {
        ret = cmsg_sub_unsubscribe_local (subscriber, (char *) *event);
        if (ret < 0)
        {
            return_value = ret;
        }
        event++;
    }

    return return_value;
}

int32_t
cmsg_sub_unsubscribe_events_remote (cmsg_subscriber *subscriber, const char **events,
                                    struct in_addr remote_addr)
{
    int32_t ret;
    int32_t return_value = CMSG_RET_OK;
    const char **event = events;

    while (*event)
    {
        ret = cmsg_sub_unsubscribe_remote (subscriber, (char *) *event, remote_addr);
        if (ret < 0)
        {
            return_value = ret;
        }
        event++;
    }

    return return_value;
}

cmsg_subscriber *
cmsg_subscriber_create_tcp (const char *server_name, struct in_addr addr,
                            const ProtobufCService *service)
{
    cmsg_transport *transport = NULL;
    cmsg_subscriber *subscriber = NULL;

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

cmsg_subscriber *
cmsg_subscriber_create_unix (const ProtobufCService *service)
{
    cmsg_subscriber *subscriber = NULL;

    subscriber = cmsg_sub_new (NULL, service);
    if (!subscriber)
    {
        CMSG_LOG_GEN_ERROR ("Failed to initialize CMSG subscriber for %s",
                            cmsg_service_name_get (service->descriptor));
        return NULL;
    }

    return subscriber;
}

void
cmsg_subscriber_destroy (cmsg_subscriber *subscriber)
{
    if (subscriber)
    {
        if (subscriber->unix_server)
        {
            cmsg_ps_remove_subscriber (subscriber->unix_server);
            cmsg_destroy_server_and_transport (subscriber->unix_server);
        }
        if (subscriber->tcp_server)
        {
            cmsg_ps_remove_subscriber (subscriber->tcp_server);
            cmsg_destroy_server_and_transport (subscriber->tcp_server);
        }

        CMSG_FREE (subscriber);
    }
}
