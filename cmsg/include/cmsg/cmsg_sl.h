/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * cmsg_sl.h
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_SL_H_
#define __CMSG_SL_H_

#include <stdbool.h>
#include <netinet/in.h>
#include <glib.h>
#include <cmsg/cmsg_transport.h>

typedef bool (*cmsg_sl_event_handler_t) (const cmsg_transport *transport, bool added,
                                         void *user_data);

typedef struct _cmsg_sl_info_s
{
    char *service_name;
    cmsg_sl_event_handler_t handler;
    uint32_t id;
    void *user_data;
    GAsyncQueue *queue;
    int eventfd;
    void *event_loop_data;
    bool using_global_thread;
    pthread_mutex_t thread_mutex;
} cmsg_sl_info;

cmsg_sl_info *cmsg_service_listener_listen (const char *service_name,
                                            cmsg_sl_event_handler_t handler,
                                            void *user_data);
void cmsg_service_listener_unlisten (cmsg_sl_info *info);
int cmsg_service_listener_get_event_fd (cmsg_sl_info *info);
bool cmsg_service_listener_event_queue_process (cmsg_sl_info *info);
bool cmsg_service_listener_wait_for_unix_server (const char *service_name, long seconds);
bool cmsg_service_listener_wait_for_tcp_server (const char *service_name,
                                                struct in_addr *addr, int seconds);
void cmsg_service_listener_event_loop_data_set (cmsg_sl_info *info, void *data);
void *cmsg_service_listener_event_loop_data_get (cmsg_sl_info *info);

#endif /* __CMSG_SL_H_ */
