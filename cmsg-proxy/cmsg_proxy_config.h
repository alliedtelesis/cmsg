/*
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_PROXY_CONFIG_H_
#define __CMSG_PROXY_CONFIG_H_

/* By default all WEB APIs require the maximum user privilege (15) */
#define DEFAULT_READONLY_PRIVILEGE  15

typedef enum _cmsg_proxy_log_mode
{
    CMSG_PROXY_LOG_NONE,    /* No logging */
    CMSG_PROXY_LOG_SETS,    /* Log PUT/POST/DELETE requests only */
    CMSG_PROXY_LOG_ALL,     /* Log all requests */
} cmsg_proxy_log_mode;

void cmsg_proxy_config_set_logging_mode (cmsg_proxy_log_mode mode);
cmsg_proxy_log_mode cmsg_proxy_config_get_logging_mode (void);
void cmsg_proxy_config_set_readonly_privilege (uint32_t privilege);
uint32_t cmsg_proxy_config_get_readonly_privilege (void);

#endif /* __CMSG_PROXY_CONFIG_H_ */
