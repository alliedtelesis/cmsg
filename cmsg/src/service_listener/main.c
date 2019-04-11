/**
 * main.c
 *
 * The CMSG service listener daemon.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <glib-unix.h>
#include <healthcheck.h>
#include "configuration.h"
#include "data.h"
#include "remote_sync.h"

#define DEBUG_FILE "/tmp/cmsg_sld_debug.txt"

/**
 * Called when we receive data on our healthcheck socket
 */
static gboolean
cmsg_sld_healthcheck_read (GIOChannel *source, GIOCondition condition, gpointer data)
{
    healthcheck_reply ();
    return TRUE;
}

/**
 * Registers for healthchecking by appmond.
 */
static gboolean
cmsg_sld_healthcheck_init (void *unused)
{
    int select_fd;

    if (healthcheck_init (&select_fd))
    {
        GIOChannel *healthcheck_channel = g_io_channel_unix_new (select_fd);
        g_io_add_watch (healthcheck_channel, G_IO_IN, cmsg_sld_healthcheck_read, NULL);

        healthcheck_start ();
        return FALSE;
    }

    return TRUE;
}

/**
 * Handles SIGUSR1 indicating that the daemon should dump the current
 * service information and state of the daemon to the debug file.
 */
static gboolean
debug_handler (gpointer user_data)
{
    FILE *fp;
    fp = fopen (DEBUG_FILE, "w");

    if (fp != NULL)
    {
        remote_sync_debug_dump (fp);
        data_debug_dump (fp);
        fclose (fp);
    }

    return G_SOURCE_CONTINUE;
}

/**
 * Handles SIGTERM and SIGINT signals indicating that the CMSG service listener
 * should shutdown cleanly.
 */
static gboolean
shutdown_handler (gpointer user_data)
{
    g_main_loop_quit (user_data);
    g_main_loop_unref (user_data);
    exit (EXIT_SUCCESS);
}

/* *INDENT-OFF* */
static void
help (void)
{
    printf ("Usage: cmsg_sld [-r <runfile>]\n"
            "  -r   use <runfile>\n");
}
/* *INDENT-ON* */

int
main (int argc, char **argv)
{
    GMainLoop *loop = NULL;
    const char *run_file = NULL;
    int i;
    FILE *fp;

    /* Parse options */
    while ((i = getopt (argc, argv, "r:")) != -1)
    {
        switch (i)
        {
        case 'r':
            run_file = optarg;
            break;
        case '?':
        case 'h':
        default:
            help ();
            return EXIT_SUCCESS;
        }
    }

    loop = g_main_loop_new (NULL, FALSE);

    g_unix_signal_add (SIGTERM, shutdown_handler, loop);
    g_unix_signal_add (SIGINT, shutdown_handler, loop);
    g_unix_signal_add (SIGUSR1, debug_handler, loop);

    /* Avoid exiting upon receiving an unintentional SIGPIPE */
    signal (SIGPIPE, SIG_IGN);

    /* register for healthchecking */
    g_timeout_add_seconds (1, cmsg_sld_healthcheck_init, NULL);

    data_init ();
    configuration_server_init ();

    /* Create run file */
    if (run_file)
    {
        fp = fopen (run_file, "w");
        if (!fp)
        {
            return EXIT_FAILURE;
        }
        fclose (fp);
    }

    g_main_loop_run (loop);
    g_main_loop_unref (loop);

    /* Should never exit */
    return EXIT_FAILURE;
}
