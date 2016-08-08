#ifndef __CMSG_COMPOSITE_CLIENT_H_
#define __CMSG_COMPOSITE_CLIENT_H_

int32_t cmsg_composite_client_add_child (cmsg_client *composite_client,
                                         cmsg_client *client);
int32_t cmsg_composite_client_delete_child (cmsg_client *composite_client,
                                            cmsg_client *client);
cmsg_client *cmsg_composite_client_new (const ProtobufCServiceDescriptor *descriptor);

#endif /* __CMSG_COMPOSITE_CLIENT_H_ */
