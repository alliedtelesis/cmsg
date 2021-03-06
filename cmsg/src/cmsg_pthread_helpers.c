/**
 * @file cmsg_pthread_helpers.c
 *
 * Simple helper functions for using CMSG with pthreads.
 *
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 *
 */

#include <sys/eventfd.h>
#include "cmsg_pthread_helpers.h"
#include "publisher_subscriber/cmsg_sub_private.h"

typedef struct _cmsg_pthread_server_info
{
    fd_set readfds;
    int fd_max;
    cmsg_server *server;
} cmsg_pthread_server_info;

typedef struct _cmsg_pthread_multithreaded_server_recv_info
{
    cmsg_pthread_multithreaded_server_info *server_info;
    int socket;
} cmsg_pthread_multithreaded_server_recv_info;

/**
 * Function to be called when the 'pthread_server_run'
 * thread is cancelled. This simply cleans up and frees any sockets/
 * memory that was used by the thread.
 *
 * @param pthread_info - Structure containing the resources used by the thread.
 */
static void
pthread_server_cancelled (cmsg_pthread_server_info *pthread_info)
{
    int fd;
    int accept_sd_eventfd = pthread_info->server->accept_thread_info->accept_sd_eventfd;

    cmsg_server_accept_thread_deinit (pthread_info->server);

    for (fd = 0; fd <= pthread_info->fd_max; fd++)
    {
        /* Don't double close the accept event fd */
        if (fd == accept_sd_eventfd)
        {
            continue;
        }
        if (FD_ISSET (fd, &pthread_info->readfds))
        {
            cmsg_server_close_accepted_socket (pthread_info->server, fd);
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

    cmsg_server_accept_thread_init (server);

    pthread_info.server = server;

    fd = server->accept_thread_info->accept_sd_eventfd;
    pthread_info.fd_max = fd;
    FD_SET (fd, &pthread_info.readfds);

    while (true)
    {
        cmsg_server_thread_receive_poll (server, -1, &pthread_info.readfds,
                                         &pthread_info.fd_max);
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
 * using 'cmsg_subscriber_destroy'.
 */
cmsg_subscriber *
cmsg_pthread_unix_subscriber_init (pthread_t *thread, const ProtobufCService *service,
                                   const char **events)
{
    cmsg_subscriber *sub = NULL;

    sub = cmsg_subscriber_create_unix (service);

    /* Subscribe to events */
    if (events)
    {
        cmsg_sub_subscribe_events_local (sub, events);
    }

    if (!cmsg_pthread_server_init (thread, cmsg_sub_unix_server_get (sub)))
    {
        syslog (LOG_ERR, "Failed to start subscriber processing thread");
        cmsg_subscriber_destroy (sub);
        return NULL;
    }

    return sub;
}

/**
 * When processing the server in the multi-threaded mode of operation when one of
 * the receive threads or the accept thread exits we need to decrement the counter
 * storing the number of threads in use. Furthermore, if the server has been marked
 * as shutting down then if this is the last thread to exit then we need to notify
 * the caller of the shutdown that they can now destroy the server.
 *
 * @param server_info - The 'cmsg_pthread_multithreaded_server_info' structure containing
 *                      the information about this server and its operation.
 */
static void
cmsg_pthread_multithreaded_thread_exit (cmsg_pthread_multithreaded_server_info *server_info)
{
    pthread_mutex_lock (&server_info->lock);
    server_info->num_threads--;
    if (server_info->exiting && server_info->num_threads == 0)
    {
        pthread_cond_signal (&server_info->wakeup_cond);
    }
    pthread_mutex_unlock (&server_info->lock);
}

/**
 * The thread receiving data on a connection when processing the server in
 * the multi-threaded mode of operation.
 *
 * @param _recv_info - Pointer to a 'cmsg_pthread_multithreaded_server_recv_info' structure
 *                     containing information about the connection and the server.
 *
 * @return NULL on thread exit.
 */
static void *
cmsg_pthread_multithreaded_receive_thread (void *_recv_info)
{
    fd_set read_fds;
    int fdmax;
    struct timeval tv;
    struct timeval *tv_ptr;
    int ret;
    cmsg_pthread_multithreaded_server_recv_info *recv_info = NULL;
    cmsg_pthread_multithreaded_server_info *server_info = NULL;

    pthread_detach (pthread_self ());

    recv_info = (cmsg_pthread_multithreaded_server_recv_info *) _recv_info;
    server_info = recv_info->server_info;

    fdmax = MAX (recv_info->socket, server_info->shutdown_eventfd);

    while (1)
    {
        FD_ZERO (&read_fds);
        FD_SET (recv_info->socket, &read_fds);
        FD_SET (server_info->shutdown_eventfd, &read_fds);

        if (server_info->timeout)
        {
            tv.tv_sec = server_info->timeout;
            tv.tv_usec = 0;
            tv_ptr = &tv;
        }
        else
        {
            tv_ptr = NULL;
        }

        ret = select (fdmax + 1, &read_fds, NULL, NULL, tv_ptr);
        if (ret == 0)
        {
            /* There has been no activity on the socket for 5 minutes.
             * Close the connection and exit the thread. */
            cmsg_server_close_accepted_socket (server_info->server, recv_info->socket);
            break;
        }
        if (FD_ISSET (recv_info->socket, &read_fds))
        {
            if (cmsg_server_receive (server_info->server, recv_info->socket) < 0)
            {
                cmsg_server_close_accepted_socket (server_info->server, recv_info->socket);
                break;
            }
        }
        if (FD_ISSET (server_info->shutdown_eventfd, &read_fds))
        {
            cmsg_server_close_accepted_socket (server_info->server, recv_info->socket);
            break;
        }
    }

    cmsg_pthread_multithreaded_thread_exit (server_info);
    free (recv_info);

    return NULL;
}

/**
 * The thread that accepts incoming connections when processing the server in the
 * multi-threaded mode of operation.
 *
 * @param info - Pointer to the 'cmsg_pthread_multithreaded_server_info' structure
 *               containing information about the server and its operation.
 *
 * @return NULL on thread exit.
 */
static void *
cmsg_pthread_multithreaded_accept_thread (void *_server_info)
{
    int ret;
    static pthread_t new_recv_thread;
    fd_set read_fds;
    int fdmax;
    cmsg_pthread_multithreaded_server_info *server_info = NULL;
    cmsg_pthread_multithreaded_server_recv_info *recv_info = NULL;
    int accept_event_fd;
    eventfd_t value;
    int *newfd_ptr = NULL;
    cmsg_server_accept_thread_info *info;

    pthread_detach (pthread_self ());

    server_info = (cmsg_pthread_multithreaded_server_info *) _server_info;

    info = server_info->server->accept_thread_info;
    accept_event_fd = info->accept_sd_eventfd;
    fdmax = MAX (accept_event_fd, server_info->shutdown_eventfd);

    while (1)
    {
        FD_ZERO (&read_fds);
        FD_SET (accept_event_fd, &read_fds);
        FD_SET (server_info->shutdown_eventfd, &read_fds);

        select (fdmax + 1, &read_fds, NULL, NULL, NULL);
        if (FD_ISSET (accept_event_fd, &read_fds))
        {
            /* clear notification */
            TEMP_FAILURE_RETRY (eventfd_read (info->accept_sd_eventfd, &value));
            while ((newfd_ptr = g_async_queue_try_pop (info->accept_sd_queue)))
            {
                recv_info = malloc (sizeof (*recv_info));
                if (!recv_info)
                {
                    syslog (LOG_ERR, "Failed to allocate memory for CMSG server receive");
                    cmsg_server_close_accepted_socket (server_info->server, *newfd_ptr);
                    CMSG_FREE (newfd_ptr);
                    continue;
                }

                recv_info->server_info = server_info;
                recv_info->socket = *newfd_ptr;
                ret = pthread_create (&new_recv_thread, NULL,
                                      cmsg_pthread_multithreaded_receive_thread,
                                      (void *) recv_info);
                if (ret == 0)
                {
                    pthread_mutex_lock (&server_info->lock);
                    server_info->num_threads++;
                    pthread_mutex_unlock (&server_info->lock);
                }
                else
                {
                    syslog (LOG_ERR, "Failed to create thread for CMSG server receive");
                    cmsg_server_close_accepted_socket (server_info->server, *newfd_ptr);
                    free (recv_info);
                }
                CMSG_FREE (newfd_ptr);
            }
        }
        if (FD_ISSET (server_info->shutdown_eventfd, &read_fds))
        {
            break;
        }
    }

    cmsg_pthread_multithreaded_thread_exit (server_info);

    return NULL;
}

/**
 * Start the processing of a CMSG server in multi-threaded operation.
 * This will cause every connection to be processed in a separate thread.
 *
 * @param server - The server to start multi-threaded processing for.
 * @param timeout - The number of seconds of inactivity before closing a connection.
 *                  Set to 0 if the connections should never be closed due to inactivity.
 *
 * @return cmsg_pthread_multithreaded_server_info' structure on success.
 *         This should subsequently be called with 'cmsg_pthread_multithreaded_server_destroy'
 *         to shutdown the processing of the server and then destroy it.
 */
cmsg_pthread_multithreaded_server_info *
cmsg_pthread_multithreaded_server_init (cmsg_server *server, uint32_t timeout)
{
    int ret;
    pthread_t accept_thread;
    cmsg_pthread_multithreaded_server_info *server_info = NULL;

    server_info = malloc (sizeof (*server_info));
    if (!server_info)
    {
        return NULL;
    }

    server_info->server = server;
    server_info->timeout = timeout;
    server_info->num_threads = 0;
    server_info->exiting = false;
    server_info->shutdown_eventfd = eventfd (0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (server_info->shutdown_eventfd < 0)
    {
        free (server_info);
        return NULL;
    }
    if (pthread_cond_init (&server_info->wakeup_cond, NULL) != 0)
    {
        close (server_info->shutdown_eventfd);
        free (server_info);
        return NULL;
    }
    if (pthread_mutex_init (&server_info->lock, NULL) != 0)
    {
        close (server_info->shutdown_eventfd);
        pthread_cond_destroy (&server_info->wakeup_cond);
        free (server_info);
        return NULL;
    }

    cmsg_server_accept_thread_init (server);

    ret = pthread_create (&accept_thread, NULL, &cmsg_pthread_multithreaded_accept_thread,
                          (void *) server_info);
    if (ret == 0)
    {
        pthread_mutex_lock (&server_info->lock);
        server_info->num_threads++;
        pthread_mutex_unlock (&server_info->lock);
    }
    else
    {
        close (server_info->shutdown_eventfd);
        pthread_cond_destroy (&server_info->wakeup_cond);
        pthread_mutex_destroy (&server_info->lock);
        free (server_info);
        return NULL;
    }

    return server_info;
}

/**
 * Shutdown and destroy the server previously initialised using
 * 'cmsg_pthread_multithreaded_server_init'.
 *
 * @param info - The 'cmsg_pthread_multithreaded_server_info' structure returned
 *               from the call to 'cmsg_pthread_multithreaded_server_init'.
 */
void
cmsg_pthread_multithreaded_server_destroy (cmsg_pthread_multithreaded_server_info *info)
{
    info->exiting = true;
    TEMP_FAILURE_RETRY (eventfd_write (info->shutdown_eventfd, 1));

    pthread_mutex_lock (&info->lock);
    while (info->num_threads != 0)
    {
        pthread_cond_wait (&info->wakeup_cond, &info->lock);
    }
    pthread_mutex_unlock (&info->lock);

    close (info->shutdown_eventfd);
    pthread_cond_destroy (&info->wakeup_cond);
    pthread_mutex_destroy (&info->lock);
    cmsg_destroy_server_and_transport (info->server);
    free (info);
}

/**
 * Function to be called when the 'cmsg_pthread_sl_event_thread'
 * thread is cancelled. This simply cleans up the service listener info.
 *
 * @param _info - Structure containing the service listener information
 *                used by the thread.
 */
static void
cmsg_pthread_sl_event_thread_cancelled (void *_info)
{
    const cmsg_sl_info *info = _info;

    cmsg_service_listener_unlisten (info);
}

/**
 * Thread used to process events generated from the CMSG service listener functionality.
 */
static void *
cmsg_pthread_sl_event_thread (void *sl_info)
{
    const cmsg_sl_info *info = (const cmsg_sl_info *) sl_info;
    int event_fd;
    fd_set read_fds;
    int maxfd;

    pthread_cleanup_push ((void (*)(void *)) cmsg_pthread_sl_event_thread_cancelled,
                          (void *) info);

    event_fd = cmsg_service_listener_get_event_fd (info);
    if (event_fd < 0)
    {
        syslog (LOG_ERR, "Failed to get socket for service listener.");
        pthread_exit (NULL);
    }

    FD_ZERO (&read_fds);
    FD_SET (event_fd, &read_fds);
    maxfd = event_fd;

    while (1)
    {
        /* Explicitly set where the thread can be cancelled. This ensures no
         * data can be leaked if the thread is cancelled while handling an event.
         */
        pthread_setcancelstate (PTHREAD_CANCEL_ENABLE, NULL);
        select (maxfd + 1, &read_fds, NULL, NULL, NULL);
        pthread_setcancelstate (PTHREAD_CANCEL_DISABLE, NULL);

        cmsg_service_listener_event_queue_process (info);
    }

    pthread_cleanup_pop (1);
    pthread_exit (NULL);
}


/**
 * Create a new thread to run a service listener function.
 *
 * @param thread - Pointer to a pthread_t variable to create the thread.
 * @param service_name - The CMSG service to listen for events for.
 * @param handler - The handler to call on a service event.
 * @param user_data - The data to supply to the handler.
 *
 * @returns true if the thread was successfully created, false otherwise.
 *
 * Note that this thread can be cancelled using 'pthread_cancel' and then should
 * be joined using 'pthread_join'.
 */
bool
cmsg_pthread_service_listener_listen (pthread_t *thread, const char *service_name,
                                      cmsg_sl_event_handler_t handler, void *user_data)
{
    int ret = 0;
    const cmsg_sl_info *info = NULL;

    info = cmsg_service_listener_listen (service_name, handler, user_data);
    if (!info)
    {
        syslog (LOG_ERR, "Failed to initialise service listener functionality");
        return false;
    }

    ret = pthread_create (thread, NULL, cmsg_pthread_sl_event_thread, (void *) info);
    if (ret != 0)
    {
        syslog (LOG_ERR, "Failed to start thread for service listener processing");
        cmsg_service_listener_unlisten (info);
        return false;
    }

    return true;
}
