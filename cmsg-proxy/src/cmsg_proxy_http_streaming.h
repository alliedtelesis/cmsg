/*
 * CMSG web socket functionality
 *
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_PROXY_WEB_SOCKETS_H_
#define __CMSG_PROXY_WEB_SOCKETS_H_

bool
cmsg_proxy_setup_streaming (void *connection, json_t **input_json_obj,
                            const ProtobufCMessageDescriptor *input_msg_descriptor,
                            const ProtobufCMessageDescriptor *output_msg_descriptor,
                            uint32_t *streaming_id);
void cmsg_proxy_remove_stream_by_id (uint32_t id);
void cmsg_proxy_streaming_init (void);
void cmsg_proxy_streaming_deinit (void);

#endif /* __CMSG_PROXY_WEB_SOCKETS_H_ */
