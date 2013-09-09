/**
 * protobuf-c-transport-cpg.c
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
 */
#include "protobuf-c-cmsg-transport.h"
#include "protobuf-c-cmsg-client.h"
#include "protobuf-c-cmsg-server.h"

/*
 * Definitions
 */
#define TV_USEC_PER_SEC 1000000
#define SLEEP_TIME_us ((TV_USEC_PER_SEC) / 10)

static void _cmsg_cpg_confchg_fn (cpg_handle_t handle, struct cpg_name *group_name,
                                  struct cpg_address *member_list, int member_list_entries,
                                  struct cpg_address *left_list, int left_list_entries,
                                  struct cpg_address *joined_list, int joined_list_entries);
static void _cmsg_cpg_deliver_fn (cpg_handle_t handle, const struct cpg_name *group_name,
                                  uint32_t nodeid, uint32_t pid, void *msg, int msg_len);

/*
 * Global variables
 */
static GHashTable *cpg_group_name_to_server_hash_table_h = NULL;
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
    DEBUG (CMSG_INFO, "[TRANSPORT] Group name used for lookup: %s\n", group_name->value);
    server = (cmsg_server *) g_hash_table_lookup (cpg_group_name_to_server_hash_table_h,
                                                  (gconstpointer) group_name->value);

    if (!server)
    {
        DEBUG (CMSG_ERROR, "[TRANSPORT] Server lookup failed\n");
        return;
    }

    if (server->_transport->config.cpg.configchg_cb != NULL)
    {
        server->_transport->config.cpg.configchg_cb (server, member_list,
                                                     member_list_entries, left_list,
                                                     left_list_entries, joined_list,
                                                     joined_list_entries);
    }
    DEBUG (CMSG_INFO, "[TRANSPORT] %s\n", __FUNCTION__);
}


/**
 * cmsg_cpg_deliver_fn
 * The callback that receives a message.
 */
static void
_cmsg_cpg_deliver_fn (cpg_handle_t handle, const struct cpg_name *group_name,
                      uint32_t nodeid, uint32_t pid, void *msg, int msg_len)
{
    cmsg_header_request header_received;
    cmsg_header_request header_converted;
    int32_t client_len;
    int32_t nbytes;
    int32_t dyn_len;
    int32_t ret = 0;
    uint8_t *buffer = 0;

    cmsg_server *server;
    cmsg_server_request server_request;

    memcpy (&header_received, msg, sizeof (cmsg_header_request));

    header_converted.method_index =
        cmsg_common_uint32_from_le (header_received.method_index);
    header_converted.message_length =
        cmsg_common_uint32_from_le (header_received.message_length);
    header_converted.request_id = header_received.request_id;

    DEBUG (CMSG_INFO, "[TRANSPORT] cpg received header\n");
    cmsg_buffer_print ((void *) &header_received, sizeof (cmsg_header_request));

    DEBUG (CMSG_INFO,
           "[TRANSPORT] cpg method_index   host: %d, wire: %d\n",
           header_converted.method_index, header_received.method_index);

    DEBUG (CMSG_INFO,
           "[TRANSPORT] cpg message_length host: %d, wire: %d\n",
           header_converted.message_length, header_received.message_length);

    DEBUG (CMSG_INFO,
           "[TRANSPORT] cpg request_id     host: %d, wire: %d\n",
           header_converted.request_id, header_received.request_id);

    server_request.message_length =
        cmsg_common_uint32_from_le (header_received.message_length);
    server_request.method_index = cmsg_common_uint32_from_le (header_received.method_index);
    server_request.request_id = header_received.request_id;

    dyn_len = header_converted.message_length;

    DEBUG (CMSG_INFO,
           "[TRANSPORT] cpg msg len = %d, header length = %ld, data length = %d\n",
           msg_len, sizeof (cmsg_header_request), dyn_len);

    if (msg_len < sizeof (cmsg_header_request) + dyn_len)
    {
        DEBUG (CMSG_ERROR, "[TRANSPORT] cpg Message larger than data buffer passed in\n");
        return;
    }

    buffer = msg + sizeof (cmsg_header_request);

    DEBUG (CMSG_INFO, "[TRANSPORT] received data\n");
    cmsg_buffer_print (buffer, dyn_len);

    DEBUG (CMSG_INFO, "[TRANSPORT] Group name used for lookup: %s\n", group_name->value);
    server = (cmsg_server *) g_hash_table_lookup (cpg_group_name_to_server_hash_table_h,
                                                  (gconstpointer) group_name->value);

    if (!server)
    {
        DEBUG (CMSG_ERROR, "[TRANSPORT] Server lookup failed\n");
        return;
    }

    server->server_request = &server_request;

    if (server->message_processor (server, buffer))
        DEBUG (CMSG_ERROR, "[TRANSPORT] message processing returned an error\n");
}


