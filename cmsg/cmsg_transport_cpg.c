/**
 * cmsg-transport-cpg.c
 *
 * CPG Transport support
 *
 * CMSG requires a server (for receiving) and a client (for sending).
 * The transport instances can be the same(???)
 *
 * SERVER
 * The server will initialise the cpg handle, and join the group when the
 * listen call is done - this will be done in a blocking way so that it
 * will loop until connected.  The fd can be gotten for listening to.  It receives
 * when there is a message to be dispatched.  Messages are dispatched by calling
 * server recv.  The cpg_handle will be stored allowing the server & client to
 * share it.
 *
 * CLIENT
 * The client is for sending messages.  It will use the same cpg_handle created
 * by the server.
 * CPG supports flow control.  The call to send may fail as CPG is congested.
 * ATL_1716_TODO - does cmsg handle the congestion & block or does the application?
 *
 * We know from past experience that multiple accesses when sending can end up
 * in corruption, but this will be a requirement on the application to make sure
 * they only send 1 message at a time (typically use a mutex/lock/sem in the
 * transmission function).
 *
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#include "cmsg_private.h"
#include "cmsg_transport.h"
#include "cmsg_server.h"
#include "cmsg_error.h"

/*
 * Definitions
 */
#define CPG_CONNECTION_TIMEOUT 180
#define CPG_JOIN_TIMEOUT 30
#define TV_USEC_PER_SEC 1000000
#define SLEEP_TIME_us ((TV_USEC_PER_SEC) / 10)

static void _cmsg_cpg_confchg_fn (cpg_handle_t handle, struct cpg_name *group_name,
                                  struct cpg_address *member_list, int member_list_entries,
                                  struct cpg_address *left_list, int left_list_entries,
                                  struct cpg_address *joined_list, int joined_list_entries);
static void _cmsg_cpg_deliver_fn (cpg_handle_t handle, const struct cpg_name *group_name,
                                  uint32_t nodeid, uint32_t pid, void *msg, int msg_len);

static int32_t _cmsg_transport_cpg_init_exe_connection (void);


/*
 * Global variables
 */
GHashTable *cpg_group_name_to_server_hash_table_h = NULL;
static cpg_handle_t cmsg_cpg_handle = 0;

cpg_callbacks_t cmsg_cpg_callbacks = {
    (cpg_deliver_fn_t) _cmsg_cpg_deliver_fn,
    (cpg_confchg_fn_t) _cmsg_cpg_confchg_fn
};

/*****************************************************************************/
/******************* Functions ***********************************************/
/*****************************************************************************/
static void
_cmsg_cpg_confchg_fn (cpg_handle_t handle, struct cpg_name *group_name,
                      struct cpg_address *member_list, int member_list_entries,
                      struct cpg_address *left_list, int left_list_entries,
                      struct cpg_address *joined_list, int joined_list_entries)
{
    cmsg_server *server;

    /* Find the server matching this group.
     */
    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] Group name used for lookup: %s\n",
                group_name->value);
    server = (cmsg_server *) g_hash_table_lookup (cpg_group_name_to_server_hash_table_h,
                                                  (gconstpointer) group_name->value);

    if (!server)
    {
        CMSG_LOG_GEN_ERROR ("Server lookup failed for group %s", group_name->value);
        return;
    }

    if (server->_transport->config.cpg.configchg_cb != NULL)
    {
        server->_transport->config.cpg.configchg_cb (member_list, member_list_entries,
                                                     left_list, left_list_entries,
                                                     joined_list, joined_list_entries);
    }
    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] %s\n", __FUNCTION__);
}


/**
 * cmsg_cpg_deliver_fn
 * The callback that receives a message for the server.
 */
