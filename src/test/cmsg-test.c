#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>

#include <google/protobuf-c/protobuf-c-cmsg.h>
#include <google/protobuf-c/protobuf-c-cmsg-client.h>

#include "generated-code/test-cmsg_api_auto.h"
#include "generated-code/test-cmsg_impl_auto.h"

#define CMSG_TEST_TIPC_SCOPE TIPC_NODE_SCOPE

int run_thread_run = 1;

struct thread_parameter
{
    int transport_type;
    int is_one_way;
    int queue;
};

static protobuf_c_boolean
starts_with (const char *str, const char *prefix)
{
    return memcmp (str, prefix, strlen (prefix)) == 0;
}

void
cmsg_test_impl_ping (const void *service, int32_t random, int32_t randomm)
{
    int code;
    int value;

    code = 0;
    value = rand () % 100;

    printf ("[IMPL]: %s : send code=%d, value=%d\n", __func__, code, value);

    cmsg_test_server_pingSend (service, code, value);
}

void
cmsg_test_impl_set_priority (const void *service, int32_t port, int32_t priority,
                             cmsg_some_numbers count)
{
    static int status = 0;

    status++;

    printf ("[IMPL]: %s : port=%d, priority=%d, enum=%d --> send status=%d\n", __func__,
            port, priority, count, status);

    cmsg_test_server_set_prioritySend (service, status);
}

int
run_sub (int transport_type, int queue)
{
    printf ("[SUBSCRIBER] starting run_sub thread\n");

    cmsg_sub *sub = 0;
    cmsg_transport *transport_register = 0;
    cmsg_transport *transport_notification = 0;


    if (transport_type == 1)
    {
        transport_register = cmsg_transport_new (CMSG_TRANSPORT_RPC_TCP);
        transport_register->config.socket.sockaddr.in.sin_addr.s_addr = htonl (0x7f000001);
        transport_register->config.socket.sockaddr.in.sin_port =
            htons ((unsigned short) 17888);

        transport_notification = cmsg_transport_new (CMSG_TRANSPORT_ONEWAY_TCP);
        transport_notification->config.socket.sockaddr.in.sin_addr.s_addr =
            htonl (0x7f000001);
        transport_notification->config.socket.sockaddr.in.sin_port =
            htons ((unsigned short) 17889);
    }
    else if (transport_type == 2)
    {
        transport_register = cmsg_transport_new (CMSG_TRANSPORT_RPC_TIPC);
        transport_register->config.socket.sockaddr.tipc.family = AF_TIPC;
        transport_register->config.socket.sockaddr.tipc.addrtype = TIPC_ADDR_NAME;
        transport_register->config.socket.sockaddr.tipc.addr.name.name.type = 17888;    //TIPC PORT
        transport_register->config.socket.sockaddr.tipc.addr.name.name.instance = 1;    //MEMBER ID
        transport_register->config.socket.sockaddr.tipc.addr.name.domain = 0;
        transport_register->config.socket.sockaddr.tipc.scope = CMSG_TEST_TIPC_SCOPE;

        transport_notification = cmsg_transport_new (CMSG_TRANSPORT_ONEWAY_TIPC);
        transport_notification->config.socket.sockaddr.tipc.family = AF_TIPC;
        transport_notification->config.socket.sockaddr.tipc.addrtype = TIPC_ADDR_NAME;
        transport_notification->config.socket.sockaddr.tipc.addr.name.name.type = 17889;    //TIPC PORT
        transport_notification->config.socket.sockaddr.tipc.addr.name.name.instance = 1;    //MEMBER ID
        transport_notification->config.socket.sockaddr.tipc.addr.name.domain = 0;
        transport_notification->config.socket.sockaddr.tipc.scope = CMSG_TEST_TIPC_SCOPE;
    }

    sub = cmsg_sub_new (transport_notification, CMSG_SERVICE (cmsg, test));

    cmsg_sub_subscribe (sub, transport_register, "set_priority");

    int fd = cmsg_sub_get_server_socket (sub);
    int fd_max = fd + 1;

    fd_set readfds;
    FD_ZERO (&readfds);
    FD_SET (fd, &readfds);

    while (run_thread_run)
        cmsg_sub_server_receive_poll (sub, 1000, &readfds, &fd_max);

    printf ("[SUBSCRIBER] subscriber thread stopped\n");

    cmsg_sub_unsubscribe (sub, transport_register, "set_priority");

    cmsg_sub_destroy (sub);
    cmsg_transport_destroy (transport_register);
    cmsg_transport_destroy (transport_notification);

    return 0;
}

