/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#include "cmsg_private.h"
#include "cmsg_client.h"
#include "cmsg_error.h"
#include "transport/cmsg_transport_private.h"
#include "cmsg_protobuf-c.h"
#include "cmsg_ant_result.h"

#ifdef HAVE_COUNTERD
#include "cntrd_app_defines.h"
#include "cntrd_app_api.h"
#endif
#include <fcntl.h>

/* This value controls how long a client waits to peek the header of a response
 * packet sent from the server in seconds. This value defaults to 100 seconds as
 * the server may take a long time to respond to the API call. */
#define CLIENT_RECV_HEADER_PEEK_TIMEOUT 100

static int32_t _cmsg_client_buffer_send_retry_once (cmsg_client *client,
                                                    uint8_t *queue_buffer,
                                                    uint32_t queue_buffer_size,
                                                    const char *method_name);

static int32_t _cmsg_client_queue_process_all_internal (cmsg_client *client);

static int32_t _cmsg_client_queue_process_all_direct (cmsg_client *client);

static int32_t _cmsg_client_buffer_send (cmsg_client *client, uint8_t *buffer,
                                         uint32_t buffer_size);

int32_t cmsg_client_counter_create (cmsg_client *client, char *app_name);

static void cmsg_client_invoke (ProtobufCService *service,
                                uint32_t method_index,
                                const ProtobufCMessage *input,
                                ProtobufCClosure closure, void *_closure_data);

static int32_t cmsg_client_queue_input (cmsg_client *client, uint32_t method_index,
                                        const ProtobufCMessage *input, bool *did_queue);

static void _cmsg_client_destroy (cmsg_client *client);
static int32_t _cmsg_client_send_bytes (cmsg_client *client, uint8_t *buffer,
                                        uint32_t buffer_len, const char *method_name);

static void cmsg_client_close_wrapper (cmsg_client *client);

static void
cmsg_client_invoke_init (cmsg_client *client, cmsg_transport *transport)
{
    // Note these may be subsequently overridden (e.g. composite client)
    client->invoke = cmsg_client_invoke;
    client->base_service.invoke = cmsg_client_invoke;

    if (transport)
    {
        switch (transport->type)
        {
        case CMSG_TRANSPORT_RPC_TCP:
        case CMSG_TRANSPORT_RPC_UNIX:
            client->invoke_send = cmsg_client_invoke_send;
            client->invoke_recv = cmsg_client_invoke_recv;
            break;
        case CMSG_TRANSPORT_ONEWAY_TCP:
        case CMSG_TRANSPORT_BROADCAST:
        case CMSG_TRANSPORT_ONEWAY_UNIX:
        case CMSG_TRANSPORT_FORWARDING:
            client->invoke_send = cmsg_client_invoke_send;
            client->invoke_recv = NULL;
            break;
        case CMSG_TRANSPORT_LOOPBACK:
            client->invoke_send = cmsg_client_invoke_send_direct;
            client->invoke_recv = cmsg_client_invoke_recv;
            break;
        default:
            assert (false && "Unknown transport type");
        }
    }
}

int32_t
cmsg_client_init (cmsg_client *client, cmsg_transport *transport,
                  const ProtobufCServiceDescriptor *descriptor)
{
    client->state = CMSG_CLIENT_STATE_INIT;

    if (transport)
    {
        client->base_service.destroy = NULL;
        client->_transport = transport;
        cmsg_transport_write_id (transport, descriptor->name);
        cmsg_transport_set_recv_peek_timeout (client->_transport,
                                              CLIENT_RECV_HEADER_PEEK_TIMEOUT);
    }

    //for compatibility with current generated code
    //this is a hack to get around a check when a client method is called
    client->descriptor = descriptor;
    client->base_service.descriptor = descriptor;

    cmsg_client_invoke_init (client, transport);

    client->client_destroy = _cmsg_client_destroy;
    client->send_bytes = _cmsg_client_send_bytes;

    client->self.object_type = CMSG_OBJ_TYPE_CLIENT;
    client->self.object = client;
    strncpy (client->self.obj_id, descriptor->name, CMSG_MAX_OBJ_ID_LEN);

    client->parent.object_type = CMSG_OBJ_TYPE_NONE;
    client->parent.object = NULL;

    if (pthread_mutex_init (&client->queue_mutex, NULL) != 0)
    {
        CMSG_LOG_CLIENT_ERROR (client, "Init failed for queue_mutex.");
        return CMSG_RET_ERR;
    }

    client->queue = g_queue_new ();
    client->queue_filter_hash_table = g_hash_table_new (g_str_hash, g_str_equal);

    if (pthread_cond_init (&client->queue_process_cond, NULL) != 0)
    {
        CMSG_LOG_CLIENT_ERROR (client, "Init failed for queue_process_cond.");
        return CMSG_RET_ERR;
    }

    if (pthread_mutex_init (&client->queue_process_mutex, NULL) != 0)
    {
        CMSG_LOG_CLIENT_ERROR (client, "Init failed for queue_process_mutex.");
        return CMSG_RET_ERR;
    }

    if (pthread_mutex_init (&client->invoke_mutex, NULL) != 0)
    {
        CMSG_LOG_CLIENT_ERROR (client, "Init failed for invoke_mutex.");
        return CMSG_RET_ERR;
    }

    if (pthread_mutex_init (&client->send_mutex, NULL) != 0)
    {
        CMSG_LOG_GEN_ERROR ("Init failed for send_mutex.");
        return CMSG_RET_ERR;
    }

    client->self_thread_id = pthread_self ();

    if (transport)
    {
        cmsg_client_queue_filter_init (client);
    }

    client->suppress_errors = false;

    return CMSG_RET_OK;
}

/*
 * This is an internal function which can be called from CMSG library.
 * Applications should use cmsg_client_new() instead.
 *
 * Create a new CMSG client (but without creating counters).
 */
cmsg_client *
cmsg_client_create (cmsg_transport *transport, const ProtobufCServiceDescriptor *descriptor)
{
    CMSG_ASSERT_RETURN_VAL (descriptor != NULL, NULL);

    cmsg_client *client = (cmsg_client *) CMSG_CALLOC (1, sizeof (cmsg_client));

    if (client)
    {
        if (cmsg_client_init (client, transport, descriptor) != CMSG_RET_OK)
        {
            CMSG_FREE (client);
            return NULL;
        }
    }
    else
    {
        CMSG_LOG_GEN_ERROR ("[%s%s] Unable to create client.", descriptor->name,
                            transport->tport_id);
    }

    return client;
}


/*
 * Create a new CMSG client.
 */
cmsg_client *
cmsg_client_new (cmsg_transport *transport, const ProtobufCServiceDescriptor *descriptor)
{
    cmsg_client *client;
    client = cmsg_client_create (transport, descriptor);

#ifdef HAVE_COUNTERD
    char app_name[CNTRD_MAX_APP_NAME_LENGTH];

    /* initialise our counters */
    if (client != NULL)
    {
        snprintf (app_name, CNTRD_MAX_APP_NAME_LENGTH, "%s%s%s",
                  CMSG_COUNTER_APP_NAME_PREFIX, descriptor->name,
                  cmsg_transport_counter_app_tport_id (transport));

        if (cmsg_client_counter_create (client, app_name) != CMSG_RET_OK)
        {
            CMSG_LOG_GEN_ERROR ("[%s] Unable to create client counters.", app_name);
        }
    }
#endif

    return client;
}

void
cmsg_client_deinit (cmsg_client *client)
{
    /* Free counter session info but do not destroy counter data in the shared memory */
#ifdef HAVE_COUNTERD
    cntrd_app_unInit_app (&client->cntr_session, CNTRD_APP_PERSISTENT);
#endif
    client->cntr_session = NULL;

    cmsg_queue_filter_free (client->queue_filter_hash_table, client->descriptor);
    pthread_mutex_destroy (&client->queue_process_mutex);
    pthread_cond_destroy (&client->queue_process_cond);
    g_hash_table_destroy (client->queue_filter_hash_table);
    cmsg_send_queue_destroy (client->queue);
    pthread_mutex_destroy (&client->queue_mutex);

    // close the connection before destroying the client
    client->state = CMSG_CLIENT_STATE_CLOSED;
    if (client->_transport)
    {
        cmsg_client_close_wrapper (client);
    }

    if (client->loopback_server)
    {
        cmsg_server_destroy (client->loopback_server);
        client->loopback_server = NULL;
    }

    if (client->crypto_sa)
    {
        cmsg_crypto_sa_free (client->crypto_sa);
    }

    pthread_mutex_destroy (&client->invoke_mutex);
    pthread_mutex_destroy (&client->send_mutex);
}

static void
_cmsg_client_destroy (cmsg_client *client)
{
    CMSG_ASSERT_RETURN_VOID (client != NULL);

    cmsg_client_deinit (client);

    CMSG_FREE (client);
}

void
cmsg_client_destroy (cmsg_client *client)
{
    CMSG_ASSERT_RETURN_VOID (client != NULL);

    client->client_destroy (client);
}