static void
_cmsg_cpg_deliver_fn (cpg_handle_t handle, const struct cpg_name *group_name,
                      uint32_t nodeid, uint32_t pid, void *msg, int msg_len)
{
    cmsg_header *header_received;
    cmsg_header header_converted;
    int32_t dyn_len;
    uint8_t *buffer = 0;
    uint32_t extra_header_size;

    cmsg_server *server;
    cmsg_server_request server_request;

    header_received = (cmsg_header *) msg;

    if (cmsg_header_process (header_received, &header_converted) != CMSG_RET_OK)
    {
        // Couldn't process the header for some reason
        CMSG_LOG_GEN_ERROR ("Unable to process message header for server receive. Group:%s",
                            group_name->value);
        return;
    }

    server_request.msg_type = header_converted.msg_type;
    server_request.message_length = header_converted.message_length;

    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] cpg received header\n");

    dyn_len = header_converted.message_length;

    CMSG_DEBUG (CMSG_INFO,
                "[TRANSPORT] cpg msg len = %d, header length = %u, data length = %d\n",
                msg_len, header_converted.header_length, dyn_len);

    if (msg_len < (header_converted.header_length + dyn_len))
    {
        CMSG_LOG_GEN_ERROR
            ("CPG message len (%d) larger than data buffer len (%d). Group:%s", msg_len,
             header_converted.header_length + dyn_len, group_name->value);
        return;
    }

    buffer = (uint8_t *) ((uint8_t *) msg + sizeof (cmsg_header));

    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] received data\n");
    cmsg_buffer_print (buffer, dyn_len);

    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] Group name used for lookup: %s\n",
                group_name->value);
    server = (cmsg_server *) g_hash_table_lookup (cpg_group_name_to_server_hash_table_h,
                                                  (gconstpointer) group_name->value);

    if (!server)
    {
        CMSG_LOG_GEN_ERROR ("Server lookup failed for group %s", group_name->value);
        return;
    }

    extra_header_size = header_converted.header_length - sizeof (cmsg_header);

    if (cmsg_tlv_header_process (buffer, &server_request, extra_header_size,
                                 server->service->descriptor) == CMSG_RET_OK)
    {
        server->server_request = &server_request;
        buffer = buffer + extra_header_size;

        if (server->message_processor (server, buffer))
        {
            CMSG_LOG_TRANSPORT_ERROR (server->_transport,
                                      "Unable to process message header");
        }
    }
}


/**
 * cmsg_transport_cpg_client_connect
 *
 * Client function to connect to the server.
 * Under CPG this is just going to reuse the existing connection created by
 * creating a server to send messages to the CPG executable.
 */
static int32_t
cmsg_transport_cpg_client_connect (cmsg_transport *transport, int timeout)
{
    if (!transport || transport->config.cpg.group_name.value[0] == '\0')
    {
        CMSG_LOG_GEN_ERROR ("CPG connect failed. Invalid arguments.");
    }
    else
    {
        CMSG_DEBUG (CMSG_INFO,
                    "[TRANSPORT] cpg connect group name: %s\n",
                    transport->config.cpg.group_name.value);
    }

    if (cmsg_cpg_handle == 0)
    {
        /* CPG handle hasn't been created yet. */
        CMSG_LOG_TRANSPORT_ERROR (transport, "Unable to find matching handle for group %s",
                                  transport->config.cpg.group_name.value);
        return -1;
    }

    /* CPG handle has been created so use it. */
    transport->connection.cpg.handle = cmsg_cpg_handle;
    return 0;
}


/**
 * _cmsg_transport_cpg_init_exe_connection
 *
 * Initialises the connection with the CPG executable.
 *
 * Times out after 10 seconds of attempting to connect to the executable.
 */