void *
run_pub (void *arg)
{
    int transport_type = ((struct thread_parameter *) arg)->transport_type;
    int queue = ((struct thread_parameter *) arg)->queue;

    cmsg_pub *pub = 0;
    cmsg_transport *transport_register = 0;
    int count = 0;
    int count_stop = 10;
    int count_wait_for_unsubscribe = 0;
    int count_wait_for_unsubscribe_stop = 10;

    if (transport_type == 1)
    {
        transport_register = cmsg_transport_new (CMSG_TRANSPORT_RPC_TCP);
        transport_register->config.socket.sockaddr.in.sin_addr.s_addr = htonl (INADDR_ANY);
        transport_register->config.socket.sockaddr.in.sin_port =
            htons ((unsigned short) 17888);
    }
    else if (transport_type == 2)
    {
        transport_register = cmsg_transport_new (CMSG_TRANSPORT_RPC_TIPC);
        transport_register->config.socket.sockaddr.tipc.family = AF_TIPC;
        transport_register->config.socket.sockaddr.tipc.addrtype = TIPC_ADDR_NAME;
        transport_register->config.socket.sockaddr.tipc.addr.name.name.type = 17888;    //TIPC PORT
        transport_register->config.socket.sockaddr.tipc.addr.name.name.instance = 1;    //MEMBER ID
        transport_register->config.socket.sockaddr.tipc.addr.name.domain = 0;
        transport_register->config.socket.sockaddr.tipc.scope = CMSG_TEST_TIPC_SCOPE;
    }


    pub = cmsg_pub_new (transport_register, CMSG_DESCRIPTOR (cmsg, test));

    cmsg_pub_queue_filter_show (pub);


    if (queue)
    {
        cmsg_pub_queue_enable (pub);
        //cmsg_pub_queue_filter_set_all (pub, CMSG_QUEUE_FILTER_QUEUE);
        //cmsg_pub_queue_filter_set (pub, "poe_notify_port_status", CMSG_QUEUE_FILTER_PROCESS);
    }

    cmsg_pub_queue_filter_show (pub);


    int fd = cmsg_pub_get_server_socket (pub);
    int fd_max = fd + 1;
    if (!fd)
        printf ("initialized rpc failed (socket %d)\n", fd);

    fd_set readfds;
    FD_ZERO (&readfds);
    FD_SET (fd, &readfds);

    while (1)
    {
        cmsg_publisher_receive_poll (pub, 1000, &readfds, &fd_max);

        if (count >= count_stop)
        {
            printf ("[PUBLISHER] waiting for subscribers to unsubscribe\n");

            cmsg_pub_queue_process_all (pub);

            printf ("[PUBLISHER] count_stop reached, destroying publisher\n");
            printf ("[PUBLISHER] end queue length: %d\n", cmsg_pub_queue_get_length (pub));

            run_thread_run = 0;

            count_wait_for_unsubscribe++;

            if (pub->subscriber_count == 0 ||
                count_wait_for_unsubscribe >= count_wait_for_unsubscribe_stop)
            {
                goto CLEAN_EXIT;
            }
        }

        if (pub->subscriber_count > 0)
        {
            int ret;
            int port;
            int priority;
            static int result_status;
            result_status++;

            port = rand () % 100;;
            priority = rand () % 100;;

            printf ("[PUBLISHER] calling set priority: port=%d, priority=%d, enum=%d\n",
                    port, priority, CMSG_FOUR);

            ret = cmsg_test_api_set_priority ((cmsg_client *) pub,
                                              port, priority, CMSG_FOUR, &result_status);

            printf ("[PUBLISHER] calling set priority done: ret=%d, result_status=%d\n",
                    ret, result_status);

            printf ("[PUBLISHER] queue length: %d\n", cmsg_pub_queue_get_length (pub));

            count++;
        }
    }

CLEAN_EXIT:
    cmsg_pub_destroy (pub);
    cmsg_transport_destroy (transport_register);

    printf ("[PUBLISHER] publisher and transport destroyed\n");
    printf ("[PUBLISHER] thread ended\n");
    return 0;
}

