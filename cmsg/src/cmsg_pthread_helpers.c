/**
 * @file cmsg_pthread_helpers.c
 *
 * Simple helper functions for using CMSG with pthreads.
 *
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 *
 */

#include "cmsg_pthread_helpers.h"

typedef struct _cmsg_pthread_server_info
{
    fd_set readfds;
    int fd_max;
    cmsg_server_accept_thread_info *accept_thread_info;
} cmsg_pthread_server_info;

/**
 * Function to be called when the 'ffo_health_cmsg_server_thread_run'
 * thread is cancelled. This simply cleans up and frees any sockets/
 * memory that was used by the thread.
 *
 * @param pthread_info - Structure containing the resources used by the thread.
 */
static void
pthread_server_cancelled (cmsg_pthread_server_info *pthread_info)
{
    int fd;
    int accept_sd_eventfd = pthread_info->accept_thread_info->accept_sd_eventfd;

    cmsg_server_accept_thread_deinit (pthread_info->accept_thread_info);
    pthread_info->accept_thread_info = NULL;

    for (fd = 0; fd < pthread_info->fd_max; fd++)
    {
        /* Don't double close the accept event fd */
        if (fd == accept_sd_eventfd)
        {
            continue;
        }
        if (FD_ISSET (fd, &pthread_info->readfds))
        {
            close (fd);
        }
    }
}

static void *
pthread_server_run (void *_server)
{
    cmsg_pthread_server_info pthread_info;
    int fd = -1;
    cmsg_server *server = (cmsg_server *) _server;

    pthread_info.fd_max = 0;
    FD_ZERO (&pthread_info.readfds);

    pthread_cleanup_push ((void (*)(void *)) pthread_server_cancelled, &pthread_info);

    pthread_info.accept_thread_info = cmsg_server_accept_thread_init (server);

    fd = pthread_info.accept_thread_info->accept_sd_eventfd;
    pthread_info.fd_max = fd + 1;
    FD_SET (fd, &pthread_info.readfds);

    while (true)
    {
        cmsg_server_thread_receive_poll (pthread_info.accept_thread_info, -1,
                                         &pthread_info.readfds, &pthread_info.fd_max);
    }

    pthread_cleanup_pop (1);

    return NULL;
}

/**
 * Create a new thread to do all processing of the given cmsg server.
 *
 * @param thread - Pointer to a pthread_t variable to create the thread.
 * @param server - The CMSG server to process in the new thread.
 *
 * @returns true if the thread was successfully created, false otherwise.
 *
 * Note that this thread can be cancelled using 'pthread_cancel' and then should
 * be joined using 'pthread_join'.
 */
bool
cmsg_pthread_server_init (pthread_t *thread, cmsg_server *server)
{
    int ret = 0;

    ret = pthread_create (thread, NULL, pthread_server_run, server);
    if (ret != 0)
    {
        syslog (LOG_ERR, "Failed to initialise server pthread processing");
        return false;
    }

    cmsg_pthread_setname (*thread,
                          server->service->descriptor->short_name, CMSG_SERVER_PREFIX);

    return true;
}

/**
 * Creates a cmsg subscriber using a unix transport, subscribes to the input list of
 * events and finally begins processing the received events in a new thread.
 *
 * @param thread - Pointer to a pthread_t variable to create the thread.
 * @param service - The CMSG service to subscribe to.
 * @param events - An array of strings containing the events to subscribe to. This
 *                 array should be NULL terminated, i.e. { "event1", "event2", NULL }.
 *
 * Note that this thread can be cancelled using 'pthread_cancel' and then should
 * be joined using 'pthread_join'. At this stage the subscriber can then be destroyed
 * using 'cmsg_destroy_subscriber_and_transport'.
 */
cmsg_sub *
cmsg_pthread_unix_subscriber_init (pthread_t *thread, const ProtobufCService *service,
                                   const char **events)
{
    cmsg_transport *transport_r = NULL;
    cmsg_sub *sub = NULL;

    sub = cmsg_create_subscriber_unix_oneway (service);

    /* Subscribe to events */
    if (events)
    {
        transport_r = cmsg_create_transport_unix (service->descriptor,
                                                  CMSG_TRANSPORT_RPC_UNIX);
        cmsg_sub_subscribe_events (sub, transport_r, events);
        cmsg_transport_destroy (transport_r);
    }

    if (!cmsg_pthread_server_init (thread, sub->pub_server))
    {
        syslog (LOG_ERR, "Failed to start subscriber processing thread");
        cmsg_destroy_subscriber_and_transport (sub);
        return NULL;
    }

    return sub;
}