// create counters
int32_t
cmsg_client_counter_create (cmsg_client *client, char *app_name)
{
    int32_t ret = CMSG_RET_ERR;

#ifdef HAVE_COUNTERD
    if (cntrd_app_init_app (app_name, CNTRD_APP_PERSISTENT,
                            (void **) &(client->cntr_session)) == CNTRD_APP_SUCCESS)
    {
        cntrd_app_register_ctr_in_group (client->cntr_session, "Client Unknown RPC",
                                         &(client->cntr_unknown_rpc));
        cntrd_app_register_ctr_in_group (client->cntr_session, "Client RPC Calls",
                                         &(client->cntr_rpc));
        cntrd_app_register_ctr_in_group (client->cntr_session, "Client Unknown Fields",
                                         &client->cntr_unknown_fields);
        cntrd_app_register_ctr_in_group (client->cntr_session, "Client Msgs Queued",
                                         &client->cntr_messages_queued);
        cntrd_app_register_ctr_in_group (client->cntr_session, "Client Msgs Dropped",
                                         &client->cntr_messages_dropped);
        cntrd_app_register_ctr_in_group (client->cntr_session, "Client Connect Attempts",
                                         &client->cntr_connect_attempts);
        cntrd_app_register_ctr_in_group (client->cntr_session, "Client Connect Failures",
                                         &client->cntr_connect_failures);
        cntrd_app_register_ctr_in_group (client->cntr_session, "Client Errors: General",
                                         &client->cntr_errors);
        cntrd_app_register_ctr_in_group (client->cntr_session, "Client Errors: Connection",
                                         &client->cntr_connection_errors);
        cntrd_app_register_ctr_in_group (client->cntr_session, "Client Errors: Recv",
                                         &client->cntr_recv_errors);
        cntrd_app_register_ctr_in_group (client->cntr_session, "Client Errors: Send",
                                         &client->cntr_send_errors);
        cntrd_app_register_ctr_in_group (client->cntr_session, "Client Errors: Pack",
                                         &client->cntr_pack_errors);
        cntrd_app_register_ctr_in_group (client->cntr_session, "Client Errors: Memory",
                                         &client->cntr_memory_errors);
        cntrd_app_register_ctr_in_group (client->cntr_session, "Client Errors: Protocol",
                                         &client->cntr_protocol_errors);
        cntrd_app_register_ctr_in_group (client->cntr_session, "Client Errors: Queue",
                                         &client->cntr_queue_errors);

        /* Tell cntrd not to destroy the counter data in the shared memory */
        cntrd_app_set_shutdown_instruction (app_name, CNTRD_SHUTDOWN_RESTART);
        ret = CMSG_RET_OK;
    }
#endif

    return ret;
}

/**
 * Helper function for 'cmsg_client_receive_encrypted'.
 * Decrypts the input encrypted data and unpacks the decrypted message.
 *
 * @param client - The client receiving data.
 * @param descriptor - The descriptor for the CMSG service.
 * @param msg_length - The length of the encrypted data passed in.
 * @param buffer - Buffer containing the encrypted data.
 * @param messagePtPt - Buffer to return the decrypted message in.
 *
 * @return CMSG_STATUS_CODE_SUCCESS on success, related error code on failure.
 */
static cmsg_status_code
_cmsg_client_receive_encrypted (cmsg_client *client,
                                const ProtobufCServiceDescriptor *descriptor,
                                int32_t msg_length, uint8_t *buffer,
                                ProtobufCMessage **messagePtPt)
{
    uint32_t dyn_len = 0;
    cmsg_header *header_received;
    cmsg_header header_converted;
    uint8_t *msg_data = NULL;
    uint8_t buf_static[512];
    uint8_t *decoded_data = buf_static;
    int decoded_bytes = 0;
    const ProtobufCMessageDescriptor *desc;
    uint32_t extra_header_size;
    cmsg_server_request server_request;
    *messagePtPt = NULL;
    cmsg_status_code code = CMSG_STATUS_CODE_SUCCESS;
    cmsg_transport *transport = client->_transport;
    int sock = transport->socket;

    if (msg_length > sizeof (buf_static))
    {
        decoded_data = (uint8_t *) CMSG_CALLOC (1, msg_length);
        if (decoded_data == NULL)
        {
            CMSG_LOG_TRANSPORT_ERROR (transport,
                                      "Client failed to allocate buffer on socket %d",
                                      sock);
            return CMSG_STATUS_CODE_SERVICE_FAILED;
        }
    }

    decoded_bytes =
        cmsg_crypto_decrypt (client->crypto_sa, buffer, msg_length, decoded_data,
                             client->crypto_sa_derive_func);
    if (decoded_bytes >= (int) sizeof (cmsg_header))
    {
        header_received = (cmsg_header *) decoded_data;
        if (cmsg_header_process (header_received, &header_converted) != CMSG_RET_OK)
        {
            /* Couldn't process the header for some reason */
            CMSG_LOG_TRANSPORT_ERROR (transport,
                                      "Unable to process message header for client receive. Bytes:%d",
                                      decoded_bytes);
            if (decoded_data != buf_static)
            {
                CMSG_FREE (decoded_data);
            }
            return CMSG_STATUS_CODE_SERVICE_FAILED;
        }

        CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] received response header\n");

        /* Take into account that someone may have changed the size of the header
         * and we don't know about it, make sure we receive all the information.
         * Any TLV is taken into account in the header length. */
        dyn_len = header_converted.message_length +
            header_converted.header_length - sizeof (cmsg_header);

        /* There is no more data to read so exit. */
        if (dyn_len == 0)
        {
            /* May have been queued, dropped or there was no message returned */
            CMSG_DEBUG (CMSG_INFO,
                        "[TRANSPORT] received response without data. server status %d\n",
                        header_converted.status_code);
            if (decoded_data != buf_static)
            {
                CMSG_FREE (decoded_data);
            }
            return header_converted.status_code;
        }

        if (dyn_len + sizeof (cmsg_header) > msg_length)
        {
            transport->tport_funcs.socket_close (transport);
            CMSG_LOG_TRANSPORT_ERROR (transport,
                                      "Received message is too large, closed the socket");
            if (decoded_data != buf_static)
            {
                CMSG_FREE (decoded_data);
            }
            return CMSG_STATUS_CODE_SERVICE_FAILED;
        }

        if (decoded_bytes - sizeof (cmsg_header) == (int) dyn_len)
        {
            extra_header_size = header_converted.header_length - sizeof (cmsg_header);

            /* Set msg_data to take into account a larger header than we expected */
            msg_data = decoded_data + sizeof (cmsg_header);

            cmsg_tlv_header_process (msg_data, &server_request, extra_header_size,
                                     descriptor);

            msg_data = msg_data + extra_header_size;
            CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] received response data\n");
            cmsg_buffer_print (msg_data, dyn_len);

            /* Message is only returned if the server returned Success. */
            if (header_converted.status_code == CMSG_STATUS_CODE_SUCCESS)
            {
                ProtobufCMessage *message = NULL;
                ProtobufCAllocator *allocator = &cmsg_memory_allocator;

                CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] unpacking response message\n");

                desc = descriptor->methods[server_request.method_index].output;
                message = protobuf_c_message_unpack (desc, allocator,
                                                     header_converted.message_length,
                                                     msg_data);

                if (message)
                {
                    *messagePtPt = message;
                }
                else
                {
                    CMSG_LOG_TRANSPORT_ERROR (transport,
                                              "Error unpacking response message. Msg length:%d",
                                              header_converted.message_length);
                    header_converted.status_code = CMSG_STATUS_CODE_SERVICE_FAILED;
                }
            }

            /* Free the allocated buffer */
            if (decoded_data != buf_static)
            {
                CMSG_FREE (decoded_data);
            }

            /* Make sure we return the status from the server */
            return header_converted.status_code;
        }
        else
        {
            CMSG_LOG_TRANSPORT_ERROR (transport,
                                      "No data for recv. socket:%d, dyn_len:%d, actual len:%d",
                                      sock, dyn_len, msg_length);

        }
    }

    if (decoded_data != buf_static)
    {
        CMSG_FREE (decoded_data);
    }

    return code;
}

/**
 * Receive the data and decrypt it.
 *
 * @param client - The server to receive on.
 * @param messagePtPt - Pointer to store the received message.
 *
 * @return CMSG_STATUS_CODE_SUCCESS on success, related status code on failure.
 */
static cmsg_status_code
cmsg_client_receive_encrypted (cmsg_client *client, ProtobufCMessage **messagePtPt)
{
    cmsg_transport *transport = client->_transport;
    uint8_t sec_header[8];
    uint32_t msg_length = 0;
    cmsg_peek_code peek_status;
    time_t receive_timeout = transport->receive_peek_timeout;
    cmsg_status_code code = CMSG_STATUS_CODE_SUCCESS;
    int nbytes = 0;
    uint8_t *buffer;
    uint8_t buf_static[512];
    int socket = transport->socket;
    const ProtobufCServiceDescriptor *descriptor = client->descriptor;

    *messagePtPt = NULL;

    peek_status = cmsg_transport_peek_for_header (transport->tport_funcs.recv_wrapper,
                                                  transport, socket, receive_timeout,
                                                  sec_header, sizeof (sec_header));
    if (peek_status != CMSG_PEEK_CODE_SUCCESS)
    {
        return cmsg_transport_peek_to_status_code (peek_status);
    }

    msg_length = cmsg_crypto_parse_header (sec_header);
    if (msg_length == -1)
    {
        CMSG_LOG_TRANSPORT_ERROR (transport, "Receive error. Invalid crypto header.");
        return CMSG_STATUS_CODE_SERVICE_FAILED;
    }

    if (msg_length < sizeof (buf_static))
    {
        buffer = buf_static;
    }
    else
    {
        buffer = (uint8_t *) CMSG_CALLOC (1, msg_length);
        if (buffer == NULL)
        {
            CMSG_LOG_TRANSPORT_ERROR (transport,
                                      "Client failed to allocate buffer on socket %d",
                                      transport->socket);
            return CMSG_STATUS_CODE_SERVICE_FAILED;
        }
    }

    nbytes =
        transport->tport_funcs.recv_wrapper (transport, socket, buffer, msg_length,
                                             MSG_WAITALL);

    if (nbytes == msg_length)
    {
        code = _cmsg_client_receive_encrypted (client, descriptor, msg_length,
                                               buffer, messagePtPt);
    }
    else if (nbytes > 0)
    {
        /* Didn't receive all of the CMSG header. */
        CMSG_LOG_TRANSPORT_ERROR (transport,
                                  "Bad header length for recv. Socket:%d nbytes:%d",
                                  transport->socket, nbytes);
        code = CMSG_STATUS_CODE_SERVICE_FAILED;
    }
    else if (nbytes == 0)
    {
        /* Normal socket shutdown case. Return other than TRANSPORT_OK to
         * have socket removed from select set. */
        code = CMSG_STATUS_CODE_CONNECTION_CLOSED;
    }
    else
    {
        if (errno == ECONNRESET)
        {
            CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] recv socket %d error: %s\n",
                        transport->socket, strerror (errno));
            code = CMSG_STATUS_CODE_SERVER_CONNRESET;
        }
        else
        {
            CMSG_LOG_TRANSPORT_ERROR (transport, "Recv error. Socket:%d Error:%s",
                                      transport->socket, strerror (errno));
            code = CMSG_STATUS_CODE_SERVICE_FAILED;
        }
    }

    if (buffer != buf_static)
    {
        CMSG_FREE (buffer);
    }

    return code;
}

