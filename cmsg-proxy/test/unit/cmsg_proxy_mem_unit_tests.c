/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */

#define _GNU_SOURCE
#include <np.h>
#include "cmsg_proxy_mem.c"
#include "cmsg_proxy_unit_tests_proxy_def.h"
#include "cmsg_proxy_unit_tests_api_auto.h"

/**
 * Function Tested: cmsg_proxy_mem_calloc()
 *
 * Tests that the function returns a pointer to dynamically allocated memory
 */
void
test_cmsg_proxy_mem_calloc (void)
{
    int *ptr = NULL;

    ptr = CMSG_PROXY_CALLOC (1, sizeof (int));

    NP_ASSERT_PTR_NOT_EQUAL (ptr, NULL);

    CMSG_PROXY_FREE (ptr);
}

/**
 * Function Tested: cmsg_proxy_mem_asprintf()
 *
 * Tests that the function returns a printed string in
 * dynamically allocated memory
 */
void
test_cmsg_proxy_mem_asprintf (void)
{
    char *str = NULL;

    CMSG_PROXY_ASPRINTF (&str, "%s", "TEST");

    NP_ASSERT_STR_EQUAL (str, "TEST");

    CMSG_PROXY_FREE (str);
}

/**
 * Function Tested: cmsg_proxy_mem_strdup()
 *
 * Tests that the function returns a printed string in
 * dynamically allocated memory
 */
void
test_cmsg_proxy_mem_strdup (void)
{
    char *str = NULL;

    str = CMSG_PROXY_STRDUP ("TEST");

    NP_ASSERT_STR_EQUAL (str, "TEST");

    CMSG_PROXY_FREE (str);
}

/**
 * Function Tested: cmsg_proxy_mem_strndup()
 *
 * Tests that the function returns a printed string of up
 * to X characters in dynamically allocated memory
 */
void
test_cmsg_proxy_mem_strndup (void)
{
    char *str = NULL;

    str = CMSG_PROXY_STRNDUP ("TEST1234", 6);

    NP_ASSERT_STR_EQUAL (str, "TEST12");

    CMSG_PROXY_FREE (str);
}

/**
 * Function Tested: test_cmsg_proxy_mem_free()
 *
 * The above tests have already tested that the function correctly
 * frees memory however test that the function handles a NULL input.
 */
void
test_cmsg_proxy_mem_free__handles_NULL (void)
{
    CMSG_PROXY_FREE (NULL);
}
