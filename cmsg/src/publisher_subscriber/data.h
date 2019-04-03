/**
 * data.h
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __DATA_H_
#define __DATA_H_

#include <stdio.h>
#include "configuration_types_auto.h"

void data_init (void);
void data_debug_dump (FILE *fp);
bool data_add_subscription (const cmsg_subscription_info *info);
void data_remove_subscription (const cmsg_subscription_info *info);
void data_remove_subscriber (const cmsg_transport_info *sub_transport);
void data_check_remote_entries (void);
GList *data_get_remote_subscriptions (void);

#endif /* __DATA_H_ */