cmsg_status_code
cmsg_client_response_receive (cmsg_client *client, ProtobufCMessage **message)
{
    cmsg_status_code ret;

    if (cmsg_client_crypto_enabled (client))
    {
        ret = cmsg_client_receive_encrypted (client, message);
    }
    else
    {
        ret = client->_transport->tport_funcs.client_recv (client->_transport,
                                                           client->descriptor, message);
    }

    return ret;
}


/**
 * Connect the transport, unless it is already connected.
 *
 * @param client - The client to connect.
 *
 * Returns 0 on success or a negative integer on failure.
 */
static int32_t
_cmsg_client_connect (cmsg_client *client)
{
    int32_t ret = 0;

    CMSG_ASSERT_RETURN_VAL (client != NULL, CMSG_RET_ERR);

    CMSG_DEBUG (CMSG_INFO, "[CLIENT] connecting\n");

    if (client->state == CMSG_CLIENT_STATE_CONNECTED)
    {
        CMSG_DEBUG (CMSG_INFO, "[CLIENT] already connected\n");
    }
    else
    {
        // count the connection attempt
        CMSG_COUNTER_INC (client, cntr_connect_attempts);

        ret = cmsg_transport_connect (client->_transport);
        if (ret < 0)
        {
            // count the connection failure
            CMSG_COUNTER_INC (client, cntr_connect_failures);
            client->state = CMSG_CLIENT_STATE_FAILED;
        }
        else
        {
            client->state = CMSG_CLIENT_STATE_CONNECTED;
        }
    }

    return ret;
}


/**
 * Connect the transport of the client, unless it's already connected.
 *
 * @param client - The client to connect.
 *
 * Returns 0 on success or a negative integer on failure.
 */
int32_t
cmsg_client_connect (cmsg_client *client)
{
    return _cmsg_client_connect (client);
}

/**
 * Configure send timeout for a cmsg client. This timeout will be applied immediately
 * to the client if it's already connected. Otherwise it will be applied when connected.
 * @param timeout   Timeout in seconds
 * @returns 0 on success or -1 on failure
 */
int
cmsg_client_set_send_timeout (cmsg_client *client, uint32_t timeout)
{
    CMSG_ASSERT_RETURN_VAL (client != NULL, CMSG_RET_ERR);

    return cmsg_transport_set_send_timeout (client->_transport, timeout);
}

/**
 * Configure the connect timeout for a cmsg client.
 *
 * @param timeout - The timeout value in seconds.
 *
 * @returns 0 on success or -1 on failure
 */
int
cmsg_client_set_connect_timeout (cmsg_client *client, uint32_t timeout)
{
    CMSG_ASSERT_RETURN_VAL (client != NULL, CMSG_RET_ERR);

    return cmsg_transport_set_connect_timeout (client->_transport, timeout);
}

/**
 * Configure receive timeout for a cmsg client. This timeout will be applied immediately
 * to the client if it's already connected. Otherwise it will be applied when connected.
 * @param timeout   Timeout in seconds
 * @returns 0 on success or -1 on failure
 */
int
cmsg_client_set_receive_timeout (cmsg_client *client, uint32_t timeout)
{
    CMSG_ASSERT_RETURN_VAL (client != NULL, CMSG_RET_ERR);

    return cmsg_transport_set_recv_peek_timeout (client->_transport, timeout);
}

int32_t
cmsg_client_invoke_recv (cmsg_client *client, uint32_t method_index,
                         ProtobufCClosure closure, cmsg_client_closure_data *closure_data)
{
    cmsg_status_code status_code;
    ProtobufCMessage *message_pt;
    ProtobufCService *service = (ProtobufCService *) client;
    const char *method_name = service->descriptor->methods[method_index].name;

    /* message_pt is filled in by the response receive.  It may be NULL or a valid pointer.
     * status_code will tell us whether it is a valid pointer.
     */
    status_code = cmsg_client_response_receive (client, &message_pt);

    if (status_code == CMSG_STATUS_CODE_SERVICE_FAILED ||
        status_code == CMSG_STATUS_CODE_CONNECTION_CLOSED ||
        status_code == CMSG_STATUS_CODE_SERVER_CONNRESET)
    {
        /* CMSG_STATUS_CODE_SERVER_CONNRESET happens when the socket is reset by peer,
         * which can happen if the connection to the peer is lost (e.g. stack node leave).
         * And reporting this event as an error is too annoying.
         * If required the calling application should take care of this error. */
        if (status_code == CMSG_STATUS_CODE_SERVER_CONNRESET)
        {
            CMSG_LOG_DEBUG ("[CLIENT] Connection reset by peer (method: %s)\n",
                            method_name);
        }
        else
        {
            CMSG_LOG_CLIENT_ERROR (client, "No response from server. (method: %s)",
                                   method_name);
        }

        // close the connection and return early
        client->state = CMSG_CLIENT_STATE_CLOSED;
        cmsg_client_close_wrapper (client);

        CMSG_COUNTER_INC (client, cntr_recv_errors);
        return CMSG_RET_CLOSED;
    }

    /* If the call was queued then no point in calling closure as there is no message.
     * Need to exit.
     */
    if (status_code == CMSG_STATUS_CODE_SERVICE_QUEUED)
    {
        CMSG_DEBUG (CMSG_INFO, "[CLIENT] info: response message QUEUED\n");
        return CMSG_RET_QUEUED;
    }
    else if (status_code == CMSG_STATUS_CODE_SERVICE_DROPPED)
    {
        CMSG_DEBUG (CMSG_INFO, "[CLIENT] info: response message DROPPED\n");
        return CMSG_RET_DROPPED;
    }
    else if (status_code == CMSG_STATUS_CODE_SERVER_METHOD_NOT_FOUND)
    {
        CMSG_DEBUG (CMSG_INFO, "[CLIENT] info: response message METHOD NOT FOUND\n");
        CMSG_COUNTER_INC (client, cntr_unknown_rpc);
        return CMSG_RET_METHOD_NOT_FOUND;
    }
    else if (message_pt == NULL)
    {
        /* There may be no message if the server has sent an empty message which is ok. */
        if (status_code == CMSG_STATUS_CODE_SUCCESS)
        {
            return CMSG_RET_OK;
        }
        else
        {
            CMSG_LOG_CLIENT_ERROR (client,
                                   "Response message not valid or empty. (method: %s)",
                                   method_name);
            CMSG_COUNTER_INC (client, cntr_protocol_errors);
            return CMSG_RET_ERR;
        }
    }

    // increment the counter if this message has unknown fields,
    if (message_pt->unknown_fields)
    {
        CMSG_COUNTER_INC (client, cntr_unknown_fields);
    }

    // free unknown fields from received message as the developer doesn't know about them
    protobuf_c_message_free_unknown_fields (message_pt, &cmsg_memory_allocator);

    closure_data->message = message_pt;
    closure_data->allocator = &cmsg_memory_allocator;

    return CMSG_RET_OK;
}

/**
 * To allow the client to be invoked safely from multiple threads
 * (i.e. from parallel CMSG API functions) we need to ensure that
 * the send/recv on the underlying socket is only executed in one
 * thread at a time. Note that the locking required to queue from
 * multiple threads (as part of the invoke call) is handled directly
 * by the queueing functionality.
 */
static void
cmsg_client_invoke (ProtobufCService *service, uint32_t method_index,
                    const ProtobufCMessage *input, ProtobufCClosure closure,
                    void *_closure_data)
{
    int32_t ret;
    cmsg_client *client = (cmsg_client *) service;
    bool did_queue = false;
    cmsg_client_closure_data *closure_data = (cmsg_client_closure_data *) _closure_data;

    if (client == NULL || input == NULL)
    {
        closure_data->retval = CMSG_RET_ERR;
        return;
    }

    ret = cmsg_client_queue_input (client, method_index, input, &did_queue);
    if (ret != CMSG_RET_OK)
    {
        closure_data->retval = ret;
        return;
    }

    if (!did_queue)
    {
        pthread_mutex_lock (&client->invoke_mutex);

        ret = client->invoke_send (client, method_index, input);
        if (ret == CMSG_RET_OK && client->invoke_recv)
        {
            ret = client->invoke_recv (client, method_index, closure, closure_data);
        }

        pthread_mutex_unlock (&client->invoke_mutex);
    }

    closure_data->retval = ret;
}