static int32_t
_cmsg_transport_cpg_init_exe_connection (void)
{
    uint32_t slept_us = 0;
    cpg_error_t result = CPG_OK;

    do
    {
        result = cpg_initialize (&cmsg_cpg_handle, &cmsg_cpg_callbacks);

        if (result == CPG_OK)
        {
            return 0;
        }

        if (!(result == CPG_ERR_TRY_AGAIN || result == CPG_ERR_NOT_EXIST))
        {
            break;
        }

        usleep (SLEEP_TIME_us);
        slept_us += SLEEP_TIME_us;
    }
    while (slept_us <= (TV_USEC_PER_SEC * CPG_CONNECTION_TIMEOUT));

    CMSG_LOG_GEN_ERROR ("Unable to initialize CPG service. Result:%d, Waited:%ums",
                        result, slept_us / 1000);
    return -1;
}


/**
 * _cmsg_transport_cpg_join_group
 * Joins the group specified in the transport connection information.
 *
 * Timesout after 10 seconds of attempting to join the group.
 */
static uint32_t
_cmsg_transport_cpg_join_group (cmsg_transport *transport)
{
    uint32_t slept_us = 0;
    cpg_error_t result;

    do
    {
        result = cpg_join (transport->connection.cpg.handle,
                           &transport->config.cpg.group_name);

        if (result == CPG_OK)
        {
            return 0;
        }

        if (result != CPG_ERR_TRY_AGAIN)
        {
            break;
        }

        usleep (SLEEP_TIME_us);
        slept_us += SLEEP_TIME_us;
    }
    while (slept_us <= (TV_USEC_PER_SEC * CPG_JOIN_TIMEOUT));

    CMSG_LOG_TRANSPORT_ERROR (transport,
                              "Unable to join CPG group %s. Result:%d, Waited:%ums",
                              transport->config.cpg.group_name.value, result,
                              slept_us / 1000);

    return -1;
}


/**
 * cmsg_transport_cpg_server_listen
 *
 * Server function to start listening to CPG.  Joins the group and allows
 * the application to receive messages.
 */
static int32_t
cmsg_transport_cpg_server_listen (cmsg_transport *transport)
{
    int res = 0;
    int fd = 0;

    if (!transport || transport->config.cpg.group_name.value[0] == '\0')
    {
        CMSG_LOG_GEN_ERROR ("Invalid parameter for cpg server listen.");
        return -1;
    }
    else
    {
        CMSG_DEBUG (CMSG_INFO,
                    "[TRANSPORT] cpg listen group name: %s\n",
                    transport->config.cpg.group_name.value);
    }

    /* If CPG connection has not been created do it now.
     */
    if (cmsg_cpg_handle == 0)
    {
        res = _cmsg_transport_cpg_init_exe_connection ();
        if (res < 0)
        {
            CMSG_LOG_TRANSPORT_ERROR (transport, "CPG listen init failed. Result %d", res);
            return -1;
        }
    }

    transport->connection.cpg.handle = cmsg_cpg_handle;

    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] server added %llu to hash table\n",
                transport->connection.cpg.handle);

    res = _cmsg_transport_cpg_join_group (transport);

    if (res < 0)
    {
        CMSG_LOG_TRANSPORT_ERROR (transport, "CPG listen join failed. Result %d", res);
        return -2;
    }

    if (cpg_fd_get (transport->connection.cpg.handle, &fd) == CPG_OK)
    {
        transport->connection.cpg.fd = fd;
        CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] cpg listen got fd: %d\n", fd);
    }
    else
    {
        transport->connection.cpg.fd = 0;
        CMSG_LOG_TRANSPORT_ERROR (transport, "CPG listen unable to get FD");
        return -3;
    }

    return 0;
}


/**
 * cmsg_transport_cpg_server_recv
 * Receives all the messages that are ready to be received.
 *
 * This should be run in a dedicated thread.
 */
static int32_t
cmsg_transport_cpg_server_recv (int32_t server_socket, cmsg_server *server,
                                uint8_t **recv_buffer, cmsg_header *processed_header,
                                int *nbytes)
{
    int ret;

    ret = cpg_dispatch (server->_transport->connection.cpg.handle, CPG_DISPATCH_ALL);

    if (ret != CPG_OK)
    {
        CMSG_LOG_TRANSPORT_ERROR (server->_transport, "CPG dispatch failed. Error:%d", ret);
        return -1;
    }

    return 0;   // Success
}


