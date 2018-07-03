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

    return true;
}
