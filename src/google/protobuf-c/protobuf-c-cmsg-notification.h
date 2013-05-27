#ifndef __CMSG_NOTIFICATION_H_
#define __CMSG_NOTIFICATION_H_


#include "protobuf-c-cmsg.h"
#include "protobuf-c-cmsg-transport.h"
#include "protobuf-c-cmsg-client.h"
#include "protobuf-c-cmsg-server.h"


typedef enum
{
  TYPE1,
  TYPE2
} cmsg_notification_type;

typedef struct
{
  cmsg_notification_type type;
  in_addr_t address;
} cmsg_notification_entry;


typedef struct
{
  cmsg_server* registration_server;     //receiveing register
  cmsg_client* notification_publisher;  //sending notifcation
} cmsg_notification_publisher;

typedef struct
{
  cmsg_server* notification_subscriber;   //receiving notifications
  cmsg_client* registration_client;       //registering
  //no std? timetravel back to 1985
  cmsg_notification_entry list[100]; //list <address, notification_type>
} cmsg_notification_subscriber;


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

#endif
