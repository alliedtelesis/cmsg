/*
 * CMSG proxy malloc/free utilities
 *
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */

#include <config.h>
#include <string.h>
#include <malloc.h>
#include <glib.h>
#include "cmsg_proxy_mem.h"

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

/**
 * Init function for CMSG proxy memory tracing
 *
 * @param mtype  - id to keep the record of malloc/free for CMSG proxy (0 indicates disable)
 */
void
cmsg_proxy_mem_init (int mtype)
{
    cmsg_proxy_mtype = mtype;
}
