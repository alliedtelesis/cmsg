/*
 * Copyright 2020, Allied Telesis Labs New Zealand, Ltd
 */
#ifndef __CMSG_CRYPTO_H_
#define __CMSG_CRYPTO_H_

#include <stdint.h>
#include <stdbool.h>

#include <openssl/crypto.h>
#include <openssl/objects.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/rand.h>
#include <openssl/err.h>

#define KEY_SIZE                32

typedef struct _cmsg_crypto_sa
{
    uint32_t id;
    EVP_CIPHER_CTX ctx_out;
    bool ctx_out_init;
    EVP_CIPHER_CTX ctx_in;
    bool ctx_in_init;
    EVP_PKEY *remote_static;
    bool server;
    uint8_t keydata[KEY_SIZE];
    uint32_t keysize;
} cmsg_crypto_sa;

typedef cmsg_crypto_sa *(*crypto_sa_create_func_t) (const struct sockaddr_storage *addr);

cmsg_crypto_sa *cmsg_crypto_sa_alloc (void);
void cmsg_crypto_sa_free (cmsg_crypto_sa *sa);

#endif /* __CMSG_CRYPTO_H_ */
