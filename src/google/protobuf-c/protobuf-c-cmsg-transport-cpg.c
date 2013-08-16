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

/*
 * Global variables
 */
static GHashTable *cpg_group_name_to_server_hash_table_h = NULL;
static cpg_handle_t cmsg_cpg_handle = 0;

//TODO from cpg-rx.x
void
cpg_bm_confchg_fn (cpg_handle_t handle,
                   struct cpg_name *group_name,
                   struct cpg_address *member_list,
                   int member_list_entries,
                   struct cpg_address *left_list,
                   int left_list_entries,
                   struct cpg_address *joined_list,
                   int joined_list_entries)
{
    DEBUG (CMSG_INFO, "[TRANSPORT] %s\n", __FUNCTION__);
}


/**
 * cpg_deliver_fn
 * The callback that receives a message.
 */
void
cpg_deliver_fn (cpg_handle_t handle,
                   struct cpg_name *group_name,
                   uint32_t nodeid,
                   uint32_t pid,
                   void *msg,
                   int msg_len)
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

    header_converted.method_index = cmsg_common_uint32_from_le (header_received.method_index);
    header_converted.message_length = cmsg_common_uint32_from_le (header_received.message_length);
    header_converted.request_id = header_received.request_id;

    DEBUG (CMSG_INFO, "[TRANSPORT] cpg received header\n");
    cmsg_buffer_print ((void *)&header_received, sizeof (cmsg_header_request));

    DEBUG (CMSG_INFO,
           "[TRANSPORT] cpg method_index   host: %d, wire: %d\n",
           header_converted.method_index, header_received.method_index);

    DEBUG (CMSG_INFO,
           "[TRANSPORT] cpg message_length host: %d, wire: %d\n",
           header_converted.message_length, header_received.message_length);

    DEBUG (CMSG_INFO,
           "[TRANSPORT] cpg request_id     host: %d, wire: %d\n",
           header_converted.request_id, header_received.request_id);

    server_request.message_length = cmsg_common_uint32_from_le (header_received.message_length);
    server_request.method_index = cmsg_common_uint32_from_le (header_received.method_index);
    server_request.request_id = header_received.request_id;

    dyn_len = header_converted.message_length;

    DEBUG (CMSG_INFO,
           "[TRANSPORT] cpg msg len = %d, header length = %ld, data length = %d\n",
           msg_len, sizeof (cmsg_header_request), dyn_len);

    if (msg_len < sizeof (cmsg_header_request) + dyn_len)
    {
        DEBUG (CMSG_ERROR,
               "[TRANSPORT] cpg Message larger than data buffer passed in\n");
        return;
    }

    buffer = msg + sizeof (cmsg_header_request);

    DEBUG (CMSG_INFO, "[TRANSPORT] received data\n");
    cmsg_buffer_print (buffer, dyn_len);


    DEBUG (CMSG_INFO, "[TRANSPORT] Handle used for lookup: %lu\n", handle);
    server = (cmsg_server *)g_hash_table_lookup (cpg_group_name_to_server_hash_table_h, (gconstpointer)group_name->value);

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

    if (!client || !client->_transport || client->_transport->config.cpg.group_name.value[0] == '\0')
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
        DEBUG (CMSG_ERROR, "[TRANSPORT] Couldn't find matching handle for group %s\n", client->transport->config.cpg.group_name.value);
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
_cmsg_transport_cpg_init_exe_connection (cmsg_server *server)
{
    unsigned int slept_us = 0;
    cpg_error_t result;

    do
    {
        result = cpg_initialize (&server->connection.cpg.handle, &server->connection.cpg.callbacks);

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
    } while (slept_us <= (TV_USEC_PER_SEC * 10));

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
static int
_cmsg_transport_cpg_join_group (cmsg_server *server)
{
    unsigned int slept_us = 0;
    cpg_error_t result;

    do
    {
        result = cpg_join (server->connection.cpg.handle, &server->_transport->config.cpg.group_name);

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
    } while (slept_us <= (TV_USEC_PER_SEC * 10));

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
    unsigned int res;
    int fd = 0;

    if (!server || !server->_transport || server->_transport->config.cpg.group_name.value[0] == '\0')
    {
        DEBUG (CMSG_ERROR, "[TRANSPORT] cpg listen sanity check failed\n");
    }
    else
    {
        DEBUG (CMSG_INFO,
               "[TRANSPORT] cpg listen group name: %s\n",
               server->_transport->config.cpg.group_name.value);
    }

    server->connection.cpg.callbacks.cpg_deliver_fn = (void *)cpg_deliver_fn;
    server->connection.cpg.callbacks.cpg_confchg_fn = (void *)cpg_bm_confchg_fn;

    /* If CPG connection has not been created do it now.
     */
    if (!cmsg_cpg_handle)
    {
        res = _cmsg_transport_cpg_init_exe_connection (server);
        if (res < 0)
        {
            DEBUG (CMSG_ERROR, "[TRANSPORT] cpg listen init failed, result %d\n", res);
            return -1;
        }
    }

    /* Add entry into the hash table for the server to be found by cpg group name.
     */
    g_hash_table_insert (cpg_group_name_to_server_hash_table_h,
                         (gpointer)server->_transport->config.cpg.group_name.value,
                         (gpointer)server);

    DEBUG (CMSG_INFO,
           "[TRANSPORT] server added %lu to hash table\n",
           server->connection.cpg.handle);

    res = _cmsg_transport_cpg_join_group (server);

    if (res < 0)
    {
        DEBUG (CMSG_ERROR, "[TRANSPORT] cpg listen join failed, result %d\n", res);
        return -1;
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
    }

    /* Now set the cpg handle so it is useable by every server.
     */
    cmsg_cpg_handle = server->connection.cpg.handle;
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

    return 0; // Success
}


static ProtobufCMessage *
cmsg_transport_cpg_client_recv (cmsg_client *client)
{
    return NULL;
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
static  int32_t
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
               client->transport->config.cpg.group_name.value);
        return -1;
    }

    DEBUG (CMSG_INFO,
           "[TRANSPORT] cpg send message to handle  %lu\n",
           client->connection.handle);

    res = cpg_mcast_joined (client->connection.handle, CPG_TYPE_AGREED, &iov, 1);
    DEBUG (CMSG_INFO, "[TRANSPORT] cpg message sent: %i\n", res);

    /* ATL_1716_TODO - work out what to do if congested
     * If TRY_AGAIN is returned CPG is congested.
     */
    if (res == CPG_ERR_TRY_AGAIN)
    {
        DEBUG (CMSG_ERROR, "[TRANSPORT] CPG_ERR_TRY_AGAIN\n");
        return -1;
    }
    else if (res == CPG_OK)
    {
        DEBUG (CMSG_INFO, "[TRANSPORT] CPG_OK\n");
        return length;
    }

    return -1;
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
    ret = g_hash_table_remove (cpg_group_name_to_server_hash_table_h, (gpointer *) server->_transport->config.cpg.group_name.value);
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
    return server->connection.cpg.fd;
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
 * Name is a string so have to convert/typecast it to a u32.
 */
static
guint
cmsg_transport_cpg_group_hash_function (gconstpointer key)
{
    char *string = (char *)key;
    guint hash = 0;
    int i = 0;

    for (i = 0; string[i] != 0; i++)
        hash += (guint)string[i];

    return (guint)hash;
}


/**
 * Private function used by the hash table for cpg handles.
 */
static
gboolean
cmsg_transport_cpg_group_equal_function (gconstpointer a, gconstpointer b)
{
    return (strcmp ((char *)a, (char *)b) == 0);
}


void
cmsg_transport_cpg_init (cmsg_transport *transport)
{
    if (transport == NULL)
        return;

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

    if (cpg_group_name_to_server_hash_table_h == NULL)
    {
        cpg_group_name_to_server_hash_table_h = g_hash_table_new (cmsg_transport_cpg_group_hash_function,
                cmsg_transport_cpg_group_equal_function);
    }

    DEBUG (CMSG_INFO, "%s: done\n", __FUNCTION__);
}

