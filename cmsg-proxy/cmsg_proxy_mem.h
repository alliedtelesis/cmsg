/*
 * CMSG proxy malloc/free utilities
 *
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_PROXY_MEM_H_
#define __CMSG_PROXY_MEM_H_

/* For developer debugging, uncomment the following line,
 * which will generate malloc info to a file on receiving SIGUSR2 signal */
//#define CMSG_PROXY_MEM_DEBUG
#define CMSG_PROXY_MEM_OUTPUT_FILE  "/tmp/cmsg-proxy-mem.output"

/* Wrappers for memory tracing */
#define CMSG_PROXY_CALLOC(n,sz)     cmsg_proxy_mem_calloc ((n), (sz), __FILE__,  __LINE__)
#define CMSG_PROXY_STRDUP(str)      cmsg_proxy_mem_strdup ((str), __FILE__,  __LINE__)
#define CMSG_PROXY_FREE(ptr)        cmsg_proxy_mem_free ((ptr),  __FILE__,  __LINE__)

void *cmsg_proxy_mem_calloc (size_t nmemb, size_t size, const char *filename, int line);
void *cmsg_proxy_mem_strdup (const char *str, const char *filename, int line);
void cmsg_proxy_mem_free (void *ptr, const char *filename, int line);
void cmsg_proxy_mem_init (int mtype);

#endif /* __CMSG_PROXY_MEM_H_ */
