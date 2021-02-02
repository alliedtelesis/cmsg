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

struct cmsg_forwarding_server_data
{
    const uint8_t *msg;
    size_t len;
    size_t pos;
    void *user_data;
};

void cmsg_transport_tcp_init (cmsg_transport *transport);
void cmsg_transport_oneway_tcp_init (cmsg_transport *transport);
void cmsg_transport_oneway_cpumail_init (cmsg_transport *transport);
void cmsg_transport_loopback_init (cmsg_transport *transport);
void cmsg_transport_tipc_broadcast_init (cmsg_transport *transport);
void cmsg_transport_rpc_unix_init (cmsg_transport *transport);
void cmsg_transport_oneway_unix_init (cmsg_transport *transport);
void cmsg_transport_udt_init (cmsg_transport *transport);
void cmsg_transport_forwarding_init (cmsg_transport *transport);

int connect_nb (int sockfd, const struct sockaddr *addr, socklen_t addrlen, int timeout);
ssize_t cmsg_transport_socket_send (int sockfd, const void *buf, size_t len, int flags);
ssize_t cmsg_transport_socket_recv (int sockfd, void *buf, size_t len, int flags);

cmsg_status_code cmsg_transport_client_recv (cmsg_transport *transport,
                                             const ProtobufCServiceDescriptor *descriptor,
                                             ProtobufCMessage **messagePtPt);

int32_t cmsg_transport_connect (cmsg_transport *transport);
int32_t cmsg_transport_accept (cmsg_transport *transport);
int32_t cmsg_transport_set_connect_timeout (cmsg_transport *transport, uint32_t timeout);
int32_t cmsg_transport_set_send_timeout (cmsg_transport *transport, uint32_t timeout);
int32_t cmsg_transport_set_recv_peek_timeout (cmsg_transport *transport, uint32_t timeout);
int32_t cmsg_transport_apply_send_timeout (cmsg_transport *transport, int sockfd);
int32_t cmsg_transport_apply_recv_timeout (cmsg_transport *transport, int sockfd);

cmsg_transport_info *cmsg_transport_info_create (const cmsg_transport *transport);
void cmsg_transport_info_free (cmsg_transport_info *transport_info);
cmsg_transport *cmsg_transport_info_to_transport (const cmsg_transport_info
                                                  *transport_info);
bool cmsg_transport_info_compare (const cmsg_transport_info *transport_info_a,
                                  const cmsg_transport_info *transport_info_b);
cmsg_transport_info *cmsg_transport_info_copy (const cmsg_transport_info *transport_info);
void cmsg_transport_tcp_cache_set (struct in_addr *address, bool present);

void cmsg_transport_forwarding_func_set (cmsg_transport *transport,
                                         cmsg_forwarding_transport_send_f send_func);
void cmsg_transport_forwarding_user_data_set (cmsg_transport *transport, void *user_data);
void *cmsg_transport_forwarding_user_data_get (cmsg_transport *transport);

#endif /* __CMSG_TRANSPORT_PRIVATE_H_ */
