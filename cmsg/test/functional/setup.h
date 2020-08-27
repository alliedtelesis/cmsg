/*
 * Common setup functionality for the functional tests.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __SETUP_H_
#define __SETUP_H_

void cmsg_service_listener_daemon_start (void);
void cmsg_service_listener_daemon_stop (void);
void cmsg_service_listener_mock_functions (void);
void cmsg_ps_mock_functions (void);

#endif /* __SETUP_H_ */
