#include "protobuf-c-cmsg-transport.h"
#include "protobuf-c-cmsg-client.h"
#include "protobuf-c-cmsg-server.h"


GHashTable* server_hash_table_h = NULL;

//TODO from cpg-rx.x
void cpg_bm_confchg_fn (
	cpg_handle_t handle,
	struct cpg_name *group_name,
	struct cpg_address *member_list, int member_list_entries,
	struct cpg_address *left_list, int left_list_entries,
	struct cpg_address *joined_list, int joined_list_entries)
{
  DEBUG (CMSG_INFO, "[TRANSPORT] %s\n", __FUNCTION__);
}


//TODO from cpg-rx.x
void cpg_bm_deliver_fn (cpg_handle_t handle,
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
  uint8_t* buffer = 0;

  cmsg_server* server;
  cmsg_server_request server_request;

  memcpy (&header_received, msg, sizeof (cmsg_header_request));

  header_converted.method_index = cmsg_common_uint32_from_le (header_received.method_index);
  header_converted.message_length = cmsg_common_uint32_from_le (header_received.message_length);
  header_converted.request_id = header_received.request_id;

  DEBUG (CMSG_INFO, "[TRANPORT] cpg received header\n");
  cmsg_buffer_print ((void*)&header_received, sizeof (cmsg_header_request));

  DEBUG (CMSG_INFO,
         "[TRANPORT] cpg method_index   host: %d, wire: %d\n",
         header_converted.method_index, header_received.method_index);

  DEBUG (CMSG_INFO,
         "[TRANPORT] cpg message_length host: %d, wire: %d\n",
         header_converted.message_length, header_received.message_length);

  DEBUG (CMSG_INFO,
         "[TRANPORT] cpg request_id     host: %d, wire: %d\n",
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
  server = (cmsg_server*)g_hash_table_lookup (server_hash_table_h, (gconstpointer)handle);

  if (!server)
  {
    DEBUG (CMSG_ERROR, "[TRANSPORT] Server lookup failed\n");
    return;
  }

  server->server_request = &server_request;

  if (server->message_processor (server, buffer))
    DEBUG (CMSG_ERROR, "[TRANSPORT] message processing returned an error\n");
}


static int32_t
cmsg_transport_cpg_connect (cmsg_client *client)
{
  unsigned int res;

  if (!client || !client->transport || client->transport->connection_info.sockaddr.addr.group_name.value[0] == '\0')
  {
    DEBUG (CMSG_ERROR, "[TRANPORT] cpg connect sanity check failed\n");
  }
  else
  {
    DEBUG (CMSG_INFO,
           "[TRANPORT] cpg connect group name: %s\n",
           client->transport->connection_info.sockaddr.addr.group_name.value);
  }


  res = cpg_initialize (&(client->connection.handle), NULL);
  if (res != CPG_OK)
  {
    DEBUG (CMSG_ERROR, "[TRANPORT] cpg connect init failed, result %d\n", res);
    return -1;
  }

  res = cpg_join (client->connection.handle, &(client->transport->connection_info.sockaddr.addr.group_name));

  if (res != CPG_OK)
  {
    client->state = CMSG_CLIENT_STATE_FAILED;
    DEBUG (CMSG_ERROR, "[TRANPORT] cpg connect join failed, result %d\n", res);
    return -1;
  }
  else
  {
    client->state = CMSG_CLIENT_STATE_CONNECTED;
    DEBUG (CMSG_INFO,
           "[TRANSPORT] CPG connect connected, handle = %lu\n",
           (uint64_t) client->connection.handle);
  }
  return 0;
}


static int32_t
cmsg_transport_cpg_listen (cmsg_server* server)
{
  unsigned int res;
  int fd = 0;

  if (!server || !server->transport || server->transport->connection_info.sockaddr.addr.group_name.value[0] == '\0')
  {
    DEBUG (CMSG_ERROR, "[TRANPORT] cpg listen sanity check failed\n");
  }
  else
  {
    DEBUG (CMSG_INFO,
           "[TRANPORT] cpg listen group name: %s\n",
           server->transport->connection_info.sockaddr.addr.group_name.value);
  }

  server->connection.cpg.callbacks.cpg_deliver_fn = (void*)cpg_bm_deliver_fn;
  server->connection.cpg.callbacks.cpg_confchg_fn = (void*)cpg_bm_confchg_fn;

  res = cpg_initialize (&(server->connection.cpg.handle), &(server->connection.cpg.callbacks));
  if (res != CPG_OK)
  {
    DEBUG (CMSG_ERROR, "[TRANPORT] cpg listen init failed, result %d\n", res);
    return -1;
  }

  g_hash_table_insert (server_hash_table_h,
                       (gpointer)server->connection.cpg.handle,
                       (gpointer)server);

  DEBUG (CMSG_INFO,
         "[TRANPORT] cpg handle added %lu\n",
         server->connection.cpg.handle);

  DEBUG (CMSG_INFO, "[TRANPORT] cpg listen result %d\n", res);

  res = cpg_join (server->connection.cpg.handle, &(server->transport->connection_info.sockaddr.addr.group_name));

  if (res != CPG_OK)
  {
    DEBUG (CMSG_ERROR, "[TRANPORT] cpg listen join failed, result %d\n", res);
    return -1;
  }

  if (cpg_fd_get (server->connection.cpg.handle, &fd) == CPG_OK )
  {
    server->connection.cpg.fd = fd;
    DEBUG (CMSG_INFO, "[TRANPORT] cpg listen got fd: %d\n", fd);
  }
  else
  {
    server->connection.cpg.fd = 0;
    DEBUG (CMSG_ERROR, "[TRANPORT] cpg listen cannot get fd\n");
  }

  return 0;
}

static int32_t
cmsg_transport_cpg_server_recv (int32_t socket, cmsg_server* server)
{
  int ret;

  ret = cpg_dispatch (server->connection.cpg.handle, CPG_DISPATCH_ALL);

  if (ret != CPG_OK)
  {
    DEBUG (CMSG_ERROR, "[TRANPORT] cpg sev recv dispatch returned error %d\n", ret);
    return -1;
  }
}


static ProtobufCMessage*
cmsg_transport_cpg_client_recv (cmsg_client *client)
{
  return NULL;
}


static  int32_t
cmsg_transport_cpg_client_send (cmsg_client *client, void *buff, int length, int flag)
{
  struct iovec iov;
  unsigned int res;

  iov.iov_len = length;
  iov.iov_base = buff;

  DEBUG (CMSG_INFO,
         "[TRANSPORT] cpg send message to handle  %lu\n",
         client->connection.handle);

  res = cpg_mcast_joined (client->connection.handle, CPG_TYPE_AGREED, &iov, 1);
  DEBUG (CMSG_INFO, "[TRANSPORT] cpg message sent: %i\n", res);

  if (res == CPG_ERR_TRY_AGAIN)
  {
    DEBUG (CMSG_ERROR, "[TRANSPORT] CPG_ERR_TRY_AGAIN\n");
  }
  else if (res == CPG_OK)
  {
    DEBUG (CMSG_INFO, "[TRANSPORT] CPG_OK\n");
  }
  usleep (10000);

  return length;
}

static int32_t
cmsg_transport_cpg_server_send (cmsg_server *server, void *buff, int length, int flag)
{
  return 0;
}


static void
cmsg_transport_cpg_client_close (cmsg_client* client)
{
  int res;

  res = cpg_finalize (client->connection.handle);
  if (res != CPG_OK)
  {
    DEBUG (CMSG_ERROR, "[TRANSPORT] cpg close failed, result %d\n", res);
    return;
  }

  DEBUG (CMSG_INFO, "[TRANSPORT] cpg close done\n");
}

static void
cmsg_transport_cpg_server_close (cmsg_server* server)
{
  DEBUG (CMSG_INFO, "[TRANSPORT] cpg close done nothing\n");
}


static void
cmsg_transport_cpg_server_destroy (cmsg_server* server)
{
  int res;
  gboolean ret;

  ret = g_hash_table_remove (server_hash_table_h, (gpointer *) server->connection.cpg.handle);

  DEBUG (CMSG_INFO, "[TRANSPORT] cpg hash table remove, result %d\n", ret);

  res = cpg_finalize (server->connection.cpg.handle);

  if (res != CPG_OK)
  {
    DEBUG (CMSG_ERROR, "[TRANSPORT] cpg close failed, result %d\n", res);
  }

  DEBUG (CMSG_INFO, "[TRANSPORT] cpg destroy done\n");
}


static int
cmsg_transport_cpg_server_get_socket (cmsg_server* server)
{
  return server->connection.cpg.fd;
}


static int
cmsg_transport_cpg_client_get_socket (cmsg_client* client)
{
  return 0;
}

guint
cmsg_transport_hash_function(gconstpointer key)
{
  return (guint)(cpg_handle_t)key;
}

gboolean
cmsg_transport_equal_function(gconstpointer a, gconstpointer b)
{
  return (guint)(cpg_handle_t)a == (guint)(cpg_handle_t)b;
}


void
cmsg_transport_cpg_init(cmsg_transport *transport)
{
  if (transport == NULL)
    return;

  transport->connect = cmsg_transport_cpg_connect;
  transport->listen = cmsg_transport_cpg_listen;
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

  transport->server_destroy = cmsg_transport_cpg_server_destroy;

  if (server_hash_table_h == NULL)
  {
    server_hash_table_h = g_hash_table_new (cmsg_transport_hash_function,
                                            cmsg_transport_equal_function);
  }

  DEBUG (CMSG_INFO, "%s: done\n", __FUNCTION__);
}

