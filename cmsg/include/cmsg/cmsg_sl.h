/**
 * cmsg_sl.h
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_SL_H_
#define __CMSG_SL_H_

typedef void (*cmsg_service_listener_event_func) (const cmsg_service_info *info,
                                                  bool added);

void cmsg_service_listener_subscribe (const char *service_name,
                                      cmsg_service_listener_event_func func);
void cmsg_service_listener_unsubscribe (const char *service_name);

#endif /* __CMSG_SL_H_ */
