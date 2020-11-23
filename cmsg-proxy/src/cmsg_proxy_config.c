/*
 * A shared memory library that contains the configuration
 * for CMSG proxy.
 *
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */

#include <stdint.h>
#include <stddef.h>
#include "simple_shm.h"
#include "cmsg_proxy_config.h"

typedef struct
{
    cmsg_proxy_log_mode log_mode;
} cmsg_proxy_config;

static void init_config (cmsg_proxy_config *proxy_conf);

static simple_shm_info shm_info = {
    .shared_data = NULL,
    .shared_data_size = sizeof (cmsg_proxy_config),
    .shared_mem_key = 0x436d5072,   /* Hex value of "CmPr" */
    .shared_sem_key = 0x436d5072,   /* Hex value of "CmPr" */
    .shared_sem_num = 1,
    .shm_id = -1,
    .sem_id = -1,
    .init_func = init_config,
};

static void
init_config (cmsg_proxy_config *proxy_conf)
{
    proxy_conf->log_mode = CMSG_PROXY_LOG_NONE;
}

void
cmsg_proxy_config_set_logging_mode (cmsg_proxy_log_mode log_mode)
{
    cmsg_proxy_config *config = get_shared_memory (&shm_info);

    config->log_mode = log_mode;
}

cmsg_proxy_log_mode
cmsg_proxy_config_get_logging_mode (void)
{
    cmsg_proxy_config *config = get_shared_memory (&shm_info);

    return config->log_mode;
}
