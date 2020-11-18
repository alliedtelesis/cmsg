/**
 * cmsg_sl.h
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_SL_H_
#define __CMSG_SL_H_

#include <stdbool.h>
#include <cmsg/cmsg_transport.h>

typedef struct _cmsg_sl_info_s cmsg_sl_info;

typedef bool (*cmsg_sl_event_handler_t) (const cmsg_transport *transport, bool added,
                                         void *user_data);

const cmsg_sl_info *cmsg_service_listener_listen (const char *service_name,
                                                  cmsg_sl_event_handler_t handler,
                                                  void *user_data);
void cmsg_service_listener_unlisten (const cmsg_sl_info *info);
int cmsg_service_listener_get_event_fd (const cmsg_sl_info *info);
bool cmsg_service_listener_event_queue_process (const cmsg_sl_info *info);
bool cmsg_service_listener_wait_for_unix_server (const char *service_name, long seconds);
void cmsg_service_listener_event_loop_data_set (const cmsg_sl_info *info, void *data);
void *cmsg_service_listener_event_loop_data_get (const cmsg_sl_info *info);

#endif /* __CMSG_SL_H_ */
