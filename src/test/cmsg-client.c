#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include <google/protobuf-c/protobuf-c-cmsg.h>
#include <google/protobuf-c/protobuf-c-cmsg-client.h>

#include "generated-code/test-cmsg_api_auto.h"
#include "generated-code/test-cmsg_impl_auto.h"

static protobuf_c_boolean starts_with (const char *str, const char *prefix)
{
  return memcmp (str, prefix, strlen (prefix)) == 0;
}

void
my_package_my_service_impl_Ping(const void *service, int32_t random, int32_t randomm)
{
  int code;
  int value;

  code = 0;
  value = rand() % 100;

  printf("[SERVER]: %s : send code=%d, value=%d\n", __func__, code, value);

  my_package_my_service_server_PingSend(service, code, value);
}

void
my_package_my_service_impl_SetPriority(const void *service, int32_t port, int32_t priority, my_package_some_numbers count)
{
  static int status = 0;

  status++;

  printf("[SERVER]: %s : port=%d, priority=%d, enum=%d --> send status=%d\n", __func__, port, priority, count, status);

  my_package_my_service_server_SetPrioritySend(service, status);
}

int main(int argc, char**argv)
{
  srand (time(NULL));
  cmsg_client* torusclient = 0;
  cmsg_transport* transport = 0;
#ifdef HAVE_VCSTACK
  cmsg_server* cpg_server = 0;
#endif
  int is_tcp_tipc_cpg = 0;// tcp:1, tipc:2, cpg:3, tipc broadcast:4
  int is_one_way = 0; //0: no, 1:yes
  int arg_i;

  for (arg_i = 1; arg_i < (unsigned) argc; arg_i++)
    {
      if (starts_with (argv[arg_i], "--tcp"))
    	  is_tcp_tipc_cpg = 1;

      if (starts_with (argv[arg_i], "--tipc"))
    	  is_tcp_tipc_cpg = 2;

      if (starts_with (argv[arg_i], "--cpg"))
    	  is_tcp_tipc_cpg = 3;

      if (starts_with (argv[arg_i], "--broadcast"))
    	  is_tcp_tipc_cpg = 4;

      if (starts_with (argv[arg_i], "--oneway"))
    	  is_one_way = 1;
    }
  
  if (is_tcp_tipc_cpg == 1)
  {
	if (is_one_way == 1)
	  transport = cmsg_transport_new(CMSG_TRANSPORT_ONEWAY_TCP);
    else
      transport = cmsg_transport_new(CMSG_TRANSPORT_RPC_TCP);

    transport->config.socket.sockaddr.in.sin_addr.s_addr = htonl (0x7f000001);
    transport->config.socket.sockaddr.in.sin_port = htons ((unsigned short)18888);
  }
  else if (is_tcp_tipc_cpg == 2)
  {

	if (is_one_way == 1)
      transport = cmsg_transport_new(CMSG_TRANSPORT_ONEWAY_TIPC);
	else
	  transport = cmsg_transport_new(CMSG_TRANSPORT_RPC_TIPC);

    transport->config.socket.sockaddr.tipc.family = AF_TIPC;
    transport->config.socket.sockaddr.tipc.addrtype = TIPC_ADDR_NAME;
    transport->config.socket.sockaddr.tipc.addr.name.name.type = 18888;    //TIPC PORT
    transport->config.socket.sockaddr.tipc.addr.name.name.instance = 1;    //MEMBER ID
    transport->config.socket.sockaddr.tipc.addr.name.domain = 0;
    transport->config.socket.sockaddr.tipc.scope = TIPC_ZONE_SCOPE;
  }
  else if (is_tcp_tipc_cpg == 3)
  {
#ifdef HAVE_VCSTACK
    cmsg_transport * server_transport;

    transport = cmsg_transport_new (CMSG_TRANSPORT_CPG);
    strcpy (transport->config.cpg.group_name.value, "cpg_bm");
    transport->config.cpg.group_name.length = 6;

    /* create server to create connection to the executable
    */
    server_transport = cmsg_transport_new (CMSG_TRANSPORT_CPG);
    strcpy (server_transport->config.cpg.group_name.value, "cpg_bm");
    server_transport->config.cpg.group_name.length = 6;
    cpg_server = cmsg_server_new (server_transport, CMSG_SERVICE(my_package, my_service));
#endif
  }
  else if (is_tcp_tipc_cpg == 4)
  {
      int stack_tipc_port = 9500; //Stack topology sending port
      transport = cmsg_transport_new (CMSG_TRANSPORT_BROADCAST);

      transport->config.socket.sockaddr.tipc.addrtype = TIPC_ADDR_MCAST;
      transport->config.socket.sockaddr.tipc.addr.nameseq.type = stack_tipc_port;
      transport->config.socket.sockaddr.tipc.addr.nameseq.lower = 1;
      transport->config.socket.sockaddr.tipc.addr.nameseq.upper = 8;
  }
  else
  {
	  printf("\n cmsg-cliet --tcp --tipc --cpg \n");
  }

  torusclient = cmsg_client_new(transport, CMSG_DESCRIPTOR(my_package, my_service));
  if(!torusclient)
    {
      printf("[CLIENT] client could not connect exiting\n");
      return 0;
    }
    

  printf("[CLIENT] sending request to server\n");

  {
  int ret;
  int l = 0;
  int port;
  int priority;
  static int result_status;
  result_status++;
  for (l=0;l<1;l++)
    {
      port = rand() % 100;;
      priority = rand() % 100;;

      printf("[CLIENT] calling set priority: port=%d, priority=%d, enum=%d\n", port, priority, MY_PACKAGE_FOUR);
      ret = my_package_my_service_api_SetPriority(torusclient, port, priority, MY_PACKAGE_FOUR, &result_status);
      printf("[CLIENT] calling set priority done: ret=%d, result_status=%d\n", ret, result_status);

      sleep(1);
    }
  }

  cmsg_client_destroy(torusclient);
  cmsg_transport_destroy(transport);
  return 0;
}

