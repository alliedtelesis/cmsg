/**
 * @file cmsg_crypto.c
 *
 * Copyright 2020, Allied Telesis Labs New Zealand, Ltd
 */

#include "cmsg_private.h"
#include "cmsg_crypto.h"

#define tracelog_openssl_error() \
    do { \
        int __e; \
        while ((__e = ERR_get_error ()) != 0) \
        { \
            tracelog ("cmsg-openssl", "%s", ERR_error_string(__e, NULL)); \
        } \
    } while (0)

/* Define a magic value for CMSG crypto traffic. This magic value plus
 * the length of the encrypted buffer are passed as a special security header
 * before the encrypted data */
#define CMSG_CRYPTO_MAGIC        0xa5a50001

/* the message types used for CMSG crypto communication */
#define CMSG_CRYPTO_TYPE_NONCE      1
#define CMSG_CRYPTO_TYPE_PAYLOAD    2

/* Header prepended to encrypted CMSG traffic */
typedef struct _cmsg_crypto_secure_header_s
{
    uint32_t magic;
    uint32_t length;
    uint32_t type;
} cmsg_crypto_secure_header;

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

/**
 * This function writes a uint32_t into an output array pointed to by *out, and
 * updates the bytes_used variable
 *
 * @param out - is a pointer to a pointer to the location to write the value
 * @param bytes_used - is a pointer the number of bytes left in the output array
 * @param value - is the value to write to the output array
 */
static void
cmsg_crypto_put32 (uint8_t **out, uint32_t *bytes_used, uint32_t value)
{
    *(*out) = ((value) >> 24) & 0xFF;
    *(*out + 1) = ((value) >> 16) & 0xFF;
    *(*out + 2) = ((value) >> 8) & 0xFF;
    *(*out + 3) = (value) & 0xFF;
    *out += 4;
    *bytes_used += 4;
}

/**
 * Encrypt a buffer of bytes.
 *
 * @param sa_void - pointer to the SA structure for the communication
 * @param inbuf - a pointer to the data buffer to be encrypted
 * @param length - the length of the data buffer to be encrypted
 * @param outbuf - a pointer to the data buffer to receive the encrypted data
 * @param out_size - the size of the output buffer
 *
 * @return the number of bytes of encrypted data, or -1 on error
 */
int
cmsg_crypto_encrypt (cmsg_crypto_sa *sa, void *inbuf, int length,
                     void *outbuf, int outbuf_size)
{
    uint8_t *out;
    int final_length = 0;
    int out_length = 0;
    uint32_t bytes_used;

    if (sa == NULL)
    {
        return -1;
    }

    /* encrypt the data for the message */
    if (EVP_EncryptInit_ex (&sa->ctx_out, EVP_aes_256_cbc (), NULL, sa->keydata, NULL) != 1)
    {
        tracelog_openssl_error ();
        return -1;
    }

    out = outbuf;
    outbuf += sizeof (cmsg_crypto_secure_header);
    if (EVP_EncryptUpdate (&sa->ctx_out, outbuf, &out_length, inbuf, length) == 1)
    {
        if (EVP_EncryptFinal (&sa->ctx_out, outbuf + out_length, &final_length) == 1)
        {
            out_length += final_length + sizeof (cmsg_crypto_secure_header);
            cmsg_crypto_put32 (&out, &bytes_used, CMSG_CRYPTO_MAGIC);
            cmsg_crypto_put32 (&out, &bytes_used, out_length);
            cmsg_crypto_put32 (&out, &bytes_used, CMSG_CRYPTO_TYPE_PAYLOAD);
        }
        else
        {
            tracelog_openssl_error ();
            out_length = -1;
        }
    }
    else
    {
        out_length = -1;
    }

    return out_length;
}
