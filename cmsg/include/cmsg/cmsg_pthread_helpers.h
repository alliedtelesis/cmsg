/*
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */
#ifndef __CMSG_PTHREAD_HELPERS_H_
#define __CMSG_PTHREAD_HELPERS_H_

#include <stdbool.h>
#include <pthread.h>
#include "cmsg_server.h"

bool cmsg_pthread_server_init (pthread_t *thread, cmsg_server *server);

#endif /* __CMSG_PTHREAD_HELPERS_H_ */
