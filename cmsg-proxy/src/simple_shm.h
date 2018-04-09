/*
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __SIMPLE_SHM_H_
#define __SIMPLE_SHM_H_

#include <sys/types.h>

typedef void (*init_f) (void *shared_data);

typedef struct
{
    void *shared_data;
    size_t shared_data_size;
    key_t shared_mem_key;
    key_t shared_sem_key;
    uint8_t shared_sem_num;
    int shm_id;
    int sem_id;
    init_f init_func;
} simple_shm_info;

void *get_shared_memory (simple_shm_info *shm_info);

#endif /* __SIMPLE_SHM_H_ */
