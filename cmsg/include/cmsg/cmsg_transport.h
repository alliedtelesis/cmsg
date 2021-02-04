/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#ifndef __CMSG_TRANSPORT_H_
#define __CMSG_TRANSPORT_H_

#include <netinet/in.h>

typedef struct cmsg_transport cmsg_transport;

typedef bool (*cmsg_forwarding_transport_send_f) (void *user_data, void *buff, int length);

cmsg_transport *cmsg_transport_copy (const cmsg_transport *transport);
struct in_addr cmsg_transport_ipv4_address_get (const cmsg_transport *transport);

#endif /* __CMSG_TRANSPORT_H_ */
