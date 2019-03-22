/**
 * data.h
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __DATA_H_
#define __DATA_H_

#include <stdio.h>
#include "cmsg_types_auto.h"

void data_init (void);
void data_debug_dump (FILE *fp);
void data_add_server (const cmsg_service_info *server_info);
void data_remove_server (const cmsg_service_info *server_info);

#endif /* __DATA_H_ */