/**
 * CPG clients do not receive a reply to their messages. This
 * function therefore returns NULL. It should not be called by the client, but
 * it prevents a null pointer exception from occurring if no function is
 * defined
 */
static cmsg_status_code
cmsg_transport_cpg_client_recv (cmsg_transport *transport,
                                const ProtobufCServiceDescriptor *descriptor,
                                ProtobufCMessage **messagePtPt)
{

    *messagePtPt = NULL;
    return CMSG_STATUS_CODE_SUCCESS;
}


static uint32_t
cmsg_transport_cpg_is_congested (cmsg_transport *transport)
{
    static int32_t cpg_error_count = 0;
    cpg_flow_control_state_t flow_control;
    cpg_error_t cpg_rc;

    /* get this CPG's flow control status from the AIS library */
    cpg_rc = cpg_flow_control_state_get (transport->connection.cpg.handle, &flow_control);
    if (cpg_rc != CPG_OK)
    {
        if ((cpg_error_count % 16) == 0)
        {
            CMSG_LOG_TRANSPORT_ERROR (transport,
                                      "Unable to get CPG flow control state - hndl %llx %d",
                                      (long long int) transport->connection.cpg.handle,
                                      (int) cpg_rc);
        }
        cpg_error_count++;
        return TRUE;
    }

    cpg_error_count = 0;
    return (flow_control == CPG_FLOW_CONTROL_ENABLED);
}


/**
 * cmsg_transport_cpg_client_send
 * Sends the message.
 *
 * Assumed that this will only be called by 1 thread at a time.  Any locking required
 * to make this so will be implemented by the application.
 *
 * Returns
 */
static int32_t
cmsg_transport_cpg_client_send (cmsg_transport *transport, void *buff, int length, int flag)
{
    struct iovec iov;
    uint32_t res = CPG_OK;

    iov.iov_len = length;
    iov.iov_base = buff;

    /* Block the current thread until CPG is not congested */
    while (transport->send_can_block)
    {

        /* Check this CPG's flow control status from the AIS library */
        if (!cmsg_transport_cpg_is_congested (transport))
        {
            break;
        }

        /* Give CPG a chance to relieve the congestion */
        usleep (1000);
    }

    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] cpg send message to handle %llu\n",
                transport->connection.cpg.handle);

    /* Keep trying to send the message until it succeeds (e.g. blocks)
     */
    while (transport->send_can_block)
    {
        /* Attempt to send message. */
        res = cpg_mcast_joined (transport->connection.cpg.handle, CPG_TYPE_AGREED, &iov, 1);
        if (res != CPG_ERR_TRY_AGAIN)
        {
            break;  /* message sent, or failure, quit loop now. */
        }

        /* Give CPG a chance to relieve the congestion */
        usleep (100000);
    }

    if (res != CPG_OK)
    {
        CMSG_LOG_TRANSPORT_ERROR (transport, "CPG multicast joined failed. Error:%d", res);
        return -1;
    }

    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] CPG_OK\n");
    return length;
}


/**
 * cmsg_transport_cpg_server_send
 *
 * Servers don't send and so this function isn't implemented.
 */
static int32_t
cmsg_transport_cpg_server_send (cmsg_transport *transport, void *buff, int length, int flag)
{
    return 0;
}


/**
 * Client doesn't close when the message/response has been sent.
 */
static void
cmsg_transport_cpg_client_close (cmsg_transport *transport)
{
    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] client cpg close done nothing\n");
}


/**
 * Server doesn't close when the message/response has been sent.
 */
static void
cmsg_transport_cpg_server_close (cmsg_transport *transport)
{
    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] server cpg close done nothing\n");
}

