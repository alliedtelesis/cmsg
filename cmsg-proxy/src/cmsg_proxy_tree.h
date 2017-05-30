/*
 * CMSG proxy tree construction and functionality
 *
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_PROXY_TREE_H_
#define __CMSG_PROXY_TREE_H_

typedef struct
{
    char *key;
    char *value;
} cmsg_url_parameter;

void cmsg_proxy_tree_init (void);
void cmsg_proxy_tree_deinit (void);

cmsg_client *_cmsg_proxy_find_client_by_service (const ProtobufCServiceDescriptor
                                                 *service_descriptor);
const cmsg_service_info *_cmsg_proxy_find_service_from_url_and_verb (const char *url,
                                                                     cmsg_http_verb verb,
                                                                     GList
                                                                     **url_parameters);
cmsg_url_parameter *_cmsg_proxy_create_url_parameter (const char *key, const char *value);
void _cmsg_proxy_free_url_parameter (gpointer ptr);

#endif /* __CMSG_PROXY_TREE_H_ */
