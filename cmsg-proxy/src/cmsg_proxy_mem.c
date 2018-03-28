/*
 * CMSG proxy malloc/free utilities
 *
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */

#include <config.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <malloc.h>
#include <glib.h>
#include <signal.h>
#include <cmsg/cmsg.h>
#include "cmsg_proxy_mem.h"
#include <gmem_diag.h>

/* For developer debugging, uncomment the following line,
 * which will generate malloc info to a file on receiving SIGUSR2 signal */
//#define CMSG_PROXY_MEM_DEBUG
#define CMSG_PROXY_MEM_OUTPUT_FILE  "/tmp/cmsg-proxy-mem.output"

/* CMSG proxy mtype id for memory tracing (0 means disabled) */
static int cmsg_proxy_mtype = 0;

/**
 * Wrapper function for calloc()
 *
 * @param  nmemb    - number of elements
 * @param  size     - size of an element
 * @param  filename - filename to record in malloc statistics
 * @param  line     - line number to record in malloc statistics
 * @return a pointer to the allocated memory or NULL on failure
 */
void *
cmsg_proxy_mem_calloc (size_t nmemb, size_t size, const char *filename, int line)
{
    void *ptr = NULL;

    ptr = calloc (nmemb, size);

    if (cmsg_proxy_mtype > 0)
    {
        g_mem_record_alloc (ptr, cmsg_proxy_mtype, filename, line);
    }

    return ptr;
}

/**
 * Wrapper function for asprintf()
 *
 * @param  filename - filename to record in malloc statistics
 * @param  line     - line number to record in malloc statistics
 * @param  strp     - where to put a pointer to the allocated memory
 * @param  fmt      - format string
 * @return the number of bytes written (excluding null terminator) if successful; -1 if unsuccessful
 */
int
cmsg_proxy_mem_asprintf (const char *filename, int line, char **strp, const char *fmt, ...)
{
    int ret;
    va_list ap;

    va_start (ap, fmt);

    ret = vasprintf (strp, fmt, ap);

    if (cmsg_proxy_mtype > 0)
    {
        g_mem_record_alloc (*strp, cmsg_proxy_mtype, filename, line);
    }

    va_end (ap);

    return ret;
}


/**
 * Wrapper function for strdup()
 *
 * @param  str      - pointer to the original string
 * @param  filename - filename to record in malloc statistics
 * @param  line     - line number to record in malloc statistics
 * @return a pointer to the allocated memory or NULL on failure
 */
void *
cmsg_proxy_mem_strdup (const char *str, const char *filename, int line)
{
    void *ptr;

    ptr = strdup (str);

    if (cmsg_proxy_mtype > 0)
    {
        g_mem_record_alloc (ptr, cmsg_proxy_mtype, filename, line);
    }

    return ptr;
}

/**
 * Wrapper function for strndup()
 *
 * @param  str      - pointer to the original string
 * @param  len      - number of bytes to be copied at most
 * @param  filename - filename to record in malloc statistics
 * @param  line     - line number to record in malloc statistics
 * @return a pointer to the allocated memory or NULL on failure
 */
void *
cmsg_proxy_mem_strndup (const char *str, size_t len, const char *filename, int line)
{
    void *ptr;

    ptr = strndup (str, len);

    if (cmsg_proxy_mtype > 0)
    {
        g_mem_record_alloc (ptr, cmsg_proxy_mtype, filename, line);
    }

    return ptr;
}

/**
 * Wrapper function for free()
 *
 * @param  ptr      - pointer to the memory block to free
 * @param  filename - filename to record in malloc statistics
 * @param  line     - line number to record in malloc statistics
 */
void
cmsg_proxy_mem_free (void *ptr, const char *filename, int line)
{
    if (ptr == NULL)
    {
        return;
    }

    if (cmsg_proxy_mtype > 0)
    {
        g_mem_record_free (ptr, cmsg_proxy_mtype, filename, line);
    }

    free (ptr);
}

#ifdef CMSG_PROXY_MEM_DEBUG
/**
 * Return a string representing for an mtype
 */
static char *
cmsg_proxy_mem_mtype_str (int mtype)
{
    if (mtype == cmsg_proxy_mtype)
    {
        return "CMSG Proxy";
    }
    else if (mtype == cmsg_proxy_mtype + 1)
    {
        return "CMSG";
    }
    else
    {
        return "Unknown";
    }
}

/**
 * Print out memory alloc/free records
 *
 * @param filename - output file name
 */
void
cmsg_proxy_mem_print (const char *filename)
{
    FILE *fp;

    fp = fopen (filename, "w");
    if (fp != NULL)
    {
        g_mem_records_print ((g_mem_fprintf_fn) fprintf, fp, cmsg_proxy_mem_mtype_str);

        fclose (fp);
    }
}

/**
 * Signal handle for SIGUSR2
 */
static void
cmsg_proxy_mem_sigusr2_handler (int signum)
{
    cmsg_proxy_mem_print (CMSG_PROXY_MEM_OUTPUT_FILE);
}

/**
 * Init function for CMSG proxy memory tracing
 */
void
cmsg_proxy_mem_init (void)
{
    cmsg_proxy_mtype = 1;

    /* Also turn on CMSG memory tracing */
    cmsg_malloc_init (mtype + 1);

    /* Use SIGUSR2 to dump the current memory allocation info */
    signal (SIGUSR2, cmsg_proxy_mem_sigusr2_handler);
}
#else
/**
 * Init function for CMSG proxy memory tracing
 */
void
cmsg_proxy_mem_init (void)
{
}
#endif /* CMSG_PROXY_MEM_DEBUG */