static int
_cmsg_client_should_queue (cmsg_client *client, const char *method_name, bool *do_queue)
{
    cmsg_queue_filter_type action = CMSG_QUEUE_FILTER_ERROR;

    /* First check queuing action with the filter function if configured.
     * Otherwise lookup the filter table */
    if (client->queue_filter_func == NULL ||
        client->queue_filter_func (client, method_name, &action) != CMSG_RET_OK)
    {
        action = cmsg_client_queue_filter_lookup (client, method_name);
    }

    if (action == CMSG_QUEUE_FILTER_ERROR)
    {
        CMSG_LOG_CLIENT_ERROR (client,
                               "Error occurred with queue_lookup_filter. (method: %s).",
                               method_name);
        CMSG_COUNTER_INC (client, cntr_queue_errors);
        return CMSG_RET_ERR;
    }
    else if (action == CMSG_QUEUE_FILTER_DROP)
    {
        CMSG_DEBUG (CMSG_INFO, "[CLIENT] dropping message: %s\n", method_name);
        CMSG_COUNTER_INC (client, cntr_messages_dropped);
        return CMSG_RET_DROPPED;
    }
    else if (action == CMSG_QUEUE_FILTER_QUEUE)
    {
        *do_queue = true;
        // count this as queued
        CMSG_COUNTER_INC (client, cntr_messages_queued);
    }
    else if (action == CMSG_QUEUE_FILTER_PROCESS)
    {
        *do_queue = false;
    }

    return CMSG_RET_OK;
}

static int
_cmsg_client_add_to_queue (cmsg_client *client, uint8_t *buffer,
                           uint32_t total_message_size, const char *method_name)
{
    pthread_mutex_lock (&client->queue_mutex);

    //todo: check return
    cmsg_send_queue_push (client->queue, buffer,
                          total_message_size,
                          client, client->_transport, (char *) method_name);

    pthread_mutex_unlock (&client->queue_mutex);

    //send signal to cmsg_client_queue_process_all
    pthread_mutex_lock (&client->queue_process_mutex);
    if (client->queue_process_count == 0)
    {
        pthread_cond_signal (&client->queue_process_cond);
    }
    client->queue_process_count = client->queue_process_count + 1;
    pthread_mutex_unlock (&client->queue_process_mutex);

    // Execute callback function if configured
    if (client->queue_callback_func != NULL)
    {
        client->queue_callback_func (client, method_name);
    }

    return CMSG_RET_QUEUED;
}

/**
 * Create the CMSG packet based on the input method name
 * and data.
 *
 * @param client - CMSG client the packet is to be sent/queued with
 * @param method_name - Method name that was invoked
 * @param input - The input data that was supplied to be invoked with
 * @param buffer_ptr - Pointer to store the created packet
 * @param total_message_size_ptr - Pointer to store the created packet size
 */
int32_t
cmsg_client_create_packet (cmsg_client *client, const char *method_name,
                           const ProtobufCMessage *input, uint8_t **buffer_ptr,
                           uint32_t *total_message_size_ptr)
{
    uint32_t ret = 0;
    cmsg_header header;
    int type = CMSG_TLV_METHOD_TYPE;
    uint32_t method_length = strlen (method_name) + 1;

    uint32_t packed_size = protobuf_c_message_get_packed_size (input);
    uint32_t extra_header_size = CMSG_TLV_SIZE (method_length);
    uint32_t total_header_size = sizeof (header) + extra_header_size;
    uint32_t total_message_size = total_header_size + packed_size;

    header = cmsg_header_create (CMSG_MSG_TYPE_METHOD_REQ, extra_header_size,
                                 packed_size, CMSG_STATUS_CODE_UNSET);

    uint8_t *buffer = (uint8_t *) CMSG_CALLOC (1, total_message_size);
    if (!buffer)
    {
        CMSG_LOG_CLIENT_ERROR (client,
                               "Unable to allocate memory for message. (method: %s).",
                               method_name);
        CMSG_COUNTER_INC (client, cntr_memory_errors);
        return CMSG_RET_ERR;
    }

    cmsg_tlv_method_header_create (buffer, header, type, method_length, method_name);

    uint8_t *buffer_data = buffer + total_header_size;

    CMSG_DEBUG (CMSG_INFO, "[CLIENT] header\n");
    cmsg_buffer_print (&header, sizeof (header));

    ret = protobuf_c_message_pack (input, buffer_data);
    if (ret < packed_size)
    {
        CMSG_LOG_CLIENT_ERROR (client,
                               "Underpacked message data. Packed %d of %d bytes. (method: %s)",
                               ret, packed_size, method_name);

        CMSG_FREE (buffer);
        CMSG_COUNTER_INC (client, cntr_pack_errors);
        return CMSG_RET_ERR;
    }
    else if (ret > packed_size)
    {
        CMSG_LOG_CLIENT_ERROR (client,
                               "Overpacked message data. Packed %d of %d bytes. (method: %s)",
                               ret, packed_size, method_name);

        CMSG_FREE (buffer);
        CMSG_COUNTER_INC (client, cntr_pack_errors);
        return CMSG_RET_ERR;
    }

    CMSG_DEBUG (CMSG_INFO, "[CLIENT] packet data\n");
    cmsg_buffer_print (buffer_data, packed_size);

    *buffer_ptr = buffer;
    *total_message_size_ptr = total_message_size;

    return CMSG_RET_OK;
}

/**
 * Checks whether the input message should be queued and then queues
 * the message on the client if required.
 *
 * @param client - CMSG client the packet is to be queued with
 * @param method_index - Method index that was invoked
 * @param input - The input data that was supplied to be invoked with
 * @param did_queue - Pointer to store whether the message was queued or not
 */
static int32_t
cmsg_client_queue_input (cmsg_client *client, uint32_t method_index,
                         const ProtobufCMessage *input, bool *did_queue)
{
    uint32_t ret = 0;
    uint8_t *buffer = NULL;
    uint32_t total_message_size = 0;
    ProtobufCService *service = (ProtobufCService *) client;
    const char *method_name = service->descriptor->methods[method_index].name;

    ret = _cmsg_client_should_queue (client, method_name, did_queue);
    if (ret != CMSG_RET_OK)
    {
        return ret;
    }

    if (*did_queue)
    {
        ret = cmsg_client_create_packet (client, method_name, input, &buffer,
                                         &total_message_size);
        if (ret == CMSG_RET_OK)
        {
            ret = _cmsg_client_add_to_queue (client, buffer, total_message_size,
                                             method_name);
            CMSG_FREE (buffer);
        }
    }

    return ret;
}

int32_t
cmsg_client_invoke_send (cmsg_client *client, uint32_t method_index,
                         const ProtobufCMessage *input)
{
    uint32_t ret = 0;
    ProtobufCService *service = (ProtobufCService *) client;
    const char *method_name = service->descriptor->methods[method_index].name;
    uint8_t *buffer = NULL;
    uint32_t total_message_size = 0;

    // count every rpc call
    CMSG_COUNTER_INC (client, cntr_rpc);

    CMSG_DEBUG (CMSG_INFO, "[CLIENT] method: %s\n", method_name);

    ret = cmsg_client_create_packet (client, method_name, input, &buffer,
                                     &total_message_size);
    if (ret != CMSG_RET_OK)
    {
        return ret;
    }

    ret = cmsg_client_buffer_send_retry_once (client, buffer, total_message_size,
                                              method_name);
    CMSG_FREE (buffer);

    return ret;
}


/**
 * Invoking like this will call the server invoke directly in the same
 * process/thread as the client. No queuing or filtering is performed.
 *
 * The reply from the server will be stored on the transport internally.
 */
int32_t
cmsg_client_invoke_send_direct (cmsg_client *client, uint32_t method_index,
                                const ProtobufCMessage *input)
{
    cmsg_server_invoke_direct (client->loopback_server, input, method_index);

    return CMSG_RET_OK;
}


int32_t
cmsg_client_get_socket (cmsg_client *client)
{
    int32_t sock = -1;

    CMSG_ASSERT_RETURN_VAL (client != NULL, -1);

    if (client->state == CMSG_CLIENT_STATE_CONNECTED)
    {
        sock = client->_transport->tport_funcs.get_socket (client->_transport);
    }
    else
    {
        CMSG_LOG_CLIENT_ERROR (client, "Failed to get socket. Client not connected.");
        CMSG_COUNTER_INC (client, cntr_connection_errors);
        return -1;
    }

    return sock;
}


/**
 * Sends an echo request to the server the client connects to.
 * The client should be one used specifically for this purpose.
 * The transport should be a RPC (twoway) connection so that a response can be received.
 *
 * The caller may not want to block however and so the function will return a
 * socket that can be listened on for the echo response.
 *
 * When the response is received the application should call cmsg_client_recv_echo_reply
 * to handle it's reception.
 */
int32_t
cmsg_client_send_echo_request (cmsg_client *client)
{
    int32_t ret = 0;
    // create header
    cmsg_header header = cmsg_header_create (CMSG_MSG_TYPE_ECHO_REQ,
                                             0, 0,
                                             CMSG_STATUS_CODE_UNSET);

    CMSG_ASSERT_RETURN_VAL (client != NULL, -1);

    CMSG_DEBUG (CMSG_INFO, "[CLIENT] header\n");
    cmsg_buffer_print (&header, sizeof (header));

    ret = cmsg_client_buffer_send_retry_once (client, (uint8_t *) &header,
                                              sizeof (header), "echo request");

    if (ret != CMSG_RET_OK)
    {
        return -1;
    }

    // return socket to listen on
    return client->_transport->tport_funcs.get_socket (client->_transport);
}


