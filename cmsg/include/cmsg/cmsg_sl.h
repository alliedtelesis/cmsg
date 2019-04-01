/**
 * cmsg_sl.h
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_SL_H_
#define __CMSG_SL_H_

typedef struct _cmsg_sl_info_s cmsg_sl_info;

typedef void (*cmsg_sl_event_handler_t) (cmsg_transport *transport, bool added);

const cmsg_sl_info *cmsg_service_listener_listen (const char *service_name,
                                                  cmsg_sl_event_handler_t handler);
void cmsg_service_listener_unlisten (const cmsg_sl_info *info);
int cmsg_service_listener_get_event_fd (const cmsg_sl_info *info);
void cmsg_service_listener_event_queue_process (const cmsg_sl_info *info);

#endif /* __CMSG_SL_H_ */
