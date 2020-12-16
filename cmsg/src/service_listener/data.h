/**
 * data.h
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __DATA_H_
#define __DATA_H_

#include <stdio.h>
#include <netinet/in.h>
#include <glib.h>
#include <cmsg/cmsg_client.h>
#include "cmsg_types_auto.h"
#include "configuration_types_auto.h"

typedef struct _listener_data
{
    cmsg_client *client;
    uint32_t id;
    uint32_t pid;
} listener_data;

typedef struct _service_data_entry_s
{
    GList *servers;
    GList *listeners;
} service_data_entry;

void data_init (void);
void data_deinit (void);
void data_debug_dump (FILE *fp);
void data_add_server (cmsg_service_info *server_info, bool local);
void data_remove_server (const cmsg_service_info *server_info, bool local);
void data_remove_servers_by_addr (struct in_addr addr);
GList *data_get_servers_by_addr (uint32_t addr);
void data_remove_by_pid (int pid);
void data_add_listener (const cmsg_sld_listener_info *info);
void data_remove_listener (const cmsg_sld_listener_info *info);
service_data_entry *get_service_entry_or_create (const char *service, bool create);

#endif /* __DATA_H_ */