/**
 * Waits and receives the echo reply on the socket passed in.
 * @returns whether the status_code returned by the server.
 */
cmsg_status_code
cmsg_client_recv_echo_reply (cmsg_client *client)
{
    cmsg_status_code status_code;
    ProtobufCMessage *message_pt = NULL;

    CMSG_ASSERT_RETURN_VAL (client != NULL, CMSG_STATUS_CODE_UNSET);

    /* message_pt is filled in by the response receive.  It may be NULL or a valid pointer.
     * status_code will tell us whether it is a valid pointer.
     */
    status_code = cmsg_client_response_receive (client, &message_pt);

    if (message_pt != NULL)
    {
        // We don't expect a message to have been sent back so free it and
        // move on.  Not treating it as an error as this behaviour might
        // change in the future and it doesn't really matter.
        protobuf_c_message_free_unpacked (message_pt, &cmsg_memory_allocator);
    }

    return status_code;
}

void
cmsg_client_queue_enable (cmsg_client *client)
{
    cmsg_client_queue_filter_set_all (client, CMSG_QUEUE_FILTER_QUEUE);
}

int32_t
cmsg_client_queue_disable (cmsg_client *client)
{
    cmsg_client_queue_filter_set_all (client, CMSG_QUEUE_FILTER_PROCESS);

    return cmsg_client_queue_process_all (client);
}

uint32_t
cmsg_client_queue_get_length (cmsg_client *client)
{
    pthread_mutex_lock (&client->queue_mutex);
    uint32_t queue_length = g_queue_get_length (client->queue);
    pthread_mutex_unlock (&client->queue_mutex);

    return queue_length;
}


int32_t
cmsg_client_queue_process_all (cmsg_client *client)
{
    struct timespec time_to_wait;
    uint32_t processed = 0;

    CMSG_ASSERT_RETURN_VAL (client != NULL, CMSG_RET_ERR);

    clock_gettime (CLOCK_REALTIME, &time_to_wait);

    //if the we run do api calls and processing in different threads wait
    //for a signal from the api thread to start processing
    if (client->self_thread_id != pthread_self ())
    {
        pthread_mutex_lock (&client->queue_process_mutex);
        while (client->queue_process_count == 0)
        {
            time_to_wait.tv_sec++;
            pthread_cond_timedwait (&client->queue_process_cond,
                                    &client->queue_process_mutex, &time_to_wait);
        }
        pthread_mutex_unlock (&client->queue_process_mutex);

        processed = _cmsg_client_queue_process_all_direct (client);

        pthread_mutex_lock (&client->queue_process_mutex);
        client->queue_process_count = client->queue_process_count - 1;
        pthread_mutex_unlock (&client->queue_process_mutex);

    }
    else
    {
        processed = _cmsg_client_queue_process_all_direct (client);
    }

    return processed;
}

static int32_t
_cmsg_client_queue_process_all_internal (cmsg_client *client)
{
    cmsg_send_queue_entry *queue_entry = NULL;
    GQueue *queue = client->queue;
    pthread_mutex_t *queue_mutex = &client->queue_mutex;
    cmsg_client *send_client = 0;

    pthread_mutex_lock (queue_mutex);
    if (g_queue_get_length (queue))
    {
        queue_entry = (cmsg_send_queue_entry *) g_queue_pop_tail (queue);
    }
    pthread_mutex_unlock (queue_mutex);

    while (queue_entry)
    {
        send_client = queue_entry->client;

        int ret = cmsg_client_buffer_send_retry (send_client,
                                                 queue_entry->queue_buffer,
                                                 queue_entry->queue_buffer_size,
                                                 CMSG_TRANSPORT_CLIENT_SEND_TRIES);

        if (ret == CMSG_RET_ERR)
        {
            CMSG_LOG_CLIENT_ERROR (client,
                                   "Server not reachable after %d tries. (method: %s).",
                                   CMSG_TRANSPORT_CLIENT_SEND_TRIES,
                                   queue_entry->method_name);
        }

        CMSG_FREE (queue_entry->queue_buffer);
        CMSG_FREE (queue_entry);
        queue_entry = NULL;

        if (ret == CMSG_RET_ERR)
        {
            return CMSG_RET_ERR;
        }

        //get the next entry
        pthread_mutex_lock (queue_mutex);
        queue_entry = (cmsg_send_queue_entry *) g_queue_pop_tail (queue);
        pthread_mutex_unlock (queue_mutex);
    }

    return CMSG_RET_OK;
}

static int32_t
_cmsg_client_queue_process_all_direct (cmsg_client *client)
{
    int ret = CMSG_RET_OK;
    GQueue *queue = client->queue;
    pthread_mutex_t *queue_mutex = &client->queue_mutex;

    if (!queue)
    {
        CMSG_COUNTER_INC (client, cntr_errors);
        return CMSG_RET_ERR;
    }

    ret = _cmsg_client_queue_process_all_internal (client);

    if (ret == CMSG_RET_ERR)
    {
        //delete all messages for this client from queue
        pthread_mutex_lock (queue_mutex);
        cmsg_send_queue_free_all_by_transport (queue, client->_transport);
        pthread_mutex_unlock (queue_mutex);

        CMSG_LOG_CLIENT_ERROR (client, "Server not reachable after %d tries.",
                               CMSG_TRANSPORT_CLIENT_SEND_TRIES);
    }

    return ret;
}

int32_t
cmsg_client_buffer_send_retry_once (cmsg_client *client, uint8_t *queue_buffer,
                                    uint32_t queue_buffer_size, const char *method_name)
{
    int ret = 0;

    CMSG_ASSERT_RETURN_VAL (client != NULL, CMSG_RET_ERR);

    pthread_mutex_lock (&client->send_mutex);

    ret = _cmsg_client_buffer_send_retry_once (client, queue_buffer,
                                               queue_buffer_size, method_name);

    pthread_mutex_unlock (&client->send_mutex);

    return ret;
}

/**
 * Wrap the sending of a buffer so that the input buffer can be encrypted if required
 *
 * @param client - The client sending data.
 * @param queue_buffer - The data to send.
 * @param queue_buffer_size - The length of the data to send.
 *
 * @returns The number of bytes sent if successful, -1 on failure.
 */
static int32_t
cmsg_client_transport_send (cmsg_client *client, uint8_t *queue_buffer,
                            uint32_t queue_buffer_size)
{
    cmsg_transport *transport = client->_transport;
    int send_ret = 0;
    uint8_t *nonce = NULL;
    uint32_t nonce_length;
    uint8_t *encrypt_buffer;
    int encrypt_length;

    if (cmsg_client_crypto_enabled (client))
    {
        if (!client->crypto_sa->ctx_in_init)
        {
            nonce = cmsg_crypto_create_nonce (client->crypto_sa,
                                              client->crypto_sa_derive_func, &nonce_length);
            if (nonce == NULL)
            {
                return CMSG_RET_ERR;
            }
            send_ret = transport->tport_funcs.client_send (transport, nonce,
                                                           nonce_length, 0);
            CMSG_FREE (nonce);
            if (send_ret < 0)
            {
                CMSG_LOG_CLIENT_ERROR (client, "Failed to send nonce for SA %u",
                                       client->crypto_sa->id);
                return CMSG_RET_ERR;
            }
        }

        encrypt_buffer = (uint8_t *) CMSG_CALLOC (1, queue_buffer_size + ENCRYPT_EXTRA);
        if (encrypt_buffer == NULL)
        {
            CMSG_LOG_CLIENT_ERROR (client, "Client failed to allocate buffer on socket %d",
                                   transport->socket);
            return CMSG_RET_ERR;
        }

        encrypt_length = cmsg_crypto_encrypt (client->crypto_sa, queue_buffer,
                                              queue_buffer_size, encrypt_buffer,
                                              queue_buffer_size + ENCRYPT_EXTRA);
        if (encrypt_length < 0)
        {
            CMSG_LOG_CLIENT_ERROR (client, "Client encrypt on socket %d failed - %s",
                                   transport->socket, strerror (errno));
            CMSG_FREE (encrypt_buffer);
            return CMSG_RET_ERR;
        }

        send_ret = transport->tport_funcs.client_send (transport, encrypt_buffer,
                                                       encrypt_length, 0);

        /* If the send was successful, fixup the return length to match the original
         * plaintext length so callers are unaware of the encryption */
        if (encrypt_length == send_ret)
        {
            send_ret = queue_buffer_size;
        }

        CMSG_FREE (encrypt_buffer);
    }
    else
    {
        send_ret = transport->tport_funcs.client_send (transport, queue_buffer,
                                                       queue_buffer_size, 0);
    }

    return send_ret;
}

