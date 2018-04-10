/*
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */
#ifndef __CMSG_TRANSPORT_PRIVATE_H_
#define __CMSG_TRANSPORT_PRIVATE_H_

/* When connecting the transport specify that the default timeout value should
 * be used with the connect call */
#define CONNECT_TIMEOUT_DEFAULT -1

/* This value is used to limit the timeout for client message peek to 100s */
#define MAX_CLIENT_PEEK_LOOP (100)

/* This value is used to limit the timeout for server message peek to 10s */
#define MAX_SERVER_PEEK_LOOP (10)

/* For transport related errors */
#define CMSG_LOG_TRANSPORT_ERROR(transport, msg, ...) \
    do { \
         syslog (LOG_ERR | LOG_LOCAL6, "CMSG(%d).tport.%s%s: " msg, __LINE__, transport->parent_obj_id, transport->tport_id, ## __VA_ARGS__); \
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

cmsg_status_code cmsg_transport_client_recv (cmsg_transport *transport,
                                             const ProtobufCServiceDescriptor *descriptor,
                                             ProtobufCMessage **messagePtPt);

cmsg_status_code cmsg_transport_peek_for_header (cmsg_recv_func recv_wrapper,
                                                 cmsg_transport *transport, int32_t socket,
                                                 time_t seconds_to_wait,
                                                 cmsg_header *header_received);

#endif /* __CMSG_TRANSPORT_PRIVATE_H_ */