void *
run_server (void *arg)
{
    printf ("[SERVER] starting run_server thread\n");

    int transport_type = ((struct thread_parameter *) arg)->transport_type;
    int is_one_way = ((struct thread_parameter *) arg)->is_one_way;

    cmsg_server *server;
    cmsg_transport *transport = 0;

    if (transport_type == 1)
    {
        if (is_one_way == 1)
            transport = cmsg_transport_new (CMSG_TRANSPORT_ONEWAY_TCP);
        else
            transport = cmsg_transport_new (CMSG_TRANSPORT_RPC_TCP);

        transport->config.socket.sockaddr.in.sin_addr.s_addr = htonl (INADDR_ANY);
        transport->config.socket.sockaddr.in.sin_port = htons ((unsigned short) 18888);
    }
    else if (transport_type == 2)
    {
        if (is_one_way == 1)
            transport = cmsg_transport_new (CMSG_TRANSPORT_ONEWAY_TIPC);
        else
            transport = cmsg_transport_new (CMSG_TRANSPORT_RPC_TIPC);

        transport->config.socket.sockaddr.tipc.family = AF_TIPC;
        transport->config.socket.sockaddr.tipc.addrtype = TIPC_ADDR_NAME;
        transport->config.socket.sockaddr.tipc.addr.name.name.type = 18888; //TIPC PORT
        transport->config.socket.sockaddr.tipc.addr.name.name.instance = 1; //MEMBER ID
        transport->config.socket.sockaddr.tipc.addr.name.domain = 0;
        transport->config.socket.sockaddr.tipc.scope = CMSG_TEST_TIPC_SCOPE;
    }
    else if (transport_type == 3)
    {
#ifdef HAVE_VCSTACK
        transport = cmsg_transport_new (CMSG_TRANSPORT_CPG);
        strcpy (transport->config.cpg.group_name.value, "cpg_bm");
        transport->config.cpg.group_name.length = 6;
#endif
    }
    else if (transport_type == 4)
    {
        int my_id = 4;              //Stack member id
        int stack_tipc_port = 9500; //Stack topology sending port

        transport = cmsg_transport_new (CMSG_TRANSPORT_BROADCAST);

        transport->config.socket.sockaddr.tipc.addrtype = TIPC_ADDR_NAMESEQ;
        transport->config.socket.sockaddr.tipc.scope = TIPC_CLUSTER_SCOPE;
        transport->config.socket.sockaddr.tipc.addr.nameseq.type = stack_tipc_port;
        transport->config.socket.sockaddr.tipc.addr.nameseq.lower = my_id;
        transport->config.socket.sockaddr.tipc.addr.nameseq.upper = my_id;
    }


    server = cmsg_server_new (transport, CMSG_SERVICE (cmsg, test));

    int fd = cmsg_server_get_socket (server);
    int fd_max = fd + 1;

    fd_set readfds;
    FD_ZERO (&readfds);
    FD_SET (fd, &readfds);

    while (run_thread_run)
    {
        cmsg_server_receive_poll (server, 1000, &readfds, &fd_max);
    }

    printf ("[SERVER] stopping thread\n");

    cmsg_server_destroy (server);
    cmsg_transport_destroy (transport);

    return 0;
}