static void
cmsg_transport_cpg_client_destroy (cmsg_transport *transport)
{
    //placeholder to make sure destroy functions are called in the right order
}

static void
cmsg_transport_cpg_server_destroy (cmsg_transport *transport)
{
    int res;

    /* Cleanup our entries in the hash table.
     */
    g_hash_table_remove (cpg_group_name_to_server_hash_table_h,
                         transport->config.cpg.group_name.value);

    /* Leave the CPG group.
     */
    cpg_leave (cmsg_cpg_handle, &(transport->config.cpg.group_name));

    /* If there are no more servers then finalize the cpg connection.
     * Finalize sends the right things to other CPG members, and frees memory.
     */
    if (g_hash_table_size (cpg_group_name_to_server_hash_table_h) == 0)
    {
        CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] finalize the CPG connection\n");
        res = cpg_finalize (transport->connection.cpg.handle);

        if (res != CPG_OK)
        {
            CMSG_LOG_TRANSPORT_ERROR (transport, "Failed to finalise CPG. Error:%d", res);
        }

        cmsg_cpg_handle = 0;
    }

    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] cpg destroy done\n");
}


static int
cmsg_transport_cpg_server_get_socket (cmsg_transport *transport)
{
    int fd = 0;
    if (cpg_fd_get (transport->connection.cpg.handle, &fd) == CPG_OK)
    {
        return fd;
    }
    else
    {
        return -1;
    }
}


/**
 * The client has no socket to get so return 0.
 */
static int
cmsg_transport_cpg_client_get_socket (cmsg_transport *transport)
{
    return 0;
}


int32_t
cmsg_transport_cpg_send_can_block_enable (cmsg_transport *transport,
                                          uint32_t send_can_block)
{
    transport->send_can_block = send_can_block;
    return 0;
}


int32_t
cmsg_transport_cpg_ipfree_bind_enable (cmsg_transport *transport,
                                       cmsg_bool_t use_ipfree_bind)
{
    /* not supported yet */
    return -1;
}


void
cmsg_transport_cpg_init (cmsg_transport *transport)
{
    if (transport == NULL)
    {
        return;
    }

    transport->config.cpg.configchg_cb = NULL;

    transport->tport_funcs.connect = cmsg_transport_cpg_client_connect;
    transport->tport_funcs.listen = cmsg_transport_cpg_server_listen;
    transport->tport_funcs.server_recv = cmsg_transport_cpg_server_recv;
    transport->tport_funcs.client_recv = cmsg_transport_cpg_client_recv;
    transport->tport_funcs.client_send = cmsg_transport_cpg_client_send;
    transport->tport_funcs.server_send = cmsg_transport_cpg_server_send;

    transport->tport_funcs.closure = cmsg_server_closure_oneway;

    transport->tport_funcs.client_close = cmsg_transport_cpg_client_close;
    transport->tport_funcs.server_close = cmsg_transport_cpg_server_close;

    transport->tport_funcs.s_socket = cmsg_transport_cpg_server_get_socket;
    transport->tport_funcs.c_socket = cmsg_transport_cpg_client_get_socket;

    transport->tport_funcs.client_destroy = cmsg_transport_cpg_client_destroy;
    transport->tport_funcs.server_destroy = cmsg_transport_cpg_server_destroy;

    transport->tport_funcs.is_congested = cmsg_transport_cpg_is_congested;
    transport->tport_funcs.send_can_block_enable = cmsg_transport_cpg_send_can_block_enable;
    transport->tport_funcs.ipfree_bind_enable = cmsg_transport_cpg_ipfree_bind_enable;

    if (cpg_group_name_to_server_hash_table_h == NULL)
    {
        cpg_group_name_to_server_hash_table_h = g_hash_table_new (g_str_hash, g_str_equal);
    }

    CMSG_DEBUG (CMSG_INFO, "%s: done\n", __FUNCTION__);
}
