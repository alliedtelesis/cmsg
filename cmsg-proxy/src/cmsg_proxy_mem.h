/*
 * CMSG proxy malloc/free utilities
 *
 * Copyright 2017, Allied Telesis Labs New Zealand, Ltd
 */

#ifndef __CMSG_PROXY_MEM_H_
#define __CMSG_PROXY_MEM_H_

/* Wrappers for memory tracing */
#define CMSG_PROXY_CALLOC(n,sz)             cmsg_proxy_mem_calloc ((n), (sz), __FILE__,  __LINE__)
#define CMSG_PROXY_ASPRINTF(strp,fmt,...)   cmsg_proxy_mem_asprintf (__FILE__,  __LINE__, (strp), (fmt), ##__VA_ARGS__)
#define CMSG_PROXY_STRDUP(str)              cmsg_proxy_mem_strdup ((str), __FILE__,  __LINE__)
#define CMSG_PROXY_STRNDUP(str, len)        cmsg_proxy_mem_strndup ((str), (len), __FILE__,  __LINE__)
#define CMSG_PROXY_FREE(ptr)                cmsg_proxy_mem_free ((ptr),  __FILE__,  __LINE__)

void *cmsg_proxy_mem_calloc (size_t nmemb, size_t size, const char *filename, int line);
int cmsg_proxy_mem_asprintf (const char *filename, int line, char **strp,
                             const char *fmt, ...);
void *cmsg_proxy_mem_strdup (const char *str, const char *filename, int line);
void *cmsg_proxy_mem_strndup (const char *str, size_t len, const char *filename, int line);
void cmsg_proxy_mem_free (void *ptr, const char *filename, int line);
void cmsg_proxy_mem_init (void);

#endif /* __CMSG_PROXY_MEM_H_ */
