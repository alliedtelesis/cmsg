/*
 * CMSG proxy counters
 *
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */

#ifdef HAVE_COUNTERD
#include <stdio.h>
#include "cmsg_proxy_counters.h"
#include "cmsg_proxy_mem.h"

GHashTable *proxy_session_counter_table = NULL;

/**
 * Uninitialise all service-specific counter
 */
static void
_proxy_session_counter_deinit_all (void)
{
    if (proxy_session_counter_table)
    {
        g_hash_table_destroy (proxy_session_counter_table);
        proxy_session_counter_table = NULL;
    }
}

/**
 * Allocate new counter info and initialise
 *
 * @param service_name - CMSG proxy service name which is used to identify counter session
 *
 * @return newly allocated counter info or NULL on failure.
 */
static session_counter_info *
_proxy_session_counter_info_new (const char *service_name)
{
    session_counter_info *counter;
    char app_name[CNTRD_MAX_APP_NAME_LENGTH];
    int ret;

    counter = CMSG_PROXY_CALLOC (1, sizeof (session_counter_info));
    if (counter == NULL)
    {
        return NULL;
    }

    /* Choose counter session name using CMSG service name */
    snprintf (app_name, CNTRD_MAX_APP_NAME_LENGTH, "%s %s",
              CMSG_PROXY_COUNTER_APP_NAME_PREFIX, service_name);

    /* Initialise counters */
    ret = cntrd_app_init_app (app_name, CNTRD_APP_PERSISTENT, &counter->cntr_session);
    if (ret == CNTRD_APP_SUCCESS)
    {
        CMSG_PROXY_COUNTER_REGISTER (counter, "API Calls", cntr_api_calls);
        CMSG_PROXY_COUNTER_REGISTER (counter, "Error: Missing Client",
                                     cntr_error_missing_client);
        CMSG_PROXY_COUNTER_REGISTER (counter, "Error: Malformed Input",
                                     cntr_error_malformed_input);
        CMSG_PROXY_COUNTER_REGISTER (counter, "Error: API Call Failure",
                                     cntr_error_api_failure);
        CMSG_PROXY_COUNTER_REGISTER (counter, "Error: Missing Error_info",
                                     cntr_error_missing_error_info);
        CMSG_PROXY_COUNTER_REGISTER (counter, "Error: Protobuf to Json",
                                     cntr_error_protobuf_to_json);

        /* Tell cntrd not to destroy the counter data in the shared memory */
        cntrd_app_set_shutdown_instruction (app_name, CNTRD_SHUTDOWN_RESTART);
    }
    else
    {
        CMSG_PROXY_FREE (counter);
        counter = NULL;
    }

    return counter;
}

/**
 * Delete session counter
 */
static void
_proxy_session_counter_info_delete (session_counter_info *counter)
{
    if (counter)
    {
        /* Free counter session info but do not destroy counter data in the shared memory */
        cntrd_app_unInit_app (&counter->cntr_session, CNTRD_APP_PERSISTENT);

        CMSG_PROXY_FREE (counter);
    }
}

/**
 * Initialise service-specific counters
 *
 * @param service_info - CMSG proxy service
 */
void
cmsg_proxy_session_counter_init (const cmsg_service_info *service_info)
{
    session_counter_info *session_counter;
    const char *service_name = service_info->service_descriptor->name;

    /* Create counter info hash table for the first time */
    if (proxy_session_counter_table == NULL)
    {
        /* Use the service descriptor as the key which is a "const" generated by protobuf-c.
         * So use direct pointer comparison for keys. */
        proxy_session_counter_table =
            g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
                                   (GDestroyNotify) _proxy_session_counter_info_delete);
    }

    /* Check if there exists counter info for the service. Otherwise create a new one */
    session_counter = g_hash_table_lookup (proxy_session_counter_table,
                                           SESSION_KEY (service_info));
    if (session_counter == NULL)
    {
        session_counter = _proxy_session_counter_info_new (service_name);
        if (session_counter != NULL)
        {
            g_hash_table_insert (proxy_session_counter_table,
                                 (gpointer) SESSION_KEY (service_info),
                                 (gpointer) session_counter);
        }
    }
}

/**
 * Initialise couter
 */
void
cmsg_proxy_counter_init (void)
{
    char app_name[CNTRD_MAX_APP_NAME_LENGTH];
    int ret;

    /* If it's already initialised, return early */
    if (proxy_counter.cntr_session)
    {
        return;
    }

    /* Choose counter session name using CMSG service name */
    snprintf (app_name, CNTRD_MAX_APP_NAME_LENGTH, "%s",
              CMSG_PROXY_COUNTER_APP_NAME_PREFIX);

    /* Initialise counters */
    ret = cntrd_app_init_app (app_name, CNTRD_APP_PERSISTENT, &proxy_counter.cntr_session);
    if (ret == CNTRD_APP_SUCCESS)
    {
        CMSG_PROXY_COUNTER_REGISTER (&proxy_counter, "Unknown Service",
                                     cntr_unknown_service);
        CMSG_PROXY_COUNTER_REGISTER (&proxy_counter, "Service Info Loaded",
                                     cntr_service_info_loaded);
        CMSG_PROXY_COUNTER_REGISTER (&proxy_counter, "Service Info Unloaded",
                                     cntr_service_info_unloaded);
        CMSG_PROXY_COUNTER_REGISTER (&proxy_counter, "Client Creation Failed",
                                     cntr_client_create_failure);
        CMSG_PROXY_COUNTER_REGISTER (&proxy_counter, "Client Created", cntr_client_created);
        CMSG_PROXY_COUNTER_REGISTER (&proxy_counter, "Client Freed", cntr_client_freed);

        /* Tell cntrd not to destroy the counter data in the shared memory */
        cntrd_app_set_shutdown_instruction (app_name, CNTRD_SHUTDOWN_RESTART);
    }
}

/**
 * Deinitialise couter
 */
void
cmsg_proxy_counter_deinit (void)
{
    if (proxy_counter.cntr_session)
    {
        /* Free counter session info but do not destroy counter data in the shared memory */
        cntrd_app_unInit_app (&proxy_counter.cntr_session, CNTRD_APP_PERSISTENT);
        proxy_counter.cntr_session = NULL;
    }

    /* Clean up session counters */
    _proxy_session_counter_deinit_all ();
}
#endif /* HAVE_COUNTERD */
