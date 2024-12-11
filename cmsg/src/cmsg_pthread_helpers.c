/* SPDX-License-Identifier: BSD-3-Clause */
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
#include "cmsg_sl.h"

typedef struct _cmsg_pthread_multithreaded_server_recv_info
{
    cmsg_pthread_multithreaded_server_info *server_info;
    int socket;
} cmsg_pthread_multithreaded_server_recv_info;

static void *
pthread_server_run (void *_server)
{
    cmsg_server *server = (cmsg_server *) _server;
    cmsg_server_thread_task_info *info;

    info = cmsg_server_thread_task_info_create (server, -1);
    if (!info)
    {
        return NULL;
    }
    pthread_cleanup_push ((void (*)(void *)) cmsg_server_thread_task_info_free, info);
    cmsg_server_accept_thread_init (server);
    while (true)
    {
        cmsg_server_thread_receive_poll (info);
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
    // Set the fds later
    struct pollfd pollfd_list[] = { { 0, POLLIN, 0 }, { 0, POLLIN, 0 } };
    int ret;

    cmsg_pthread_multithreaded_server_recv_info *recv_info = NULL;
    cmsg_pthread_multithreaded_server_info *server_info = NULL;

    pthread_detach (pthread_self ());

    recv_info = (cmsg_pthread_multithreaded_server_recv_info *) _recv_info;
    server_info = recv_info->server_info;

    pollfd_list[0].fd = recv_info->socket;
    pollfd_list[1].fd = server_info->shutdown_eventfd;

    while (1)
    {
        ret = poll (pollfd_list, 2, server_info->timeout);
        /* Accept event FD */
        if (pollfd_list[0].revents & POLLIN)
        {
            if (cmsg_server_receive (server_info->server, recv_info->socket) < 0)
            {
                cmsg_server_close_accepted_socket (server_info->server, recv_info->socket);
                break;
            }
        }
        /* If ret == 0, the poll timed out (default 5 minutes). fd_list[1] is the
         * shutdown socket. In both cases, close the connection and exit the thread. */
        if (ret == 0 || (pollfd_list[1].revents & POLLIN))
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
    // Set the fds later
    struct pollfd pollfd_list[] = { { 0, POLLIN, 0 }, { 0, POLLIN, 0 } };
    cmsg_pthread_multithreaded_server_info *server_info = NULL;
    cmsg_pthread_multithreaded_server_recv_info *recv_info = NULL;
    eventfd_t value;
    int *newfd_ptr = NULL;

    pthread_detach (pthread_self ());

    server_info = (cmsg_pthread_multithreaded_server_info *) _server_info;

    pollfd_list[0].fd = server_info->server->accept_sd_eventfd;
    pollfd_list[1].fd = server_info->shutdown_eventfd;

    while (1)
    {
        ret = poll (pollfd_list, 2, -1);
        /* Accept event FD */
        if (pollfd_list[0].revents & POLLIN)
        {
            /* clear notification */
            TEMP_FAILURE_RETRY (eventfd_read
                                (server_info->server->accept_sd_eventfd, &value));
            while ((newfd_ptr =
                    g_async_queue_try_pop (server_info->server->accept_sd_queue)))
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
        /* Shutdown FD */
        if (pollfd_list[1].revents & POLLIN)
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
    server_info->timeout = (timeout && timeout <= UINT32_MAX / 1000) ? timeout * 1000 : -1;
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

/* Thread for all the servers of this process. */
static pthread_t global_sl_accept_thread = -1;
/* This mutex protects the global server accept count and epoll data. */
static pthread_mutex_t global_sl_accept_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned int global_sl_accept_count = 0;

/* Global epoll data. Note that MAX_EVENTS may need to be increased at some point
 * as it is the number of CMSG servers using the global accept thread. */
#define MAX_EVENTS 128
static struct epoll_event events[MAX_EVENTS];
static int epollfd = -1;

/**
 * Thread used to process events generated from the CMSG service listener functionality.
 */
static void *
cmsg_pthread_sl_event_thread (void *unused)
{
    cmsg_sl_info *info;
    int n;
    int nfds;

    while (1)
    {
        nfds = epoll_wait (epollfd, events, MAX_EVENTS, -1);
        if (nfds == -1)
        {
            continue;
        }
        for (n = 0; n < nfds; n++)
        {
            /* Explicitly set where the thread can be cancelled. This ensures no
             * sockets can be leaked if the thread is cancelled after accepting
             * a connection. */
            pthread_setcancelstate (PTHREAD_CANCEL_DISABLE, NULL);
            if (events[n].events & EPOLLIN)
            {
                info = events[n].data.ptr;

                pthread_mutex_lock (&info->thread_mutex);
                if (info->using_global_thread)
                {
                    cmsg_service_listener_event_queue_process (info);
                }
                pthread_mutex_unlock (&info->thread_mutex);
            }
            else
            {
                /* Sleep on error before re-trying in order to avoid a tight loop */
                sleep (1);
            }
            pthread_setcancelstate (PTHREAD_CANCEL_ENABLE, NULL);
        }
    }

    pthread_exit (NULL);
}


static bool
_cmsg_pthread_service_listener_listen (cmsg_sl_info *info, const char *service_name,
                                       cmsg_sl_event_handler_t handler, void *user_data)
{
    struct epoll_event ev;

    /* Global thread data is to be setup. */
    if (epollfd == -1)
    {
        epollfd = epoll_create1 (0);
        if (epollfd == -1)
        {
            return CMSG_RET_ERR;
        }
    }

    if (global_sl_accept_thread == -1)
    {
        if (pthread_create (&global_sl_accept_thread, NULL,
                            cmsg_pthread_sl_event_thread, NULL) != 0)
        {
            global_sl_accept_thread = -1;
            return CMSG_RET_ERR;
        }
        cmsg_pthread_setname (global_sl_accept_thread, "global", CMSG_ACCEPT_PREFIX);
    }

    if (global_sl_accept_count == MAX_EVENTS)
    {
        /* This should not happen in an actual release but during development.
         * As more servers are added, the limit will need to be increased.
         * Continue processing but the new server will fail to run. */
        syslog (LOG_ALERT, "Maximum CMSG service listener thread limit has been reached!");
    }

    memset (&ev, 0, sizeof (struct epoll_event));
    ev.events = EPOLLIN;
    ev.data.ptr = info;
    if (epoll_ctl (epollfd, EPOLL_CTL_ADD, cmsg_service_listener_get_event_fd (info), &ev)
        == -1)
    {
        return CMSG_RET_ERR;
    }

    info->using_global_thread = true;

    global_sl_accept_count++;

    return CMSG_RET_OK;
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
cmsg_sl_info *
cmsg_pthread_service_listener_listen (const char *service_name,
                                      cmsg_sl_event_handler_t handler, void *user_data)
{
    int ret = 0;
    cmsg_sl_info *info = NULL;

    info = cmsg_service_listener_listen (service_name, handler, user_data);
    if (!info)
    {
        syslog (LOG_ERR, "Failed to initialise service listener functionality");
        return NULL;
    }

    pthread_mutex_lock (&global_sl_accept_lock);
    pthread_mutex_lock (&info->thread_mutex);
    ret = _cmsg_pthread_service_listener_listen (info, service_name, handler, user_data);
    if (ret != 0)
    {
        syslog (LOG_ERR, "Failed to start thread for service listener processing");
        cmsg_service_listener_unlisten (info);
        return NULL;
    }
    pthread_mutex_unlock (&info->thread_mutex);
    pthread_mutex_unlock (&global_sl_accept_lock);

    return info;
}

/**
 * Shutdown the server accept thread.
 *
 * @param server - The CMSG server to shutdown the accept thread for.
 */
void
cmsg_pthread_service_listener_unlisten (cmsg_sl_info *info)
{
    if (info && info->using_global_thread)
    {
        pthread_mutex_lock (&global_sl_accept_lock);

        epoll_ctl (epollfd, EPOLL_CTL_DEL, cmsg_service_listener_get_event_fd (info), NULL);

        pthread_mutex_lock (&info->thread_mutex);
        info->using_global_thread = false;
        pthread_mutex_unlock (&info->thread_mutex);
        cmsg_service_listener_unlisten (info);

        global_sl_accept_count--;
        if (global_sl_accept_count == 0)
        {
            pthread_cancel (global_sl_accept_thread);
            pthread_join (global_sl_accept_thread, NULL);

            global_sl_accept_thread = -1;
            close (epollfd);
            epollfd = -1;
        }
        pthread_mutex_unlock (&global_sl_accept_lock);
    }
}
