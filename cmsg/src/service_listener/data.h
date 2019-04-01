/**
 * data.h
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __DATA_H_
#define __DATA_H_

#include <stdio.h>
#include <netinet/in.h>
#include "cmsg_types_auto.h"
#include "configuration_types_auto.h"

void data_init (void);
void data_debug_dump (FILE *fp);
void data_add_server (const cmsg_service_info *server_info);
void data_remove_server (const cmsg_service_info *server_info);
void data_remove_servers_by_addr (struct in_addr addr);
GList *data_get_servers_by_addr (uint32_t addr);
void data_add_listener (const cmsg_sld_listener_info *info);
void data_remove_listener (const cmsg_sld_listener_info *info);

#endif /* __DATA_H_ */