static int32_t
_cmsg_client_buffer_send_retry_once (cmsg_client *client, uint8_t *queue_buffer,
                                     uint32_t queue_buffer_size, const char *method_name)
{
    int send_ret = 0;
    int connect_error = 0;

    connect_error = cmsg_client_connect (client);

    if (client->state != CMSG_CLIENT_STATE_CONNECTED)
    {
        CMSG_LOG_DEBUG ("[CLIENT] client is not connected (method: %s, error: %d)",
                        method_name, connect_error);
        return CMSG_RET_CLOSED;
    }

    send_ret = cmsg_client_transport_send (client, queue_buffer, queue_buffer_size);

    if (send_ret < (int) (queue_buffer_size))
    {
        // close the connection as something must be wrong
        client->state = CMSG_CLIENT_STATE_CLOSED;
        cmsg_client_close_wrapper (client);
        // the connection may be down due to a problem since the last send
        // attempt once to reconnect and send
        connect_error = cmsg_client_connect (client);

        if (client->state == CMSG_CLIENT_STATE_CONNECTED)
        {
            send_ret = cmsg_client_transport_send (client, queue_buffer, queue_buffer_size);

            if (send_ret < (int) (queue_buffer_size))
            {
                // Having retried connecting and now failed again this is
                // an actual problem.
                if (send_ret == -1)
                {
                    if (errno == EAGAIN)
                    {
                        CMSG_LOG_DEBUG ("[CLIENT] client_send failed (method: %s), %s",
                                        method_name, strerror (errno));
                    }
                    else
                    {
                        CMSG_LOG_CLIENT_ERROR (client,
                                               "Client send failed (method: %s), %s",
                                               method_name, strerror (errno));
                    }
                }
                else
                {
                    CMSG_LOG_CLIENT_ERROR (client,
                                           "Client send failed. Sent %d of %u bytes. (method: %s)",
                                           send_ret, (uint32_t) (queue_buffer_size),
                                           method_name);
                }
                client->state = CMSG_CLIENT_STATE_FAILED;
                cmsg_client_close_wrapper (client);
                CMSG_COUNTER_INC (client, cntr_send_errors);
                return CMSG_RET_ERR;
            }
        }
        else
        {
            CMSG_LOG_DEBUG ("[CLIENT] client is not connected (method: %s, error: %d)",
                            method_name, connect_error);
            return CMSG_RET_CLOSED;
        }
    }

    return CMSG_RET_OK;
}

/**
 * Send a buffer of bytes on the client. Note that sending anything other than
 * a well formed cmsg packet will be dropped by the server being sent to.
 *
 * @param client - The client to send on.
 * @param buffer - The buffer of bytes to send.
 * @param buffer_len - The length of the buffer being sent.
 * @param method_name - The name of the method being invoked.
 *
 * @returns CMSG_RET_OK on success, related error code on failure.
 */
static int32_t
_cmsg_client_send_bytes (cmsg_client *client, uint8_t *buffer, uint32_t buffer_len,
                         const char *method_name)
{
    int ret = CMSG_RET_ERR;

    CMSG_ASSERT_RETURN_VAL (client != NULL, CMSG_RET_ERR);

    pthread_mutex_lock (&client->send_mutex);
    ret = _cmsg_client_buffer_send_retry_once (client, buffer, buffer_len, method_name);
    pthread_mutex_unlock (&client->send_mutex);

    return ret;
}

/**
 * Send a buffer of bytes on a client. Note that sending anything other than
 * a well formed cmsg packet will be dropped by the server being sent to.
 *
 * @param client - The client to send on.
 * @param buffer - The buffer of bytes to send.
 * @param buffer_len - The length of the buffer being sent.
 * @param method_name - The name of the method being invoked.
 *
 * @returns CMSG_RET_OK on success, related error code on failure.
 */
int32_t
cmsg_client_send_bytes (cmsg_client *client, uint8_t *buffer, uint32_t buffer_len,
                        const char *method_name)
{
    return client->send_bytes (client, buffer, buffer_len, method_name);
}

int32_t
cmsg_client_buffer_send_retry (cmsg_client *client, uint8_t *queue_buffer,
                               uint32_t queue_buffer_size, int max_tries)
{
    int c = 0;

    CMSG_ASSERT_RETURN_VAL (client != NULL, CMSG_RET_ERR);

    for (c = 0; c <= max_tries; c++)
    {
        pthread_mutex_lock (&client->send_mutex);
        int ret = _cmsg_client_buffer_send (client, queue_buffer, queue_buffer_size);
        pthread_mutex_unlock (&client->send_mutex);

        if (ret == CMSG_RET_OK)
        {
            return CMSG_RET_OK;
        }
        else
        {
            usleep (200000);
        }
    }
    CMSG_DEBUG (CMSG_WARN, "[CLIENT] send tries %d\n", max_tries);

    return CMSG_RET_ERR;
}

static int32_t
_cmsg_client_buffer_send (cmsg_client *client, uint8_t *buffer, uint32_t buffer_size)
{
    int send_ret = 0;
    int connect_ret = 0;

    connect_ret = cmsg_client_connect (client);
    if (client->state != CMSG_CLIENT_STATE_CONNECTED)
    {
        CMSG_LOG_DEBUG ("[CLIENT] client is not connected, error: %d)", connect_ret);
        return CMSG_RET_CLOSED;
    }

    send_ret = cmsg_client_transport_send (client, buffer, buffer_size);
    if (send_ret < (int) buffer_size)
    {
        CMSG_DEBUG (CMSG_ERROR, "[CLIENT] sending buffer failed, send: %d of %d\n",
                    send_ret, buffer_size);
        client->state = CMSG_CLIENT_STATE_FAILED;
        cmsg_client_close_wrapper (client);
        CMSG_COUNTER_INC (client, cntr_send_errors);
        return CMSG_RET_ERR;
    }

    return CMSG_RET_OK;
}

void
cmsg_client_queue_filter_set_all (cmsg_client *client, cmsg_queue_filter_type filter_type)
{
    cmsg_queue_filter_set_all (client->queue_filter_hash_table, client->descriptor,
                               filter_type);
}

void
cmsg_client_queue_filter_clear_all (cmsg_client *client)
{
    cmsg_queue_filter_clear_all (client->queue_filter_hash_table, client->descriptor);
}

int32_t
cmsg_client_queue_filter_set (cmsg_client *client, const char *method,
                              cmsg_queue_filter_type filter_type)
{
    return cmsg_queue_filter_set (client->queue_filter_hash_table, method, filter_type);
}

int32_t
cmsg_client_queue_filter_clear (cmsg_client *client, const char *method)
{
    return cmsg_queue_filter_clear (client->queue_filter_hash_table, method);
}

void
cmsg_client_msg_queue_filter_func_set (cmsg_client *client, cmsg_queue_filter_func_t func)
{
    if (client)
    {
        client->queue_filter_func = func;
    }
}

void
cmsg_client_msg_queue_callback_func_set (cmsg_client *client,
                                         cmsg_queue_callback_func_t func)
{
    if (client)
    {
        client->queue_callback_func = func;
    }
}

void
cmsg_client_queue_filter_init (cmsg_client *client)
{
    cmsg_queue_filter_init (client->queue_filter_hash_table, client->descriptor);
}

cmsg_queue_filter_type
cmsg_client_queue_filter_lookup (cmsg_client *client, const char *method)
{
    return cmsg_queue_filter_lookup (client->queue_filter_hash_table, method);
}

void
cmsg_client_suppress_error (cmsg_client *client, cmsg_bool_t enable)
{
    client->suppress_errors = enable;

    /* Apply to transport as well */
    if (client->_transport)
    {
        client->_transport->suppress_errors = enable;
    }
}

/* Create a cmsg client and its transport over a UNIX socket */
static cmsg_client *
_cmsg_create_client_unix (const ProtobufCServiceDescriptor *descriptor,
                          cmsg_transport_type transport_type)
{
    cmsg_transport *transport;
    cmsg_client *client;

    transport = cmsg_create_transport_unix (descriptor, transport_type);
    if (!transport)
    {
        CMSG_LOG_GEN_ERROR ("Failed to create UNIX CMSG client for service: %s",
                            descriptor->name);
        return NULL;
    }

    client = cmsg_client_new (transport, descriptor);
    if (!client)
    {
        cmsg_transport_destroy (transport);
        CMSG_LOG_GEN_ERROR ("Failed to create UNIX CMSG client for service: %s",
                            descriptor->name);
        return NULL;
    }
    return client;
}

cmsg_client *
cmsg_create_client_unix (const ProtobufCServiceDescriptor *descriptor)
{
    CMSG_ASSERT_RETURN_VAL (descriptor != NULL, NULL);

    return _cmsg_create_client_unix (descriptor, CMSG_TRANSPORT_RPC_UNIX);
}

cmsg_client *
cmsg_create_client_unix_oneway (const ProtobufCServiceDescriptor *descriptor)
{
    CMSG_ASSERT_RETURN_VAL (descriptor != NULL, NULL);

    return _cmsg_create_client_unix (descriptor, CMSG_TRANSPORT_ONEWAY_UNIX);
}

int32_t
cmsg_client_unix_server_ready (const ProtobufCServiceDescriptor *descriptor)
{
    CMSG_ASSERT_RETURN_VAL (descriptor != NULL, CMSG_RET_ERR);

    int ret;
    char *sun_path = cmsg_transport_unix_sun_path (descriptor);

    ret = access (sun_path, F_OK);
    free (sun_path);

    return ret;
}

/**
 * Create a TIPC broadcast client.
 *
 * @param descriptor - The descriptor for the client.
 * @param service_name - The service name in the /etc/services file to get
 *                       the port number.
 * @param lower_addr - The lower TIPC node id to broadcast to.
 * @param upper_addr - The upper TIPC node id to broadcast to.
 *
 * @returns Pointer to the client on success, NULL on failure.
 */