/**
 * Creates a cmsg subscriber using a tipc transport, subscribes to the input list of
 * events and finally begins processing the received events in a new thread.
 *
 * @param thread - Pointer to a pthread_t variable to create the thread.
 * @param service - The CMSG service to subscribe to.
 * @param events - An array of strings containing the events to subscribe to. This
 *                 array should be NULL terminated, i.e. { "event1", "event2", NULL }.
 * @param subscriber_service_name - The service name of the subscriber. Note that it is
 *                                  important that if there are multiple subscribers on the
 *                                  same node then they need to use different port numbers,
 *                                  and hence different subscriber service names.
 * @param publisher_service_name - The service name of the publisher to subscribe to.
 * @param this_node_id - The TIPC node id of this node.
 * @param scope - The TIPC scope to use.
 *
 * Note that this thread can be cancelled using 'pthread_cancel' and then should
 * be joined using 'pthread_join'. At this stage the subscriber can then be destroyed
 * using 'cmsg_destroy_subscriber_and_transport'.
 */
cmsg_sub *
cmsg_pthread_tipc_subscriber_init (pthread_t *thread, const ProtobufCService *service,
                                   const char **events, const char *subscriber_service_name,
                                   const char *publisher_service_name,
                                   int this_node_id, int scope)
{
    cmsg_transport *transport_r = NULL;
    cmsg_sub *sub = NULL;

    sub = cmsg_create_subscriber_tipc_oneway (subscriber_service_name, this_node_id,
                                              scope, service);

    /* Subscribe to events */
    if (events)
    {
        transport_r = cmsg_create_transport_tipc_rpc (publisher_service_name, this_node_id,
                                                      scope);
        cmsg_sub_subscribe_events (sub, transport_r, events);
        cmsg_transport_destroy (transport_r);
    }

    if (!cmsg_pthread_server_init (thread, sub->pub_server))
    {
        syslog (LOG_ERR, "Failed to start subscriber processing thread");
        cmsg_destroy_subscriber_and_transport (sub);
        return NULL;
    }

    return sub;
}

static cmsg_pub *
cmsg_pthread_publisher_init (pthread_t *thread, cmsg_transport *transport,
                             const ProtobufCServiceDescriptor *service_desc)
{
    cmsg_pub *pub = NULL;

    pub = cmsg_pub_new (transport, service_desc);
    if (!pub)
    {
        syslog (LOG_ERR, "Failed to initialize the CMSG publisher");
        return NULL;
    }

    if (!cmsg_pthread_server_init (thread, pub->sub_server))
    {
        syslog (LOG_ERR, "Failed to start publisher listening thread");
        cmsg_pub_destroy (pub);
        return NULL;
    }

    cmsg_pub_queue_enable (pub);
    cmsg_pub_queue_thread_start (pub);

    return pub;
}

/**
 * Creates a cmsg publisher using a unix transport and begins processing the
 * received subscription requests in a new thread. Note that this function
 * automatically begins queueing the published events and sends them from yet
 * another thread to avoid any potential deadlock.
 *
 * @param thread - Pointer to a pthread_t variable to create the thread.
 * @param service_desc - The CMSG service descriptor to publish for.
 *
 * Note that this thread can be cancelled using 'pthread_cancel' and then should
 * be joined using 'pthread_join'. At this stage the function 'cmsg_pub_queue_thread_stop'
 * should be called with the publisher before finally destroying it using
 * 'cmsg_destroy_publisher_and_transport'.
 */
cmsg_pub *
cmsg_pthread_unix_publisher_init (pthread_t *thread,
                                  const ProtobufCServiceDescriptor *service_desc)
{
    cmsg_transport *transport = NULL;
    cmsg_pub *pub = NULL;

    transport = cmsg_create_transport_unix (service_desc, CMSG_TRANSPORT_RPC_UNIX);
    pub = cmsg_pthread_publisher_init (thread, transport, service_desc);
    if (!pub)
    {
        cmsg_transport_destroy (transport);
    }

    return pub;
}

/**
 * Creates a cmsg publisher using a tipc transport and begins processing the
 * received subscription requests in a new thread. Note that this function
 * automatically begins queueing the published events and sends them from yet
 * another thread to avoid any potential deadlock.
 *
 * @param thread - Pointer to a pthread_t variable to create the thread.
 * @param service_desc - The CMSG service descriptor to publish for.
 * @param publisher_service_name - The service name of the publisher.
 * @param this_node_id - The TIPC node id of this node.
 * @param  scope - The TIPC scope to use.
 *
 * Note that this thread can be cancelled using 'pthread_cancel' and then should
 * be joined using 'pthread_join'. At this stage the function 'cmsg_pub_queue_thread_stop'
 * should be called with the publisher before finally destroying it using
 * 'cmsg_destroy_publisher_and_transport'.
 */
cmsg_pub *
cmsg_pthread_tipc_publisher_init (pthread_t *thread,
                                  const ProtobufCServiceDescriptor *service_desc,
                                  const char *publisher_service_name, int this_node_id,
                                  int scope)
{
    cmsg_transport *transport = NULL;
    cmsg_pub *pub = NULL;

    transport = cmsg_create_transport_tipc_rpc (publisher_service_name, this_node_id,
                                                scope);
    pub = cmsg_pthread_publisher_init (thread, transport, service_desc);
    if (!pub)
    {
        cmsg_transport_destroy (transport);
    }

    return pub;
}
