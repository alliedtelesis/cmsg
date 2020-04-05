/**
 * netlink.c
 *
 * To have functions monitoring process event and dealing the situation that
 * a process terminates or is terminated abnormally.
 *
 * Copyright 2020, Allied Telesis Labs New Zealand, Ltd
 */

#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <glib-unix.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/connector.h>
#include <linux/cn_proc.h>
#include <unistd.h>
#include <strings.h>
#include <errno.h>
#include "data.h"
#include "netlink.h"
#include <syslog.h>

/* Read the net link socket and deal with the case that a process is terminated
 * abnormally */
static gboolean
netlink_event_receive_read (GIOChannel *source, GIOCondition condition, gpointer data)
{
    char buff[CONNECTOR_MAX_MSG_SIZE];
    struct proc_event *event;
    fd_set fds;
    int sock = g_io_channel_unix_get_fd (source);

    FD_ZERO (&fds);
    FD_SET (sock, &fds);

    if (select (sock + 1, &fds, NULL, NULL, NULL) < 0)
    {
        return FALSE;
    }

    /* If there were no events detected, return */
    if (!FD_ISSET (sock, &fds))
    {
        return FALSE;
    }

    /* if there are events, filtering them and processing exit event */
    if (recv (sock, buff, sizeof (buff), 0) > 0)
    {
        struct nlmsghdr *hdr = (struct nlmsghdr *) buff;
        if (NLMSG_DONE == hdr->nlmsg_type)
        {
            event = (struct proc_event *) ((struct cn_msg *) NLMSG_DATA (hdr))->data;
            struct exit_proc_event *exit = &event->event_data.exit;
            switch (event->what)
            {
            case PROC_EVENT_EXIT:
                /* When a process exits due to a signal the exit code is 128 + the signal
                 * number. One exception to this is SIGKILL where the exit code is set to
                 * SIGKILL. An exit code of 255 specifies an exit code that was out of range,
                 * not due to a signal. */
                if ((exit->process_pid == exit->process_tgid) &&
                    (exit->exit_code == SIGKILL ||
                     (exit->exit_code > 128 && exit->exit_code < 255)))
                {
                    data_remove_by_pid (exit->process_pid);
                }
                break;
            default:
                break;
            }
        }
    }

    return TRUE;
}


/* Open a socket to net link connector and start watching process events.
 * Returns 0 on success, or -1 otherwise */
int
netlink_init (void)
{
    int sock;

    if ((sock = socket (PF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR)) < 0)
    {
        syslog (LOG_ERR, "Error: cannot open netlink socket");
        return -1;
    }

    // bind socket
    struct sockaddr_nl addr;
    memset (&addr, 0, sizeof (addr));
    addr.nl_pid = getpid ();
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = CN_IDX_PROC;
    if (bind (sock, (struct sockaddr *) &addr, sizeof (addr)) < 0)
    {
        syslog (LOG_ERR, "Error: cannot bind to netlink socket");
        return -1;
    }

    // send monitoring message
    struct nlmsghdr nlmsghdr;
    memset (&nlmsghdr, 0, sizeof (nlmsghdr));
    nlmsghdr.nlmsg_len =
        NLMSG_LENGTH (sizeof (struct cn_msg) + sizeof (enum proc_cn_mcast_op));
    nlmsghdr.nlmsg_pid = getpid ();
    nlmsghdr.nlmsg_type = NLMSG_DONE;

    struct cn_msg cn_msg;
    memset (&cn_msg, 0, sizeof (cn_msg));
    cn_msg.id.idx = CN_IDX_PROC;
    cn_msg.id.val = CN_VAL_PROC;
    cn_msg.len = sizeof (enum proc_cn_mcast_op);

    struct iovec iov[3];
    iov[0].iov_base = &nlmsghdr;
    iov[0].iov_len = sizeof (nlmsghdr);
    iov[1].iov_base = &cn_msg;
    iov[1].iov_len = sizeof (cn_msg);

    enum proc_cn_mcast_op op = PROC_CN_MCAST_LISTEN;
    iov[2].iov_base = &op;
    iov[2].iov_len = sizeof (op);

    if (writev (sock, iov, 3) == -1)
    {
        syslog (LOG_ERR, "Error: cannot write to netlink socket");
        return -1;
    }

    GIOChannel *nl_io_channel = g_io_channel_unix_new (sock);
    g_io_add_watch (nl_io_channel, G_IO_IN, netlink_event_receive_read, NULL);

    return 0;
}
