#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "generated-code/test-cmsg_api_auto.h"
#include "generated-code/test-cmsg_types_auto.h"

#include <google/protobuf-c/protobuf-c-cmsg.h>
#include <google/protobuf-c/protobuf-c-cmsg-client.h>


static protobuf_c_boolean starts_with (const char *str, const char *prefix)
{
  return memcmp (str, prefix, strlen (prefix)) == 0;
}

static void
handle_query_response (const my_package_PingResponse *response,
                       void                            *closure_data)
{
  if (response == NULL)
    printf("[CLIENT] Error processing request\n");
  else
    {
      printf("[CLIENT] response from server\n");
      printf("[CLIENT]  response->return_code: %d\n", response->return_code);
      printf("[CLIENT]  response->random: %d\n", response->random);
    }

  *(protobuf_c_boolean*) closure_data = 1;
}

static void handle_set_priority_response (const my_package_poe_send_status *response, void *closure_data)
{
  printf("[CLIENT] handle_set_priority_response\n");
  if(response)
    {
      printf("[CLIENT] response == 1\n");
      ((my_package_poe_send_status *)closure_data)->status = response->status;
      printf("[CLIENT] response->status=%d\n", response->status);
    }
  else
    {
      printf("[CLIENT] response == 0\n");
    }
}

int main(int argc, char**argv)
{
  srand (time(NULL));
  cmsg_client* torusclient = 0;
  cmsg_transport* transport = 0;
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

    transport->sockaddr.in.sin_addr.s_addr = htonl(0x7f000001);
    transport->sockaddr.in.sin_port = htons((unsigned short)18888);
  }
  else if (is_tcp_tipc_cpg == 2)
  {

	if (is_one_way == 1)
      transport = cmsg_transport_new(CMSG_TRANSPORT_ONEWAY_TIPC);
	else
	  transport = cmsg_transport_new(CMSG_TRANSPORT_RPC_TIPC);

    transport->sockaddr.tipc.family = AF_TIPC;
    transport->sockaddr.tipc.addrtype = TIPC_ADDR_NAME;
    transport->sockaddr.tipc.addr.name.name.type = 18888;    //TIPC PORT
    transport->sockaddr.tipc.addr.name.name.instance = 1;    //MEMBER ID
    transport->sockaddr.tipc.addr.name.domain = 0;
    transport->sockaddr.tipc.scope = TIPC_ZONE_SCOPE;
  }
  else if (is_tcp_tipc_cpg == 3)
  {
	  transport = cmsg_transport_new (CMSG_TRANSPORT_CPG);
	  strcpy (transport->sockaddr.group_name.value, "cpg_bm");
	  transport->sockaddr.group_name.length = 6;
  }
  else if (is_tcp_tipc_cpg == 4)
  {
      int my_id = 1; //Stack member id
      int stack_tipc_port = 9500; //Stack topology sending port
      transport = cmsg_transport_new (CMSG_TRANSPORT_BROADCAST);

      transport->sockaddr.tipc.addrtype = TIPC_ADDR_MCAST;
      transport->sockaddr.tipc.addr.nameseq.type = stack_tipc_port;
      transport->sockaddr.tipc.addr.nameseq.lower = 1;
      transport->sockaddr.tipc.addr.nameseq.upper = 8;
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

      printf("[CLIENT] calling set priority: port=%d, priority=%d\n", port, priority);
      ret = my_package_my_service_api_SetPriority((ProtobufC_RPC_Client *)torusclient, port, priority, &result_status);
      printf("[CLIENT] calling set priority done: ret=%d, result_status=%d\n", ret, result_status);

      sleep(1);
    }
  }

  cmsg_client_destroy(torusclient);
  cmsg_transport_destroy(transport);
  return 0;
}