int
run_client (int transport_type, int is_one_way, int queue)
{
    cmsg_client *client = 0;
    cmsg_transport *transport = 0;
#ifdef HAVE_VCSTACK
    cmsg_server *cpg_server = 0;
#endif

    if (transport_type == 1)
    {
        if (is_one_way == 1)
            transport = cmsg_transport_new (CMSG_TRANSPORT_ONEWAY_TCP);
        else
            transport = cmsg_transport_new (CMSG_TRANSPORT_RPC_TCP);

        transport->config.socket.sockaddr.in.sin_addr.s_addr = htonl (0x7f000001);
        transport->config.socket.sockaddr.in.sin_port = htons ((unsigned short) 18888);
    }
    else if (transport_type == 2)
    {

        if (is_one_way == 1)
            transport = cmsg_transport_new (CMSG_TRANSPORT_ONEWAY_TIPC);
        else
            transport = cmsg_transport_new (CMSG_TRANSPORT_RPC_TIPC);

        transport->config.socket.sockaddr.tipc.family = AF_TIPC;
        transport->config.socket.sockaddr.tipc.addrtype = TIPC_ADDR_NAME;
        transport->config.socket.sockaddr.tipc.addr.name.name.type = 18888; //TIPC PORT
        transport->config.socket.sockaddr.tipc.addr.name.name.instance = 1; //MEMBER ID
        transport->config.socket.sockaddr.tipc.addr.name.domain = 0;
        transport->config.socket.sockaddr.tipc.scope = CMSG_TEST_TIPC_SCOPE;
    }
    else if (transport_type == 3)
    {
#ifdef HAVE_VCSTACK
        cmsg_transport *server_transport;

        transport = cmsg_transport_new (CMSG_TRANSPORT_CPG);
        strcpy (transport->config.cpg.group_name.value, "cpg_bm");
        transport->config.cpg.group_name.length = 6;

        /* create server to create connection to the executable
         */
        server_transport = cmsg_transport_new (CMSG_TRANSPORT_CPG);
        strcpy (server_transport->config.cpg.group_name.value, "cpg_bm");
        server_transport->config.cpg.group_name.length = 6;
        cpg_server =
            cmsg_server_new (server_transport, CMSG_SERVICE (my_package, my_service));
#endif
    }
    else if (transport_type == 4)
    {
        int stack_tipc_port = 9500; //Stack topology sending port
        transport = cmsg_transport_new (CMSG_TRANSPORT_BROADCAST);

        transport->config.socket.sockaddr.tipc.addrtype = TIPC_ADDR_MCAST;
        transport->config.socket.sockaddr.tipc.addr.nameseq.type = stack_tipc_port;
        transport->config.socket.sockaddr.tipc.addr.nameseq.lower = 1;
        transport->config.socket.sockaddr.tipc.addr.nameseq.upper = 8;
    }

    client = cmsg_client_new (transport, CMSG_DESCRIPTOR (cmsg, test));

    if (queue)
    {
        cmsg_client_queue_enable (client);
        //cmsg_pub_queue_filter_set_all (pub, CMSG_QUEUE_FILTER_QUEUE);
        //cmsg_pub_queue_filter_set (pub, "poe_notify_port_status", CMSG_QUEUE_FILTER_PROCESS);
    }


    {
        int ret;
        int l = 0;
        int port;
        int priority;
        static int result_status;
        result_status++;
        for (l = 0; l < 10; l++)
        {
            port = rand () % 100;;
            priority = rand () % 100;;

            printf ("[CLIENT] calling set priority: port=%d, priority=%d, enum=%d\n", port,
                    priority, CMSG_FOUR);
            ret = cmsg_test_api_set_priority (client, port, priority, CMSG_FOUR,
                                              &result_status);
            printf ("[CLIENT] calling set priority done: ret=%d, result_status=%d\n", ret,
                    result_status);

            printf ("[CLIENT] queue length: %d\n", cmsg_client_queue_get_length (client));

            sleep (1);
        }
    }

    cmsg_client_queue_process_all (client);

    cmsg_client_destroy (client);
    cmsg_transport_destroy (transport);

    return 0;
}


