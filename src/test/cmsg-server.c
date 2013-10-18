#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <poll.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <linux/tipc.h>
#include <sys/un.h>

#include "generated-code/test-cmsg_impl_auto.h"

#include <google/protobuf-c/protobuf-c-cmsg.h>
#include <google/protobuf-c/protobuf-c-cmsg-server.h>

int count = 0;

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
  cmsg_server* server;
  cmsg_transport* transport = 0;
  unsigned arg_i;
  srand (time(NULL));
  int count = 0;

  int is_one_way = 0; //0: no, 1:yes

  for (arg_i = 1; arg_i < (unsigned) argc; arg_i++)
    {
      if (starts_with (argv[arg_i], "--oneway"))
        {
    	  is_one_way = 1;
    	  break;
        }
    }


  for (arg_i = 1; arg_i < (unsigned) argc; arg_i++)
    {
      if (starts_with (argv[arg_i], "--tcp="))
        {
          int port = atoi(strchr (argv[arg_i], '=') + 1);
          
          if ( is_one_way == 1)
            transport = cmsg_transport_new(CMSG_TRANSPORT_ONEWAY_TCP);
          else
            transport = cmsg_transport_new(CMSG_TRANSPORT_RPC_TCP);

          transport->config.socket.sockaddr.in.sin_addr.s_addr = htonl (INADDR_ANY);
          transport->config.socket.sockaddr.in.sin_port = htons ((unsigned short)port);

          break;
        }
      else if (starts_with (argv[arg_i], "--tipc="))
        {
          const char *name = NULL;
          name = strchr (argv[arg_i], '=') + 1;
          const char *colon = strchr (name, ':');
          char *type_name = 0;
          unsigned instance;
          unsigned type;
          if (colon == NULL)
            {
              printf("missing --tcp=PORT or --unix=PATH or --tipc=PORT:MEMBER\n");
              return 0;
            }
          type_name = CMSG_MALLOC(sizeof(char) *  (colon + 1 - name));
          memcpy (type_name, name, colon - name);
          type_name[colon - name] = 0;
          instance = atoi (colon + 1);
          type = atoi (type_name);
          CMSG_FREE(type_name);
          
          if ( is_one_way == 1)
        	transport = cmsg_transport_new(CMSG_TRANSPORT_ONEWAY_TIPC);
          else
        	transport = cmsg_transport_new(CMSG_TRANSPORT_RPC_TIPC);

          transport->config.socket.sockaddr.tipc.family = AF_TIPC;
          transport->config.socket.sockaddr.tipc.addrtype = TIPC_ADDR_NAME;
          transport->config.socket.sockaddr.tipc.addr.name.name.type = type;    //TIPC PORT
          transport->config.socket.sockaddr.tipc.addr.name.name.instance = instance;    //MEMBER ID
          transport->config.socket.sockaddr.tipc.addr.name.domain = 0;
          transport->config.socket.sockaddr.tipc.scope = TIPC_ZONE_SCOPE;

          break;
        }
      else if (starts_with (argv[arg_i], "--cpg"))
        {
    	  transport = cmsg_transport_new (CMSG_TRANSPORT_CPG);
          strcpy (transport->config.cpg.group_name.value, "cpg_bm");
          transport->config.cpg.group_name.length = 6;
          break;
        }
      else if (starts_with (argv[arg_i], "--broadcast"))
      {
          int my_id = 4; //Stack member id
          int stack_tipc_port = 9500; //Stack topology sending port
          transport = cmsg_transport_new (CMSG_TRANSPORT_BROADCAST);

          transport->config.socket.sockaddr.tipc.addrtype = TIPC_ADDR_NAMESEQ;
          transport->config.socket.sockaddr.tipc.scope = TIPC_CLUSTER_SCOPE;
          transport->config.socket.sockaddr.tipc.addr.nameseq.type = stack_tipc_port;
          transport->config.socket.sockaddr.tipc.addr.nameseq.lower = my_id;
          transport->config.socket.sockaddr.tipc.addr.nameseq.upper = my_id;
          break;
      }
      else
        {
          printf("missing --tcp=PORT or --unix=PATH or --tipc=PORT:MEMBER --cpg --oneway\n");
        }
    }

  if (transport == NULL)
    {
      printf("missing --tcp=PORT or --unix=PATH or --tipc=PORT:MEMBER\n");
      return 0;
    }

  server = cmsg_server_new(transport, CMSG_SERVICE(my_package, my_service));
  if(!server)
    {
      printf ("[torusserver] server could not initialize\n");
      exit(0);
    }

  //poll test
  int ret;
  int fd = 0;
  int accept_fd = 0;
  struct pollfd poll_list[1];

  fd = cmsg_server_get_socket(server);
  if (fd)
      printf ("[torusserver] Initialized rpc successfully (socket %d)\n", fd);
  else
      printf ("[torusserver] Initialized rpc failed (socket %d)\n", fd);

  poll_list[0].fd = fd;
  poll_list[0].events = POLLIN;
  
  while (1)
    {
      count++;
      poll_list[0].fd = fd;
      poll_list[0].events = POLLIN;
      ret = poll(poll_list, (unsigned long)1, 30000);
      if(ret < 0)
        {
          printf("[torusserver] Error while polling\n");
          return 0;
        }
      else
        {
          if((poll_list[0].revents & POLLIN) == POLLIN)
            {
              printf("[torusserver] calling cmsg_server_receive\n");
              accept_fd = cmsg_server_accept(server, fd);
              cmsg_server_receive(server, accept_fd);
              server->_transport->server_close(server);
            }
        }
//      if(count >= 1)
//        {
//          printf("[torusserver] exit\n");
//          return 0;
//        }
    }
  
  cmsg_server_destroy(server);
  cmsg_transport_destroy(transport);

  return 0;
}

// other impl functions stub

int my_package_my_notification_register_impl_notification_register(const void *service, int32_t subscriber_address, int32_t subscriber_port, int32_t notification_type)
{
	return 0;
}

//int my_package_my_notification_impl_poe_notify_port_status(const void *service, uint32_t port, const My_Package_PortEventS *eventptr)
//{
//	return 0;
//}

int my_package_my_notification_impl_poe_notify_psu_event(const void *service, int32_t membid, int32_t event)
{
	return 0;
}
