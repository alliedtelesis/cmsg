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

void _cmsg_proxy_library_handles_load (void);
void _cmsg_proxy_library_handles_close (void);
cmsg_client *_cmsg_proxy_find_client_by_service (const ProtobufCServiceDescriptor
                                                 *service_descriptor);
void _cmsg_proxy_clients_deinit (void);
void _cmsg_proxy_service_info_deinit (void);
void _cmsg_proxy_clients_init (void);
const cmsg_service_info *cmsg_proxy_service_info_get (const cmsg_proxy_api_info *api_info,
                                                      cmsg_http_verb verb);
const cmsg_service_info *_cmsg_proxy_find_service_from_url_and_verb (const char *url,
                                                                     cmsg_http_verb verb,
                                                                     GList
                                                                     **url_parameters);
cmsg_url_parameter *_cmsg_proxy_create_url_parameter (const char *key, const char *value);
void _cmsg_proxy_free_url_parameter (gpointer ptr);

#endif /* __CMSG_PROXY_TREE_H_ */
