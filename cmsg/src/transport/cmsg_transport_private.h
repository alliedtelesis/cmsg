/*
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */
#ifndef __CMSG_TRANSPORT_PRIVATE_H_
#define __CMSG_TRANSPORT_PRIVATE_H_

#include "cmsg_types_auto.h"
#include "cmsg_transport.h"

/* The default connect timeout value in seconds */
#define CONNECT_TIMEOUT_DEFAULT 5

/* The default send timeout value in seconds */
#define SEND_TIMEOUT_DEFAULT 5

/* The default recv timeout value in seconds */
#define RECV_TIMEOUT_DEFAULT 5

/* The default timeout value for peeking for the header of a received message in seconds */
#define RECV_HEADER_PEEK_TIMEOUT_DEFAULT 10

/* For transport related errors */
#define CMSG_LOG_TRANSPORT_ERROR(transport, msg, ...) \
    do { \
        syslog ((transport->suppress_errors ? LOG_DEBUG : LOG_ERR) | LOG_LOCAL6, \
                "CMSG(%d).tport.%s%s: " msg, \
                __LINE__, transport->parent_obj_id, transport->tport_id, ## __VA_ARGS__); \
    } while (0)

void cmsg_transport_tipc_init (cmsg_transport *transport);
void cmsg_transport_tcp_init (cmsg_transport *transport);
void cmsg_transport_oneway_tipc_init (cmsg_transport *transport);
void cmsg_transport_oneway_tcp_init (cmsg_transport *transport);
void cmsg_transport_oneway_cpumail_init (cmsg_transport *transport);
void cmsg_transport_loopback_init (cmsg_transport *transport);
void cmsg_transport_tipc_broadcast_init (cmsg_transport *transport);
void cmsg_transport_rpc_unix_init (cmsg_transport *transport);
void cmsg_transport_oneway_unix_init (cmsg_transport *transport);
void cmsg_transport_udt_init (cmsg_transport *transport);

int connect_nb (int sockfd, const struct sockaddr *addr, socklen_t addrlen, int timeout);

cmsg_status_code cmsg_transport_client_recv (cmsg_transport *transport,
                                             const ProtobufCServiceDescriptor *descriptor,
                                             ProtobufCMessage **messagePtPt);

cmsg_status_code cmsg_transport_peek_for_header (cmsg_recv_func recv_wrapper,
                                                 cmsg_transport *transport, int32_t socket,
                                                 time_t seconds_to_wait,
                                                 cmsg_header *header_received);

int32_t cmsg_transport_connect (cmsg_transport *transport);
int32_t cmsg_transport_accept (cmsg_transport *transport);
int32_t cmsg_transport_set_connect_timeout (cmsg_transport *transport, uint32_t timeout);
int32_t cmsg_transport_set_send_timeout (cmsg_transport *transport, uint32_t timeout);
int32_t cmsg_transport_set_recv_peek_timeout (cmsg_transport *transport, uint32_t timeout);
int32_t cmsg_transport_apply_send_timeout (cmsg_transport *transport, int sockfd);
int32_t cmsg_transport_apply_recv_timeout (cmsg_transport *transport, int sockfd);

cmsg_transport_info *cmsg_transport_info_create (cmsg_transport *transport);
void cmsg_transport_info_free (cmsg_transport_info *transport_info);
cmsg_transport *cmsg_transport_info_to_transport (const cmsg_transport_info
                                                  *transport_info);
bool cmsg_transport_info_compare (const cmsg_transport_info *transport_info_a,
                                  const cmsg_transport_info *transport_info_b);

#endif /* __CMSG_TRANSPORT_PRIVATE_H_ */
