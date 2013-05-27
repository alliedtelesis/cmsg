#include "protobuf-c-cmsg-notification.h"


cmsg_notification_publisher*
cmsg_notification_publisher_new (cmsg_transport*                   registration_server_transport,
                                 cmsg_transport*                   notification_publisher_transport,
                                 ProtobufCService*                 registration_server_service,
                                 const ProtobufCServiceDescriptor* notification_publisher_service);

int32_t
cmsg_notification_publisher_destroy (cmsg_notification_publisher* publisher);

int
cmsg_notification_publisher_get_server_socket (cmsg_notification_publisher* publisher);

int32_t
cmsg_notification_publisher_notify (cmsg_notification_publisher* publisher,
                                                    cmsg_notification_type       type,
                                                    ProtobufCMessage*                            msg);

cmsg_notification_subscriber*
cmsg_notification_subscriber_new (cmsg_transport*                   notification_subscriber_transport,
                                  cmsg_transport*                   registration_client_transport,
                                  ProtobufCService*                 notification_subscriber_service,
                                  const ProtobufCServiceDescriptor* registration_client_service);

int32_t
cmsg_notification_subscriber_destroy (cmsg_notification_subscriber* subscriber);

int
cmsg_notification_subscriber_get_server_socket (cmsg_notification_subscriber* subscriber);

int32_t
cmsg_notification_subscriber_register (cmsg_notification_type type /*publisher addresss?*/);

int32_t
cmsg_notification_subscriber_unregister (cmsg_notification_type type /*publisher addresss?*/);
