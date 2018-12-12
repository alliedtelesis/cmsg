/*
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_PROXY_HTTP_STREAMING_API_H_
#define __CMSG_PROXY_HTTP_STREAMING_API_H_

#include <stdint.h>
#include <cmsg/cmsg_client.h>

cmsg_client *cmsg_proxy_http_streaming_api_create_client (void);
void cmsg_proxy_http_streaming_api_close_connection (cmsg_client *client, uint32_t id);
bool cmsg_proxy_http_streaming_api_send_response (cmsg_client *client, uint32_t stream_id,
                                                  ProtobufCMessage *send_msg);
bool cmsg_proxy_http_streaming_api_send_file_response (cmsg_client *client,
                                                       uint32_t stream_id, uint8_t *data,
                                                       ssize_t length);
bool cmsg_proxy_http_streaming_api_set_json_data_headers (cmsg_client *client,
                                                          uint32_t stream_id);
bool cmsg_proxy_http_streaming_api_set_file_data_headers (cmsg_client *client,
                                                          uint32_t stream_id,
                                                          const char *file_name,
                                                          uint32_t file_size);

#endif /* __CMSG_PROXY_HTTP_STREAMING_API_H_ */
