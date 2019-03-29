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
void data_add_subscription (const cmsg_pssd_subscription_info *info);
void data_remove_subscription (const cmsg_pssd_subscription_info *info);

#endif /* __DATA_H_ */
