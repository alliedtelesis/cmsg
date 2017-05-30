/*
 * CMSG proxy counters
 *
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_PROXY_COUNTERS_H_
#define __CMSG_PROXY_COUNTERS_H_

#ifdef HAVE_COUNTERD
#include "cmsg_proxy.h"
#include "cntrd_app_defines.h"
#include "cntrd_app_api.h"

/* Global counters */
typedef struct _counter_info
{
    /* Counter session */
    void *cntr_session;

    /* Counters */
    void *cntr_unknown_service;
    void *cntr_service_info_loaded;
    void *cntr_service_info_unloaded;
    void *cntr_client_create_failure;
    void *cntr_client_created;
    void *cntr_client_freed;
} counter_info;

/* Per-service counters */
typedef struct _session_counter_info
{
    /* Counter session */
    void *cntr_session;

    /* Counters */
    void *cntr_api_calls;
    void *cntr_error_missing_client;
    void *cntr_error_malformed_input;
    void *cntr_error_api_failure;
    void *cntr_error_missing_error_info;
    void *cntr_error_protobuf_to_json;
} session_counter_info;

/* Global counters */
static counter_info proxy_counter;
/* Table to store per-session counters */
static GHashTable *proxy_session_counter_table = NULL;

void cmsg_proxy_session_counter_init (const cmsg_service_info *service_info);

#define CMSG_PROXY_COUNTER_REGISTER(info,str,cntr) \
    cntrd_app_register_ctr_in_group ((info)->cntr_session, (str), &(info)->cntr)

#define SESSION_KEY(service)    (service->service_descriptor)

/* Increment global counter */
#define CMSG_PROXY_COUNTER_INC(counter)     \
    cntrd_app_inc_ctr (proxy_counter.cntr_session, proxy_counter.counter)

/* Increment session counter */
#define CMSG_PROXY_SESSION_COUNTER_INC(service,counter)             \
    do {                                                            \
        session_counter_info *_sc;                                  \
        _sc = g_hash_table_lookup (proxy_session_counter_table,     \
                                   SESSION_KEY ((service)));        \
        if (_sc)                                                    \
        {                                                           \
            cntrd_app_inc_ctr (_sc->cntr_session, _sc->counter);    \
        }                                                           \
    } while (0)

void cmsg_proxy_counter_init (void);
void cmsg_proxy_counter_deinit (void);
#else
#define CMSG_PROXY_COUNTER_INC(counter)
#define CMSG_PROXY_SESSION_COUNTER_INC(service,counter)
#endif /* HAVE_COUNTERD */

#endif /* __CMSG_PROXY_COUNTERS_H_ */