/**
 * cmsg_transport_cpg_client_connect
 *
 * Client function to connect to the server.
 * Under CPG this is just going to reuse the existing connection created by
 * creating a server to send messages to the CPG executable.
 */
static int32_t
cmsg_transport_cpg_client_connect (cmsg_client *client)
{
    cpg_handle_t *handlePt = NULL;

    if (!client || !client->_transport ||
        client->_transport->config.cpg.group_name.value[0] == '\0')
    {
        DEBUG (CMSG_ERROR, "[TRANSPORT] cpg connect sanity check failed\n");
    }
    else
    {
        DEBUG (CMSG_INFO,
               "[TRANSPORT] cpg connect group name: %s\n",
               client->_transport->config.cpg.group_name.value);
    }

    if (cmsg_cpg_handle == 0)
    {
        /* CPG handle hasn't been created yet.
         */
        client->state = CMSG_CLIENT_STATE_FAILED;
        DEBUG (CMSG_ERROR, "[TRANSPORT] Couldn't find matching handle for group %s\n",
               client->_transport->config.cpg.group_name.value);
        return -1;
    }

    /* CPG handle has been created so use it.
     */
    client->connection.handle = cmsg_cpg_handle;
    client->state = CMSG_CLIENT_STATE_CONNECTED;
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
    unsigned int slept_us = 0;
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
    while (slept_us <= (TV_USEC_PER_SEC * 10));

    DEBUG (CMSG_ERROR,
           "Couldn't initialize CPG service result:%d, waited:%ums",
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
_cmsg_transport_cpg_join_group (cmsg_server *server)
{
    unsigned int slept_us = 0;
    cpg_error_t result;

    do
    {
        result = cpg_join (server->connection.cpg.handle,
                           &server->_transport->config.cpg.group_name);

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
    while (slept_us <= (TV_USEC_PER_SEC * 10));

    DEBUG (CMSG_ERROR,
           "Couldn't join CPG group %s, result:%d, waited:%ums",
           server->_transport->config.cpg.group_name.value, result, slept_us / 1000);

    return -1;
}


/**
 * cmsg_transport_cpg_server_listen
 *
 * Server function to start listening to CPG.  Joins the group and allows
 * the application to receive messages.
 */
static int32_t
cmsg_transport_cpg_server_listen (cmsg_server *server)
{
    int res = 0;
    int fd = 0;

    if (!server || !server->_transport ||
        server->_transport->config.cpg.group_name.value[0] == '\0')
    {
        DEBUG (CMSG_ERROR, "[TRANSPORT] cpg listen sanity check failed\n");
        return -1;
    }
    else
    {
        DEBUG (CMSG_INFO,
               "[TRANSPORT] cpg listen group name: %s\n",
               server->_transport->config.cpg.group_name.value);
    }

    /* If CPG connection has not been created do it now.
     */
    if (cmsg_cpg_handle == 0)
    {
        res = _cmsg_transport_cpg_init_exe_connection ();
        if (res < 0)
        {
            DEBUG (CMSG_ERROR, "[TRANSPORT] cpg listen init failed, result %d\n", res);
            return -1;
        }
    }

    server->connection.cpg.handle = cmsg_cpg_handle;

    /* Add entry into the hash table for the server to be found by cpg group name.
     */
    g_hash_table_insert (cpg_group_name_to_server_hash_table_h,
                         (gpointer) server->_transport->config.cpg.group_name.value,
                         (gpointer) server);

    DEBUG (CMSG_INFO, "[TRANSPORT] server added %lu to hash table\n",
           server->connection.cpg.handle);

    res = _cmsg_transport_cpg_join_group (server);

    if (res < 0)
    {
        DEBUG (CMSG_ERROR, "[TRANSPORT] cpg listen join failed, result %d\n", res);
        return -2;
    }

    if (cpg_fd_get (server->connection.cpg.handle, &fd) == CPG_OK)
    {
        server->connection.cpg.fd = fd;
        DEBUG (CMSG_INFO, "[TRANSPORT] cpg listen got fd: %d\n", fd);
    }
    else
    {
        server->connection.cpg.fd = 0;
        DEBUG (CMSG_ERROR, "[TRANSPORT] cpg listen cannot get fd\n");
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
cmsg_transport_cpg_server_recv (int32_t socket, cmsg_server *server)
{
    int ret;

    ret = cpg_dispatch (server->connection.cpg.handle, CPG_DISPATCH_ALL);

    if (ret != CPG_OK)
    {
        DEBUG (CMSG_ERROR, "[TRANSPORT] cpg serv recv dispatch returned error %d\n", ret);
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
cmsg_transport_cpg_client_recv (cmsg_client *client, ProtobufCMessage **messagePtPt)
{

    *messagePtPt = NULL;
    return CMSG_STATUS_CODE_SUCCESS;
}


static uint32_t
cmsg_transport_cpg_is_congested (cmsg_client *client)
{
    static int32_t cpg_error_count = 0;
    cpg_flow_control_state_t flow_control;
    cpg_error_t cpg_rc;

    /* get this CPG's flow control status from the AIS library */
    cpg_rc = cpg_flow_control_state_get (client->connection.handle, &flow_control);
    if (cpg_rc != CPG_OK)
    {
        if ((cpg_error_count % 16) == 0)
        {
            DEBUG (CMSG_ERROR,
                   "[TRANSPORT] Unable to get CPG flow control state - hndl %#llx %u",
                   client->connection.handle, cpg_rc);
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
cmsg_transport_cpg_client_send (cmsg_client *client, void *buff, int length, int flag)
{
    struct iovec iov;
    unsigned int res;

    iov.iov_len = length;
    iov.iov_base = buff;

    if (client->state != CMSG_CLIENT_STATE_CONNECTED)
    {
        DEBUG (CMSG_ERROR,
               "[TRANSPORT] CPG Client is not connected prior to attempting to send to group %s\n",
               client->_transport->config.cpg.group_name.value);
        return -1;
    }

    if (client->_transport->send_called_multi_enabled)
    {
        // Get send lock to make sure we are the only one sending
        pthread_mutex_lock (&(client->_transport->send_lock));
    }

    /* Block the current thread until CPG is not congested */
    while (client->_transport->send_can_block)
    {

        /* Check this CPG's flow control status from the AIS library */
        if (!cmsg_transport_cpg_is_congested (client))
            break;

        /* Give CPG a chance to relieve the congestion */
        usleep (1000);
    }

    DEBUG (CMSG_INFO, "[TRANSPORT] cpg send message to handle  %lu\n",
           client->connection.handle);

    /* Keep trying to send the message until it succeeds (e.g. blocks)
     */
    while (client->_transport->send_can_block)
    {
        /* Attempt to send message. */
        res = cpg_mcast_joined (client->connection.handle, CPG_TYPE_AGREED, &iov, 1);
        if (res != CPG_ERR_TRY_AGAIN)
            break;  /* message sent, or failure, quit loop now. */

        /* Give CPG a chance to relieve the congestion */
        usleep (100000);
    }

    if (client->_transport->send_called_multi_enabled)
    {
        pthread_mutex_unlock (&client->_transport->send_lock);
    }

    if (res != CPG_OK)
    {
        DEBUG (CMSG_ERROR, "[TRANSPORT] an error %d\n", res);
        return -1;
    }

    DEBUG (CMSG_INFO, "[TRANSPORT] CPG_OK\n");
    return length;
}


/**
 * cmsg_transport_cpg_server_send
 *
 * Servers don't send and so this function isn't implemented.
 */
static int32_t
cmsg_transport_cpg_server_send (cmsg_server *server, void *buff, int length, int flag)
{
    return 0;
}


/**
 * Client doesn't close when the message/response has been sent.
 */
static void
cmsg_transport_cpg_client_close (cmsg_client *client)
{
    DEBUG (CMSG_INFO, "[TRANSPORT] client cpg close done nothing\n");
}


/**
 * Server doesn't close when the message/response has been sent.
 */
static void
cmsg_transport_cpg_server_close (cmsg_server *server)
{
    DEBUG (CMSG_INFO, "[TRANSPORT] server cpg close done nothing\n");
}

static void
cmsg_transport_cpg_client_destroy (cmsg_client *cmsg_client)
{
    //placeholder to make sure destroy functions are called in the right order
}

static void
cmsg_transport_cpg_server_destroy (cmsg_server *server)
{
    int res;
    gboolean ret;

    /* Cleanup our entries in the hash table.
     */
    ret = g_hash_table_remove (cpg_group_name_to_server_hash_table_h,
                               (gpointer *) server->_transport->config.cpg.group_name.value);
    DEBUG (CMSG_INFO, "[TRANSPORT] cpg group name hash table remove, result %d\n", ret);

    /* Leave the CPG group.
     */
    cpg_leave (cmsg_cpg_handle, &(server->_transport->config.cpg.group_name));

    /* If there are no more servers then finalize the cpg connection.
     * Finalize sends the right things to other CPG members, and frees memory.
     */
    if (g_hash_table_size (cpg_group_name_to_server_hash_table_h) == 0)
    {
        DEBUG (CMSG_INFO, "[TRANSPORT] finalize the CPG connection\n");
        res = cpg_finalize (server->connection.cpg.handle);

        if (res != CPG_OK)
        {
            DEBUG (CMSG_ERROR, "[TRANSPORT] cpg close failed, result %d\n", res);
        }

        cmsg_cpg_handle = 0;
    }

    DEBUG (CMSG_INFO, "[TRANSPORT] cpg destroy done\n");
}


static int
cmsg_transport_cpg_server_get_socket (cmsg_server *server)
{
    int fd = 0;
    if (cpg_fd_get (server->connection.cpg.handle, &fd) == CPG_OK)
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
cmsg_transport_cpg_client_get_socket (cmsg_client *client)
{
    return 0;
}


/**
 * Private function used by the hash table for cpg group names.
 *
 * Name is a string so add all characters together to generate a hash
 */
static guint
cmsg_transport_cpg_group_hash_function (gconstpointer key)
{
    char *string = (char *) key;
    guint hash = 0;
    int i = 0;

    for (i = 0; string[i] != 0; i++)
        hash += (guint) string[i];

    return (guint) hash;
}


/**
 * Private function used by the hash table for cpg handles.
 */
static gboolean
cmsg_transport_cpg_group_equal_function (gconstpointer a, gconstpointer b)
{
    return (strcmp ((char *) a, (char *) b) == 0);
}


int32_t
cmsg_transport_cpg_send_called_multi_threads_enable (cmsg_transport *transport,
                                                     uint32_t enable)
{
    if (enable)
    {
        if (pthread_mutex_init (&transport->send_lock, NULL) != 0)
        {
            DEBUG (CMSG_ERROR, "[TRANSPORT] error: send mutex init failed\n");
            memset (&transport->send_lock, 0, sizeof (transport->send_lock));
            return -1;
        }
    }
    transport->send_called_multi_enabled = enable;
    return 0;
}


int32_t
cmsg_transport_cpg_send_can_block_enable (cmsg_transport *transport,
                                          uint32_t send_can_block)
{
    transport->send_can_block = send_can_block;
    return 0;
}


void
cmsg_transport_cpg_init (cmsg_transport *transport)
{
    if (transport == NULL)
        return;

    transport->config.cpg.configchg_cb = NULL;

    transport->connect = cmsg_transport_cpg_client_connect;
    transport->listen = cmsg_transport_cpg_server_listen;
    transport->server_recv = cmsg_transport_cpg_server_recv;
    transport->client_recv = cmsg_transport_cpg_client_recv;
    transport->client_send = cmsg_transport_cpg_client_send;
    transport->server_send = cmsg_transport_cpg_server_send;

    transport->closure = cmsg_server_closure_oneway;
    transport->invoke = cmsg_client_invoke_oneway;

    transport->client_close = cmsg_transport_cpg_client_close;
    transport->server_close = cmsg_transport_cpg_server_close;

    transport->s_socket = cmsg_transport_cpg_server_get_socket;
    transport->c_socket = cmsg_transport_cpg_client_get_socket;

    transport->client_destroy = cmsg_transport_cpg_client_destroy;
    transport->server_destroy = cmsg_transport_cpg_server_destroy;

    transport->is_congested = cmsg_transport_cpg_is_congested;
    transport->send_called_multi_threads_enable =
        cmsg_transport_cpg_send_called_multi_threads_enable;
    transport->send_called_multi_enabled = FALSE;
    transport->send_can_block_enable = cmsg_transport_cpg_send_can_block_enable;

    if (cpg_group_name_to_server_hash_table_h == NULL)
    {
        cpg_group_name_to_server_hash_table_h =
            g_hash_table_new (cmsg_transport_cpg_group_hash_function,
                              cmsg_transport_cpg_group_equal_function);
    }

    DEBUG (CMSG_INFO, "%s: done\n", __FUNCTION__);
}