cmsg_client *
cmsg_create_client_tipc_broadcast (const ProtobufCServiceDescriptor *descriptor,
                                   const char *service_name, int lower_addr, int upper_addr)
{
    cmsg_transport *transport;
    cmsg_client *client;
    uint16_t port;

    CMSG_ASSERT_RETURN_VAL (descriptor != NULL, NULL);

    port = cmsg_service_port_get (service_name, "tipc");
    if (port == 0)
    {
        CMSG_LOG_GEN_ERROR ("Unknown TIPC broadcast service: %s", service_name);
        return NULL;
    }

    transport = cmsg_transport_new (CMSG_TRANSPORT_BROADCAST);
    if (transport == NULL)
    {
        return NULL;
    }

    transport->config.socket.sockaddr.tipc.addrtype = TIPC_ADDR_MCAST;
    transport->config.socket.sockaddr.tipc.addr.nameseq.type = port;
    transport->config.socket.sockaddr.tipc.addr.nameseq.lower = lower_addr;
    transport->config.socket.sockaddr.tipc.addr.nameseq.upper = upper_addr;

    client = cmsg_client_new (transport, descriptor);
    if (!client)
    {
        cmsg_transport_destroy (transport);
        CMSG_LOG_GEN_ERROR ("[%s] Failed to create TIPC broadcast client.",
                            descriptor->name);
        return NULL;
    }

    return client;
}

/**
 * Change the broadcast address for a TIPC broadcast client.
 *
 * @param client - The TIPC broadcast client to modify.
 * @param lower_addr - The lower TIPC node id to broadcast to.
 * @param upper_addr - The upper TIPC node id to broadcast to.
 */
void
cmsg_client_tipc_broadcast_set_destination (cmsg_client *client, int lower_addr,
                                            int upper_addr)
{
    client->_transport->config.socket.sockaddr.tipc.addr.nameseq.lower = lower_addr;
    client->_transport->config.socket.sockaddr.tipc.addr.nameseq.upper = upper_addr;
}

/**
 * Creates a client of type Loopback and sets all the correct fields.
 *
 * Returns NULL if failed to create anything - malloc problems.
 */
cmsg_client *
cmsg_create_client_loopback (ProtobufCService *service)
{
    cmsg_transport *transport = NULL;
    cmsg_server *server = NULL;
    cmsg_client *client = NULL;

    transport = cmsg_transport_new (CMSG_TRANSPORT_LOOPBACK);
    if (transport == NULL)
    {
        CMSG_LOG_GEN_ERROR ("Could not create transport for loopback client\n");
        return NULL;
    }

    /* The point of the loopback is to process the CMSG within the same process-
     * space, without using RPC. So the client actually does the server-side
     * processing as well */
    server = cmsg_server_new (transport, service);
    if (server == NULL)
    {
        CMSG_LOG_GEN_ERROR ("Could not create server for loopback transport\n");
        cmsg_transport_destroy (transport);
        return NULL;
    }

    /* When using a loopback client/server the server_invoke gets given the
     * memory that the client declared the message in. We don't want the server
     * trying to free this memory (often it is on the stack) so let it know that
     * it does not own the memory for the messages. */
    cmsg_server_app_owns_all_msgs_set (server, true);

    client = cmsg_client_new (transport, service->descriptor);

    if (client == NULL)
    {
        syslog (LOG_ERR, "Could not create loopback client");
        cmsg_destroy_server_and_transport (server);
        return NULL;
    }

    /* the client stores a pointer to the server so we can access it later to
     * invoke the implementation function directly. */
    client->loopback_server = server;

    return client;
}

/**
 * Creates a forwarding client.
 *
 * @param descriptor - The descriptor for the client.
 * @param user_data - The user data to provide to the forwarding function.
 * @param send_func - The function that will be called to forward the message.
 *
 * @returns A pointer to the client on success, NULL otherwise.
 */
cmsg_client *
cmsg_create_client_forwarding (const ProtobufCServiceDescriptor *descriptor,
                               void *user_data, cmsg_forwarding_transport_send_f send_func)
{
    cmsg_transport *transport = NULL;
    cmsg_client *client = NULL;

    transport = cmsg_transport_new (CMSG_TRANSPORT_FORWARDING);
    if (transport == NULL)
    {
        CMSG_LOG_GEN_ERROR ("Could not create transport for forwarding client\n");
        return NULL;
    }

    cmsg_transport_forwarding_func_set (transport, send_func);
    cmsg_transport_forwarding_user_data_set (transport, user_data);

    client = cmsg_client_new (transport, descriptor);
    if (client == NULL)
    {
        cmsg_transport_destroy (transport);
        syslog (LOG_ERR, "Could not create forwarding client");
        return NULL;
    }

    return client;
}

/**
 * Set the user data for the forwarding client.
 *
 * @param client - The forwarding client to set the data for.
 * @param user_data - The data to set.
 */
void
cmsg_client_forwarding_data_set (cmsg_client *client, void *user_data)
{
    if (!client || client->_transport->type != CMSG_TRANSPORT_FORWARDING)
    {
        return;
    }

    cmsg_transport_forwarding_user_data_set (client->_transport, user_data);
}

/**
 * Close the client transport layer.
 *
 * NOTE user applications should not call this routine directly
 *
 */
static void
cmsg_client_close_wrapper (cmsg_client *client)
{
    if (client->_transport->tport_funcs.socket_close)
    {
        client->_transport->tport_funcs.socket_close (client->_transport);
    }

    if (cmsg_client_crypto_enabled (client))
    {
        client->crypto_sa->ctx_in_init = false;
    }
}

/**
 * Destroy a cmsg client and its transport
 *
 * @param client - the cmsg client to destroy
 */
void
cmsg_destroy_client_and_transport (cmsg_client *client)
{
    cmsg_transport *transport;

    if (client)
    {
        transport = client->_transport;
        cmsg_client_destroy (client);

        cmsg_transport_destroy (transport);
    }
}

/**
 * Helper function for creating a CMSG client using TCP over IPv4.
 *
 * @param service_name - The service name in the /etc/services file to get
 *                       the port number.
 * @param addr - The IPv4 address to connect to (in network byte order).
 * @param vrf_bind_dev - For VRF support, the device to bind to the socket (NULL if not relevant)
 * @param descriptor - The CMSG service descriptor for the service.
 * @param oneway - Whether to make a one-way client, or a two-way (RPC) client.
 */
static cmsg_client *
_cmsg_create_client_tcp_ipv4 (const char *service_name, struct in_addr *addr,
                              const char *vrf_bind_dev,
                              const ProtobufCServiceDescriptor *descriptor, bool oneway)
{
    cmsg_transport *transport;
    cmsg_client *client;

    transport = cmsg_create_transport_tcp_ipv4 (service_name, addr, vrf_bind_dev, oneway);
    if (!transport)
    {
        return NULL;
    }

    client = cmsg_client_new (transport, descriptor);
    if (!client)
    {
        cmsg_transport_destroy (transport);
        CMSG_LOG_GEN_ERROR ("No TCP IPC client on %s", descriptor->name);
        return NULL;
    }

    return client;
}

/**
 * Create a RPC (two-way) CMSG client using TCP over IPv4.
 *
 * @param service_name - The service name in the /etc/services file to get
 *                       the port number.
 * @param addr - The IPv4 address to connect to (in network byte order).
 * @param vrf_bind_dev - For VRF support, the device to bind to the socket (NULL if not relevant)
 * @param descriptor - The CMSG service descriptor for the service.
 */
