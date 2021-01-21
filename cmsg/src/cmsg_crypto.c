/**
 * @file cmsg_crypto.c
 *
 * Copyright 2020, Allied Telesis Labs New Zealand, Ltd
 */

#include "cmsg_private.h"
#include "cmsg_crypto.h"

/**
 * Allocate a CMSG SA structure.
 *
 * @returns Pointer to allocated structure on success, NULL otherwise.
 */
cmsg_crypto_sa *
cmsg_crypto_sa_alloc (void)
{
    return CMSG_CALLOC (1, sizeof (cmsg_crypto_sa));
}

/**
 * Cleanup a CMSG SA. Free the associated keys and encrypt/decrypt contexts and the
 * memory for the SA.
 *
 * @param sa - pointer to the SA structure.
 */
void
cmsg_crypto_sa_free (cmsg_crypto_sa *sa)
{
    if (sa)
    {
        EVP_PKEY_free (sa->remote_static);
        if (sa->ctx_out_init)
        {
            EVP_CIPHER_CTX_cleanup (&sa->ctx_out);
        }
        if (sa->ctx_in_init)
        {
            EVP_CIPHER_CTX_cleanup (&sa->ctx_in);
        }

        CMSG_FREE (sa);
    }
}
