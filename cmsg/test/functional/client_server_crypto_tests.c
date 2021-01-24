/*
 * Functional tests for client <-> server communication with encryption.
 *
 * Copyright 2021, Allied Telesis Labs New Zealand, Ltd
 */

#include <arpa/inet.h>
#include <np.h>
#include <stdint.h>
#include "cmsg_functional_tests_api_auto.h"
#include "cmsg_functional_tests_impl_auto.h"
#include "setup.h"

/* A 256 bit key */
unsigned char *key = (unsigned char *) "01234567890123456789012345678901";

static cmsg_server *server = NULL;
static pthread_t server_thread;

/**
 * Common functionality to run before each test case.
 */
static int USED
set_up (void)
{
    np_mock (cmsg_service_port_get, sm_mock_cmsg_service_port_get);

    /* Ignore SIGPIPE signal if it occurs */
    signal (SIGPIPE, SIG_IGN);

    cmsg_service_listener_mock_functions ();

    return 0;
}

/**
 * Common functionality to run at the end of each test case.
 */
static int USED
tear_down (void)
{
    NP_ASSERT_NULL (server);

    return 0;
}

void
cmsg_test_impl_simple_crypto_test (const void *service, const cmsg_bool_msg *recv_msg)
{
    cmsg_bool_msg send_msg = CMSG_BOOL_MSG_INIT;

    NP_ASSERT_TRUE (recv_msg->value);

    CMSG_SET_FIELD_VALUE (&send_msg, value, true);

    cmsg_test_server_simple_rpc_testSend (service, &send_msg);
}

static cmsg_crypto_sa *
sa_create (const struct sockaddr_storage *addr)
{
    cmsg_crypto_sa *sa;

    sa = cmsg_crypto_sa_alloc ();
    sa->server = true;

    return sa;
}

static int
sa_derive (cmsg_crypto_sa *sa, const uint8_t *nonce)
{
    /* save the key in the SA */
    memcpy (sa->keydata, key, 32);
    sa->keysize = 32;

    EVP_CIPHER_CTX_init (&sa->ctx_out);
    sa->ctx_out_init = TRUE;
    /* Initialise the AES256-CBC encryption */
    if (!EVP_EncryptInit_ex (&sa->ctx_out, EVP_aes_256_cbc (), NULL, key, NULL))
    {
        return -1;
    }

    EVP_CIPHER_CTX_init (&sa->ctx_in);
    sa->ctx_in_init = TRUE;
    /* Initialise the AES256-CBC decryption */
    if (!EVP_DecryptInit_ex (&sa->ctx_in, EVP_aes_256_cbc (), NULL, key, NULL))
    {
        return -1;
    }

    return 0;
}

static void
run_client_server_crypto_test (cmsg_transport_type type, int family)
{
    int ret = 0;
    cmsg_bool_msg send_msg = CMSG_BOOL_MSG_INIT;
    cmsg_bool_msg *recv_msg = NULL;
    cmsg_client *client = NULL;
    cmsg_crypto_sa *sa;

    server = create_server (type, family, &server_thread);
    cmsg_server_crypto_enable (server, sa_create, sa_derive);

    client = create_client (type, family);
    sa = cmsg_crypto_sa_alloc ();
    sa->server = false;
    cmsg_client_crypto_enable (client, sa, sa_derive);

    CMSG_SET_FIELD_VALUE (&send_msg, value, true);

    ret = cmsg_test_api_simple_rpc_test (client, &send_msg, &recv_msg);

    NP_ASSERT_EQUAL (ret, CMSG_RET_OK);
    NP_ASSERT_NOT_NULL (recv_msg);
    NP_ASSERT_TRUE (recv_msg->value);

    CMSG_FREE_RECV_MSG (recv_msg);

    pthread_cancel (server_thread);
    pthread_join (server_thread, NULL);
    cmsg_destroy_server_and_transport (server);
    server = NULL;

    cmsg_destroy_client_and_transport (client);
}

/**
 * Run the simple client <-> server test case with a TCP transport (IPv4).
 */
void
test_client_server_crypto_rpc_tcp (void)
{
    run_client_server_crypto_test (CMSG_TRANSPORT_RPC_TCP, AF_INET);
}

/**
 * Run the simple client <-> server test case with a TCP transport (IPv6).
 */
void
test_client_server_crypto_rpc_tcp6 (void)
{
    run_client_server_crypto_test (CMSG_TRANSPORT_RPC_TCP, AF_INET6);
}

/**
 * Run the simple client <-> server test case with a UNIX transport.
 */
void
test_client_server_crypto_rpc_unix (void)
{
    run_client_server_crypto_test (CMSG_TRANSPORT_RPC_UNIX, AF_UNSPEC);
}
