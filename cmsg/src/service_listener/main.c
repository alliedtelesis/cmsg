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
#include "configuration.h"
#include "data.h"

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

    /* Avoid exiting upon receiving an unintentional SIGPIPE */
    signal (SIGPIPE, SIG_IGN);

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
