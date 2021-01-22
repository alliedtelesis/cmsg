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
 * This function reads a uint32_t from an input array pointed to by *in, and
 * updates the bytes_remaining variable
 *
 * @param in - is a pointer to a pointer to the location to read the value
 * @param bytes_remaining - is a pointer the number of bytes left in the input array
 * @param value - is a pointer to variable to written from the input array
 */
void
cmsg_crypto_get32 (uint8_t **in, int *bytes_remaining, uint32_t *value)
{
    *value = *(*in) << 24;
    *value |= *(*in + 1) << 16;
    *value |= *(*in + 2) << 8;
    *value |= *(*in + 3);
    *in += 4;
    *bytes_remaining -= 4;
}

/**
 * This function reads a uint32_t from an input array pointed to by *in
 *
 * @param in - is a pointer to a pointer to the location to read the value
 * @param value - is a pointer to variable to written from the input array
 */
static void
cmsg_crypto_get32_direct (uint8_t *in, uint32_t *value)
{
    *value = in[0] << 24;
    *value |= in[1] << 16;
    *value |= in[2] << 8;
    *value |= in[3];
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

/**
 * Decrypt a CMSG buffer. If this is the first message from the client, then
 * a NONCE is read and used by the server to derive the shared AES secret key
 * used for encryption/decryption for subsequent traffic.
 *
 * @param sa_void - pointer to the SA structure for the communication
 * @param inbuf - a pointer to the data buffer to be encrypted
 * @param length - the length of the data buffer to be encrypted
 * @param outbuf - a pointer to the data buffer to receive the encrypted data
 * @param sa_derive_func - user supplied callback to derive the SA
 *
 * @return the number of bytes of decrypted data, or -1 on error
 */
int
cmsg_crypto_decrypt (cmsg_crypto_sa *sa, void *inbuf, int length, void *outbuf,
                     crypto_sa_derive_func_t sa_derive_func)
{
    uint8_t *in;
    int final_length = 0;
    int out_length = -1;
    int bytes_remaining = length;
    uint32_t magic;
    uint32_t encrypt_length;
    uint32_t type;

    if (!sa || !inbuf || !outbuf || !sa_derive_func)
    {
        return -1;
    }

    in = inbuf;
    cmsg_crypto_get32 (&in, &bytes_remaining, &magic);
    cmsg_crypto_get32 (&in, &bytes_remaining, &encrypt_length);
    cmsg_crypto_get32 (&in, &bytes_remaining, &type);

    if (sa->server && !sa->ctx_out_init)
    {
        if (type == CMSG_CRYPTO_TYPE_NONCE)
        {
            /* setup the SA now we have the nonce from the client */
            if (sa_derive_func (sa, in) < 0)
            {
                /* error message was given in the derive function */
                return -1;
            }

            return 0;
        }

        return -1;
    }

    if (type == CMSG_CRYPTO_TYPE_PAYLOAD)
    {
        /* decrypt the data for the message */
        if (EVP_DecryptInit_ex (&sa->ctx_in, EVP_aes_256_cbc (), NULL, sa->keydata,
                                NULL) != 1)
        {
            tracelog_openssl_error ();
            return -1;
        }

        if (EVP_DecryptUpdate (&sa->ctx_in, outbuf, &out_length, in, bytes_remaining) == 1)
        {
            if (EVP_DecryptFinal (&sa->ctx_in, outbuf + out_length, &final_length) == 1)
            {
                out_length += final_length;
            }
            else
            {
                tracelog_openssl_error ();
                out_length = -1;
            }
        }
        else
        {
            tracelog_openssl_error ();
            out_length = -1;
        }
    }

    return out_length;
}

/**
 * Parse the crypto header to check that it is valid.
 *
 * @param header - The crypto header to parse.
 *
 * @returns The full message length on success, -1 on failure.
 */
int
cmsg_crypto_parse_header (uint8_t *header)
{
    uint32_t msg_length;
    uint32_t magic = 0;

    cmsg_crypto_get32_direct (header, &magic);
    cmsg_crypto_get32_direct (&header[4], &msg_length);
    if (magic != CMSG_CRYPTO_MAGIC || msg_length == 0)
    {
        return -1;
    }

    return msg_length;
}
