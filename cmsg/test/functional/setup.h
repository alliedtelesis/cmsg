/*
 * Common setup functionality for the functional tests.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __SETUP_H_
#define __SETUP_H_

#include <cmsg_pthread_helpers.h>

/**
 * This informs the compiler that the function is, in fact, being used even though it
 * doesn't look like it. This is useful for static functions that get found by NovaProva
 * using debug symbols.
 */
#define USED __attribute__ ((used))

void cmsg_service_listener_mock_functions (void);
void cmsg_ps_mock_functions (void);
int sm_mock_cmsg_service_port_get (const char *name, const char *proto);
cmsg_client *create_client (cmsg_transport_type type, int family);
cmsg_server *create_server (cmsg_transport_type type, int family, pthread_t *thread);

#endif /* __SETUP_H_ */
