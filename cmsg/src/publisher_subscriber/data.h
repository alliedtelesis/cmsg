/**
 * data.h
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __DATA_H_
#define __DATA_H_

#include <stdio.h>
#include <glib.h>
#include "configuration_types_auto.h"

void data_init (void);
void data_deinit (void);
void data_debug_dump (FILE *fp);
bool data_add_subscription (const cmsg_subscription_info *info);
void data_remove_subscription (const cmsg_subscription_info *info);
void data_remove_subscriber (const cmsg_transport_info *sub_transport);
void data_check_remote_entries (void);
GList *data_get_remote_subscriptions (void);
void data_add_local_subscription (const cmsg_subscription_info *info);
void data_remove_local_subscription (const cmsg_subscription_info *info);
void data_remove_local_subscriptions_for_addr (uint32_t addr);
void data_add_publisher (const char *service, cmsg_transport_info *transport_info);
void data_remove_publisher (const char *service, cmsg_transport_info *transport_info);
void data_get_subscription_info_for_service (const char *service,
                                             cmsg_subscription_methods *msg);
void data_get_subscription_info_for_service_free (cmsg_subscription_methods *msg);

#endif /* __DATA_H_ */