cmsg_client *
cmsg_create_client_tcp_ipv4_rpc (const char *service_name, struct in_addr *addr,
                                 const char *vrf_bind_dev,
                                 const ProtobufCServiceDescriptor *descriptor)
{
    CMSG_ASSERT_RETURN_VAL (service_name != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (addr != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (descriptor != NULL, NULL);

    return _cmsg_create_client_tcp_ipv4 (service_name, addr, vrf_bind_dev, descriptor,
                                         false);
}

/**
 * Create a oneway CMSG client using TCP over IPv4.
 *
 * @param service_name - The service name in the /etc/services file to get
 *                       the port number.
 * @param addr - The IPv4 address to connect to (in network byte order).
 * @param vrf_bind_dev - For VRF support, the device to bind to the socket (NULL if not relevant)
 * @param descriptor - The CMSG service descriptor for the service.
 */
cmsg_client *
cmsg_create_client_tcp_ipv4_oneway (const char *service_name, struct in_addr *addr,
                                    const char *vrf_bind_dev,
                                    const ProtobufCServiceDescriptor *descriptor)
{
    CMSG_ASSERT_RETURN_VAL (service_name != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (addr != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (descriptor != NULL, NULL);

    return _cmsg_create_client_tcp_ipv4 (service_name, addr, vrf_bind_dev, descriptor,
                                         true);
}

/**
 * Helper function for creating a CMSG client using TCP over IPv6.
 *
 * @param service_name - The service name in the /etc/services file to get
 *                       the port number.
 * @param addr - The IPv6 address to connect to (in network byte order).
 * @param scope_id - The scope id if a link local address is used, zero otherwise
 * @param vrf_bind_dev - For VRF support, the device to bind to the socket (NULL if not relevant)
 * @param descriptor - The CMSG service descriptor for the service.
 * @param oneway - Whether to make a one-way client, or a two-way (RPC) client.
 */
static cmsg_client *
_cmsg_create_client_tcp_ipv6 (const char *service_name, struct in6_addr *addr,
                              uint32_t scope_id, const char *vrf_bind_dev,
                              const ProtobufCServiceDescriptor *descriptor, bool oneway)
{
    cmsg_transport *transport;
    cmsg_client *client;

    transport = cmsg_create_transport_tcp_ipv6 (service_name, addr, scope_id, vrf_bind_dev,
                                                oneway);
    if (!transport)
    {
        return NULL;
    }

    client = cmsg_client_new (transport, descriptor);
    if (!client)
    {
        cmsg_transport_destroy (transport);
        return NULL;
    }

    return client;
}

/**
 * Create a RPC (two-way) CMSG client using TCP over IPv6.
 *
 * @param service_name - The service name in the /etc/services file to get
 *                       the port number.
 * @param addr - The IPv4 address to connect to (in network byte order).
 * @param scope_id - The scope id if a link local address is used, zero otherwise
 * @param vrf_bind_dev - For VRF support, the device to bind to the socket (NULL if not relevant)
 * @param descriptor - The CMSG service descriptor for the service.
 */
cmsg_client *
cmsg_create_client_tcp_ipv6_rpc (const char *service_name, struct in6_addr *addr,
                                 uint32_t scope_id, const char *vrf_bind_dev,
                                 const ProtobufCServiceDescriptor *descriptor)
{
    CMSG_ASSERT_RETURN_VAL (service_name != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (addr != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (descriptor != NULL, NULL);

    return _cmsg_create_client_tcp_ipv6 (service_name, addr, scope_id, vrf_bind_dev,
                                         descriptor, false);
}

/**
 * Create a oneway CMSG client using TCP over IPv6.
 *
 * @param service_name - The service name in the /etc/services file to get
 *                       the port number.
 * @param addr - The IPv6 address to connect to (in network byte order).
 * @param scope_id - The scope is if a link local address is used, zero otherwise
 * @param bindvrf_bind_dev_dev - For VRF support, the device to bind to the socket (NULL if not relevant)
 * @param descriptor - The CMSG service descriptor for the service.
 */
cmsg_client *
cmsg_create_client_tcp_ipv6_oneway (const char *service_name, struct in6_addr *addr,
                                    uint32_t scope_id, const char *vrf_bind_dev,
                                    const ProtobufCServiceDescriptor *descriptor)
{
    CMSG_ASSERT_RETURN_VAL (service_name != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (addr != NULL, NULL);
    CMSG_ASSERT_RETURN_VAL (descriptor != NULL, NULL);

    return _cmsg_create_client_tcp_ipv6 (service_name, addr, scope_id, vrf_bind_dev,
                                         descriptor, true);
}

/**
 * Check the passed in recv_msg pointer to see if it is NULL. If it is not NULL, set it to
 * NULL and log a client debug message.
 * @param client cmsg client that called the API
 * @param recv_msg pointer to set to NULL
 * @param ref API reference to put in the error message
 */
static void
cmsg_api_recv_ptr_null_check (cmsg_client *client, ProtobufCMessage **recv_msg,
                              const char *ref)
{
    if (!recv_msg)
    {
        return;
    }

    /* test that the pointer to the recv msg is NULL. If it isn't, set it to
     * NULL but complain loudly that the api is not being used correctly  */
    if (*(recv_msg) != NULL)
    {
        *(recv_msg) = NULL;
        CMSG_LOG_CLIENT_DEBUG (client,
                               "WARNING: %s API called with Non-NULL recv_msg! Setting to NULL! (This may be a leak!)",
                               ref);
    }
}

/**
 * Helper function to set or free received response data for an API
 * @param closure_data received response
 * @param recv_msg Message to update with responses (if a response is expected)
 * @returns CMSG API return code.
 */
static int
cmsg_api_process_closure_data (cmsg_client_closure_data *closure_data,
                               ProtobufCMessage **recv_msg)
{
    int i = 0;

    /* sanity check our returned message pointer */
    while (closure_data[i].message != NULL)
    {
        ProtobufCMessage *msg = (ProtobufCMessage *) closure_data[i].message;
        if (msg->descriptor->n_fields > 0)
        {
            /* Update developer output msg to point to received message from invoke */
            recv_msg[i] = msg;
        }
        else
        {
            /* Free the received message since the caller does not expect to receive it */
            CMSG_FREE_RECV_MSG (msg);
        }
        i++;
    }

    return closure_data[0].retval;
}


/**
 * Helper for cmsg_api_invoke that returns a response from a file on the client side.
 * @param filename file that holds the response data
 * @param output_desc Output message descriptor
 * @param recv_msg array pointer to hold message responses
 * @returns API return code
 */
static int
cmsg_api_file_response (const char *filename,
                        const ProtobufCMessageDescriptor *output_desc,
                        ProtobufCMessage **recv_msg)
{
    /* File response */
    if (access (filename, F_OK) == -1)
    {
        recv_msg[0] = cmsg_create_ant_response (NULL, ANT_CODE_OK, output_desc);
    }
    else
    {
        recv_msg[0] = cmsg_get_msg_from_file (output_desc, filename);
        if (recv_msg[0] == NULL)
        {
            return CMSG_RET_ERR;
        }
    }
    return CMSG_RET_OK;
}

/**
 * Check if service is available and if not, generate response message.
 * Requires recv_msg to either be ant_result or have an ant_result field called _error_info)
 * @param check_params Parameters to use for service check.
 * @param output_desc descriptor for message to be generated
 * @param recv_msg array to hold response. (Response is only set in first entry)
 * @returns true if service is available/supported, else false.
 */
static bool
cmsg_supported_service_check (const service_support_parameters *check_params,
                              const ProtobufCMessageDescriptor *output_desc,
                              ProtobufCMessage **recv_msg)
{
    /* Service support check */
    if (access (check_params->filename, F_OK) == -1)
    {
        recv_msg[0] = cmsg_create_ant_response (check_params->msg,
                                                check_params->return_code, output_desc);

        return false;
    }
    return true;
}

/**
 * Invoke a CMSG API
 * The call to this function is intended to be auto-generated, so shouldn't be manually
 * called.
 * @param client cmsg client for API call
 * @param cmsg_desc CMSG API descriptor for the service being called
 * @param method_index index of method being called
 * @param send_msg message to be sent to the server
 * @param recv_msg array pointer to hold message responses
 * @returns API return code
 */
int
cmsg_api_invoke (cmsg_client *client, const cmsg_api_descriptor *cmsg_desc,
                 int method_index, const ProtobufCMessage *send_msg,
                 ProtobufCMessage **recv_msg)
#ifdef HAVE_UNITTEST
{
    /* This allows mock function for cmsg_api_invoke to still call the real code
     * in some cases (if only certain APIs should be mocked and not others) */
    return cmsg_api_invoke_real (client, cmsg_desc, method_index, send_msg, recv_msg);
}

int
cmsg_api_invoke_real (cmsg_client *client, const cmsg_api_descriptor *cmsg_desc,
                      int method_index, const ProtobufCMessage *send_msg,
                      ProtobufCMessage **recv_msg)
#endif /*HAVE_UNITTEST */
{
    ProtobufCService *service = (ProtobufCService *) client;
    const ProtobufCServiceDescriptor *service_desc = cmsg_desc->service_desc;
    const cmsg_method_client_extensions *extensions =
        cmsg_desc->method_extensions[method_index];
    ProtobufCMessage *dummy = NULL;

    /* test that the pointer to the client is valid before doing anything else */
    if (service == NULL)
    {
        return CMSG_RET_ERR;
    }
    assert (service->descriptor == service_desc);
    cmsg_api_recv_ptr_null_check (client, recv_msg,
                                  service->descriptor->methods[method_index].name);

    if (extensions)
    {
        if (extensions->response_filename)
        {
            return cmsg_api_file_response (extensions->response_filename,
                                           service->descriptor->
                                           methods[method_index].output, recv_msg);
        }

        if (extensions->service_support)
        {
            if (!cmsg_supported_service_check (extensions->service_support,
                                               service_desc->methods[method_index].output,
                                               recv_msg))
            {
                return CMSG_RET_OK;
            }
        }
    }

    if (!send_msg)
    {
        const ProtobufCMessageDescriptor *input_desc =
            service->descriptor->methods[method_index].input;
        if (input_desc->n_fields == 0)
        {
            dummy = CMSG_MALLOC (input_desc->sizeof_message);
            protobuf_c_message_init (input_desc, dummy);
            send_msg = dummy;
        }
    }
    cmsg_client_closure_data closure_data[CMSG_RECV_ARRAY_SIZE] =
        { { NULL, NULL, CMSG_RET_ERR } };
    /* Send! */
    service->invoke (service, method_index, send_msg, NULL, &closure_data);
    CMSG_FREE (dummy);

    return cmsg_api_process_closure_data (closure_data, recv_msg);
}

/**
 * Enable encryption for this clients connections.
 *
 * @param client - The client to enable encryption for.
 * @param sa - The SA structure to use for the encrypted connection.
 * @param derive_func - The user supplied callback function to derive the crypto sa
 *                      before sending the nonce to the server.
 *
 * @return CMSG_RET_OK on success, CMSG_RET_ERR on failure.
 */
int32_t
cmsg_client_crypto_enable (cmsg_client *client, cmsg_crypto_sa *sa,
                           crypto_sa_derive_func_t derive_func)
{
    if (sa == NULL || derive_func == NULL)
    {
        return CMSG_RET_ERR;
    }

    client->crypto_sa = sa;
    client->crypto_sa_derive_func = derive_func;

    return CMSG_RET_OK;
}

/**
 * Is encrypted connections enabled for this client.
 *
 * @param client - The client to check.
 *
 * @returns true if enabled, false otherwise.
 */
bool
cmsg_client_crypto_enabled (cmsg_client *client)
{
    return (client->crypto_sa != NULL);
}