int
main (int argc, char *argv[])
{
    srand (time (NULL));

    pthread_t thread;
    int mode = 0;
    int transport_type = 0; // tcp:1, tipc:2, cpg:3, tipc broadcast:4
    int is_one_way = 0;     //0: no, 1:yes
    int queue = 0;          //0: no, 1:yes
    int test = 0;           //0: no, 1:yes
    int i;
    struct thread_parameter thread_par;


    for (i = 1; i < (unsigned) argc; i++)
    {
        if (starts_with (argv[i], "--cs"))
            mode = 1;

        if (starts_with (argv[i], "--ps"))
            mode = 2;

        if (starts_with (argv[i], "--tcp"))
            transport_type = 1;

        if (starts_with (argv[i], "--tipc"))
            transport_type = 2;

#ifdef HAVE_VCSTACK
        if (starts_with (argv[i], "--cpg"))
            transport_type = 3;

        if (starts_with (argv[i], "--tipc-broadcast"))
            transport_type = 4;
#endif

        if (starts_with (argv[i], "--oneway"))
            is_one_way = 1;

        if (starts_with (argv[i], "--queue"))
            queue = 1;

        if (starts_with (argv[i], "--test"))
            test = 1;
    }

    if ((transport_type == 0 || mode == 0) && (test == 0))
    {
        printf ("cmsg-test program:\n");
        printf ("run all tests                          --test\n");
        printf ("client/server                          --cs\n");
        printf ("publisher/subscriber                   --ps\n");
        printf ("transports for client server:\n");
        printf ("                                       --tcp\n");
        printf ("                                       --tipc\n");
#ifdef HAVE_VCSTACK
        printf ("                                       --cpg \n");
        printf ("                                       --tipc-broadcast\n");
#endif
        printf ("transport options for client/server:\n");
        printf ("                                       --oneway\n");
        printf ("                                       --queue\n");
        printf ("transports for publisher/subscriber:\n");
        printf ("                                       --tcp\n");
        printf ("                                       --tipc\n");
        printf ("transport options for publisher/subscriber:\n");
        printf ("                                       --queue\n");
        exit (0);
    }

    thread_par.transport_type = transport_type;
    thread_par.is_one_way = is_one_way;
    thread_par.queue = queue;

    if (mode == 1)
    {
        pthread_create (&thread, NULL, &run_server, (void *) &thread_par);
        sleep (1);
        run_client (transport_type, is_one_way, queue);
        run_thread_run = 0;
        pthread_join (thread, NULL);
    }
    else if (mode == 2)
    {
        pthread_create (&thread, NULL, &run_pub, (void *) &thread_par);
        sleep (1);
        run_sub (transport_type, queue);
        run_thread_run = 0;
        pthread_join (thread, NULL);
    }
    else if (test == 1)
    {
        printf ("publisher\n");
        {
            cmsg_pub *pub = 0;
            cmsg_transport *transport_register = 0;
            transport_register = cmsg_transport_new (CMSG_TRANSPORT_RPC_TCP);
            transport_register->config.socket.sockaddr.in.sin_addr.s_addr =
                htonl (INADDR_ANY);
            transport_register->config.socket.sockaddr.in.sin_port =
                htons ((unsigned short) 17888);

            pub = cmsg_pub_new (transport_register, CMSG_DESCRIPTOR (cmsg, test));

            cmsg_pub_destroy (pub);
            cmsg_transport_destroy (transport_register);
        }

        printf ("server\n");
        {
            cmsg_server *server;
            cmsg_transport *transport = 0;

            transport = cmsg_transport_new (CMSG_TRANSPORT_RPC_TCP);
            transport->config.socket.sockaddr.in.sin_addr.s_addr = htonl (INADDR_ANY);
            transport->config.socket.sockaddr.in.sin_port = htons ((unsigned short) 18888);

            server = cmsg_server_new (transport, CMSG_SERVICE (cmsg, test));
            cmsg_server_destroy (server);
            cmsg_transport_destroy (transport);
        }

        printf ("client\n");
        {
            cmsg_client *client = 0;
            cmsg_transport *transport = 0;
            transport = cmsg_transport_new (CMSG_TRANSPORT_RPC_TCP);

            transport->config.socket.sockaddr.in.sin_addr.s_addr = htonl (0x7f000001);
            transport->config.socket.sockaddr.in.sin_port = htons ((unsigned short) 18888);

            client = cmsg_client_new (transport, CMSG_DESCRIPTOR (cmsg, test));
            cmsg_client_destroy (client);
            cmsg_transport_destroy (transport);

        }

        printf ("subscriber\n");
        {
            cmsg_sub *sub = 0;
            cmsg_transport *transport_register = 0;
            cmsg_transport *transport_notification = 0;


            transport_register = cmsg_transport_new (CMSG_TRANSPORT_RPC_TCP);
            transport_register->config.socket.sockaddr.in.sin_addr.s_addr =
                htonl (0x7f000001);
            transport_register->config.socket.sockaddr.in.sin_port =
                htons ((unsigned short) 17888);

            transport_notification = cmsg_transport_new (CMSG_TRANSPORT_ONEWAY_TCP);
            transport_notification->config.socket.sockaddr.in.sin_addr.s_addr =
                htonl (0x7f000001);
            transport_notification->config.socket.sockaddr.in.sin_port =
                htons ((unsigned short) 17889);

            sub = cmsg_sub_new (transport_notification, CMSG_SERVICE (cmsg, test));

            cmsg_sub_destroy (sub);
            cmsg_transport_destroy (transport_register);
            cmsg_transport_destroy (transport_notification);
        }

    }

    return 0;
}
