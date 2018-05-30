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

#endif /* __CMSG_PROXY_HTTP_STREAMING_API_H_ */
