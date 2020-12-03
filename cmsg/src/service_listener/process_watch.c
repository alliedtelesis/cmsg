/**
 * process_watch.c
 *
 * Implements the functionality for watching process exit events
 * via pidfds.
 *
 * Copyright 2020, Allied Telesis Labs New Zealand, Ltd
 */

#include <linux/unistd.h>
#include "data.h"
#include "process_watch.h"

typedef struct _pidfd_watch_entry
{
    int pid;
    int pidfd;
    guint source;
    int ref;
} pidfd_watch_entry;

static GHashTable *hash_table = NULL;

/**
 * Currently glibc does not provide a wrapper for this system call.
 * Therefore we define the following function for now.
 *
 * @param pid - The PID to create a file descriptor for.
 * @param flags - Must be specified as 0.
 *
 * @returns A file descriptor on success, -1 on failure.
 */
static int
pidfd_open (pid_t pid, unsigned int flags)
{
#ifdef __NR_pidfd_open
    return syscall (__NR_pidfd_open, pid, flags);
#else
    return -1;
#endif
}

/**
 * Callback function to read a pidfd (i.e. the process has exited).
 */
static int
pidfd_read (GIOChannel *source, GIOCondition condition, gpointer data)
{
    int pid = GPOINTER_TO_INT (data);

    data_remove_by_pid (pid);
    g_hash_table_remove (hash_table, &pid);

    return FALSE;
}

/**
 * Create a process watch for the given pid.
 *
 * @param pid - The process ID to watch for.
 */
static void
create_process_watch (pid_t pid)
{
    int fd;
    int saved_errno;
    GIOChannel *read_channel = NULL;
    pidfd_watch_entry *entry = NULL;

    fd = pidfd_open (pid, 0);
    saved_errno = errno;
    if (fd != -1)
    {
        entry = CMSG_CALLOC (1, sizeof (pidfd_watch_entry));

        read_channel = g_io_channel_unix_new (fd);

        entry->pid = pid;
        entry->pidfd = fd;
        entry->ref++;
        entry->source = g_io_add_watch (read_channel, G_IO_IN, pidfd_read,
                                        GINT_TO_POINTER (pid));
        g_io_channel_unref (read_channel);

        g_hash_table_insert (hash_table, &entry->pid, entry);
    }
    else
    {
        if (saved_errno == ESRCH)
        {
            /* The process with the given PID does not exist.
             * We assume it has crashed already. */
            data_remove_by_pid (pid);
        }
        else
        {
            syslog (LOG_ERR, "Failed to watch pid %u (%s)", pid, strerror (saved_errno));
        }
    }
}

/**
 * Free the memory associated with a process watch entry.
 *
 * @param data - The entry to free.
 */
static void
process_watch_entry_free (gpointer data)
{
    pidfd_watch_entry *entry = (pidfd_watch_entry *) data;

    close (entry->pidfd);
    g_source_remove (entry->source);

    CMSG_FREE (entry);
}

/**
 * Start watching for exit events for the process with the given pid.
 *
 * @param pid - The process ID to watch for.
 */
void
process_watch_add (pid_t pid)
{
    pidfd_watch_entry *entry = NULL;

    entry = (pidfd_watch_entry *) g_hash_table_lookup (hash_table, &pid);
    if (entry)
    {
        entry->ref++;
    }
    else
    {
        create_process_watch (pid);
    }
}

/**
 * Stop watching for exit events for the process with the given pid.
 *
 * @param pid - The process ID to stop watching for.
 */
void
process_watch_remove (pid_t pid)
{
    pidfd_watch_entry *entry = NULL;

    entry = (pidfd_watch_entry *) g_hash_table_lookup (hash_table, &pid);
    if (entry)
    {
        entry->ref--;
        if (entry->ref == 0)
        {
            g_hash_table_remove (hash_table, &pid);
        }
    }
    else
    {
        syslog (LOG_ERR, "Failed to find process watch for pid %d", pid);
    }
}


/**
 * Initialise the process watching functionality.
 */
void
process_watch_init (void)
{
    hash_table = g_hash_table_new_full (g_int_hash, g_int_equal, NULL,
                                        process_watch_entry_free);
    if (!hash_table)
    {
        syslog (LOG_ERR, "Failed to initialize process watch hash table");
        return;
    }
}

/**
 * Deinitialise the process watching functionality.
 */
void
process_watch_deinit (void)
{
    if (hash_table)
    {
        g_hash_table_remove_all (hash_table);
        g_hash_table_unref (hash_table);
        hash_table = NULL;
    }
}
