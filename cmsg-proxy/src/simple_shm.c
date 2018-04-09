/*
 * Helper library for easily creating shared memory libraries
 *
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */

#include <config.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <assert.h>
#include <errno.h>
#include "simple_shm.h"

#define INIT_DELAY 20

/* mutex to avoid multiple threads from one process trying to set up shared memory */
pthread_mutex_t shared_mem_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * Initialises the semaphores for the shared memory.
 *
 * @param shm_info - Pointer to a simple_shm_info structure containing the information
 *                   about the shared memory to connect to.
 */
static void
init_shared_memory_semaphores (simple_shm_info *shm_info)
{
    union semun
    {
        int val;
        struct semid_ds *buf;
        unsigned short *array;
        struct seminfo *__buf;
    } arg;
    int sem_id;
    int i;

    shm_info->sem_id = semget (shm_info->shared_sem_key, shm_info->shared_sem_num,
                               0666 | IPC_CREAT);

    /* once created, we still need to init each semaphore's value to zero */
    if (shm_info->sem_id >= 0)
    {
        arg.val = 0;

        for (i = 0; i < shm_info->shared_sem_num; i++)
        {
            if (semctl (shm_info->sem_id, i, SETVAL, arg) < 0)
            {
                syslog (LOG_ERR, "Could not initialize semaphore %u", i);
            }
        }
    }
    else
    {
        syslog (LOG_ERR, "Could not create semaphore");
    }
}

/**
 * Initialises the shared memory.
 *
 * @param shm_info - Pointer to a simple_shm_info structure containing the information
 *                   about the shared memory to connect to.
 */
static void
init_shared_memory (simple_shm_info *shm_info)
{
    /* attach to the shared memory created */
    shm_info->shared_data = shmat (shm_info->shm_id, NULL, 0);

    /* Initialise the shared memory using the init function provided */
    shm_info->init_func (shm_info->shared_data);

    /* create a semaphore so writes from multiple processes are protected */
    init_shared_memory_semaphores (shm_info);
}


 /**
 * Waits until the shared memory has been initialised by the process that created
 * the memory.
 *
 * @param shm_info - Pointer to a simple_shm_info structure containing the information
 *                   about the shared memory to connect to.
 */
static void
wait_for_shared_memory_init (simple_shm_info *shm_info)
{
    uint32_t waited_secs = 0;

    /* the semaphore gets created after the shared memory is initialized */
    while ((shm_info->sem_id = semget (shm_info->shared_sem_key,
                                       shm_info->shared_sem_num, 0)) < 0)
    {
        /* another process is initializing the memory - wait until it's done */
        sleep (1);
        waited_secs++;
        assert (waited_secs < INIT_DELAY);
    }
}


/**
 * Map our pointer to the shared memory specified in the simple_shm_info structure.
 * If we're the first process to access the shared memory, then we'll also need to
 * create and initialise the shared memory.
 *
 * @param shm_info - Pointer to a simple_shm_info structure containing the information
 *                   about the shared memory to connect to.
 */
static void
_get_shared_memory (simple_shm_info *shm_info)
{
    /* try allocating a new shared memory segment */
    shm_info->shm_id = shmget (shm_info->shared_mem_key, shm_info->shared_data_size,
                               0666 | IPC_CREAT | IPC_EXCL);

    if (shm_info->shm_id < 0)
    {
        /* allocation failed because the memory segment already exists;
         * attach to the existing shared memory (and semaphore) */
        assert (errno == EEXIST);

        wait_for_shared_memory_init (shm_info);

        shm_info->shm_id = shmget (shm_info->shared_mem_key, 0, 0);
        assert (shm_info->shm_id >= 0);

        shm_info->shared_data = shmat (shm_info->shm_id, NULL, 0);
    }
    else
    {
        /* allocation succeeded; now the memory needs to be initialized */
        init_shared_memory (shm_info);
    }
}


/**
 * Gets the shared memory block specified in the simple_shm_info structure.
 *
 * @param shm_info - Pointer to a simple_shm_info structure containing the information
 *                   about the shared memory to connect to.
 *
 * @return pointer to the shared memory block
 */
void *
get_shared_memory (simple_shm_info *shm_info)
{
    /* The first time we access the shared memory, we need to
     * initialise our pointer so that it's mapped to the memory */
    if (shm_info->shared_data == NULL)
    {
        /* only one thread per process should go in here */
        pthread_mutex_lock (&shared_mem_mutex);
        if (shm_info->shared_data == NULL)
        {
            _get_shared_memory (shm_info);
        }
        pthread_mutex_unlock (&shared_mem_mutex);
    }

    assert (shm_info->shared_data != NULL && "Error mapping to shared memory");

    return shm_info->shared_data;
}
