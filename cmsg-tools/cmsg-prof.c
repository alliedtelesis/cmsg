/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#include <google/protobuf-c/protobuf-c-cmsg.h>
#include <google/protobuf-c/protobuf-c-cmsg-client.h>

#include "cmsg-prof_api_auto.h"
#include "cmsg-prof_impl_auto.h"

#define TEST_REPEAT 100         //repeat per step -> average
#define SIZE_STEPS 128          //this * biggest_type_size -> biggest size
#define SIZE_STEP_INCREASE 128  //steps for smaller datatype
#define STRING_SIZE 128

int i, j, s = 0;
char file_name_prefix[STRING_SIZE];

//biggest_type_size this size is calculated from the largest message times the SIZE_STEPS
//it represents the message size when the tests stops. Used to get the end size for smaller
//datatypes
int biggest_type_size = (sizeof (int8_t) + sizeof (uint8_t) +
                         sizeof (int16_t) + sizeof (uint16_t) +
                         sizeof (int32_t) + sizeof (int32_t) +
                         sizeof (int32_t) + sizeof (uint32_t) +
                         sizeof (uint32_t) + sizeof (int64_t) +
                         sizeof (int64_t) + sizeof (int64_t) +
                         sizeof (uint64_t) + sizeof (uint64_t) +
                         sizeof (float) + sizeof (double) +
                         sizeof (cmsg_bool_t) + sizeof (char *) +
                         (sizeof (char) * STRING_SIZE) * SIZE_STEPS);

//macro to implement api call test
#define CALL_PROF_REPEATED(type,ftype,max_val) \
    printf ("prof_test_api_%s_repeated\n", #ftype); \
    for (s = 1; s <= biggest_type_size / sizeof (type); s += SIZE_STEP_INCREASE) \
    { \
        printf ("%d of %ld\n", s, (long unsigned int)biggest_type_size / sizeof (type)); \
        for (i = 0; i < TEST_REPEAT; i++) \
        { \
            CMSG_PROF_ENABLE(&client->prof); \
            CMSG_PROF_TIME_LOG_START(&client->prof, log_file); \
            size_t n_field_1 = s; \
            type *field_1 = malloc (sizeof (type) * s); \
            for (j = 0; j < s; j++) \
                field_1[j] = max_val; \
            prof_test_api_##ftype##_repeated (client, n_field_1, field_1, &result_field_1); \
            free (field_1); \
            CMSG_PROF_TIME_LOG_STOP(&client->prof, #ftype, (sizeof (type) * s)); \
            CMSG_PROF_DISABLE(&client->prof); \
        } \
    }

//macro to implement impltations
#define IMPLEMENT_PROF_REPEATED(type,ftype,max_val) \
    void prof_test_impl_##ftype##_repeated (const void *service, \
                                            size_t n_field_1, type field_1) \
    { \
        prof_test_server_##ftype##_repeatedSend (service, max_val); \
    }


void
handler (int s)
{
    printf ("Caught signal %d\n", s);
    kill (0, SIGTERM);
}

void
client_test_api_all (cmsg_client *client, char *log_file)
{
    int32_t result_field_1 = 0;

    printf ("prof_test_api_all\n");
    for (i = 0; i < TEST_REPEAT; i++)
    {
        CMSG_PROF_ENABLE (&client->prof);
        CMSG_PROF_TIME_LOG_START (&client->prof, log_file);

        prof_test_api_all (client,
                           0xff, 0xff, 0xffff, 0xffff, 0xffffffff, 0xffffffff, 0xffffffff,
                           0xffffffff, 0xffffffff, 0xffffffffffffffff, 0xffffffffffffffff,
                           0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff,
                           FLT_MAX, DBL_MAX, 1, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789",
                           &result_field_1);

        CMSG_PROF_TIME_LOG_STOP (&client->prof,
                                 "all",
                                 sizeof (int8_t) + sizeof (uint8_t) +
                                 sizeof (int16_t) + sizeof (uint16_t) +
                                 sizeof (int32_t) + sizeof (int32_t) +
                                 sizeof (int32_t) + sizeof (uint32_t) +
                                 sizeof (uint32_t) + sizeof (int64_t) +
                                 sizeof (int64_t) + sizeof (int64_t) +
                                 sizeof (uint64_t) + sizeof (uint64_t) +
                                 sizeof (float) + sizeof (double) +
                                 sizeof (cmsg_bool_t) + sizeof (char *) +
                                 (sizeof (char) * STRING_SIZE) * 1);

        CMSG_PROF_DISABLE (&client->prof);
    }
}

void
client_test_api_all_repeated (cmsg_client *client, char *log_file)
{
    int32_t result_field_1 = 0;

    printf ("prof_test_api_all_repeated\n");
    for (s = 1; s <= SIZE_STEPS; s++)
    {
        int size_byte = (sizeof (int8_t) + sizeof (uint8_t) +
                         sizeof (int16_t) + sizeof (uint16_t) +
                         sizeof (int32_t) + sizeof (int32_t) +
                         sizeof (int32_t) + sizeof (uint32_t) +
                         sizeof (uint32_t) + sizeof (int64_t) +
                         sizeof (int64_t) + sizeof (int64_t) +
                         sizeof (uint64_t) + sizeof (uint64_t) +
                         sizeof (float) + sizeof (double) +
                         sizeof (cmsg_bool_t) + sizeof (char *) +
                         (sizeof (char) * STRING_SIZE) * s);

        printf ("%d of %d\n", size_byte, biggest_type_size);

        for (i = 1; i <= TEST_REPEAT; i++)
        {
            CMSG_PROF_ENABLE (&client->prof);
            CMSG_PROF_TIME_LOG_START (&client->prof, log_file);

            size_t n_field_1 = s;
            size_t n_field_2 = s;
            size_t n_field_3 = s;
            size_t n_field_4 = s;
            size_t n_field_5 = s;
            size_t n_field_6 = s;
            size_t n_field_7 = s;
            size_t n_field_8 = s;
            size_t n_field_9 = s;
            size_t n_field_10 = s;
            size_t n_field_11 = s;
            size_t n_field_12 = s;
            size_t n_field_13 = s;
            size_t n_field_14 = s;
            size_t n_field_15 = s;
            size_t n_field_16 = s;
            size_t n_field_17 = s;
            size_t n_field_18 = s;

            int8_t *field_1 = malloc (sizeof (int8_t) * s);
            uint8_t *field_2 = malloc (sizeof (uint8_t) * s);
            int16_t *field_3 = malloc (sizeof (int16_t) * s);
            uint16_t *field_4 = malloc (sizeof (uint16_t) * s);
            int32_t *field_5 = malloc (sizeof (int32_t) * s);
            int32_t *field_6 = malloc (sizeof (int32_t) * s);
            int32_t *field_7 = malloc (sizeof (int32_t) * s);
            uint32_t *field_8 = malloc (sizeof (uint32_t) * s);
            uint32_t *field_9 = malloc (sizeof (uint32_t) * s);
            int64_t *field_10 = malloc (sizeof (int64_t) * s);
            int64_t *field_11 = malloc (sizeof (int64_t) * s);
            int64_t *field_12 = malloc (sizeof (int64_t) * s);
            uint64_t *field_13 = malloc (sizeof (uint64_t) * s);
            uint64_t *field_14 = malloc (sizeof (uint64_t) * s);
            float *field_15 = malloc (sizeof (float) * s);
            double *field_16 = malloc (sizeof (double) * s);
            cmsg_bool_t *field_17 = malloc (sizeof (cmsg_bool_t) * s);
            char **field_18 = malloc (sizeof (char *) * s);

            for (j = 0; j < s; j++) //fill up depending on size
                field_18[j] = malloc (sizeof (char) * STRING_SIZE);

            for (j = 0; j < s; j++)
            {
                field_1[j] = 0xff;
                field_2[j] = 0xff;
                field_3[j] = 0xffff;
                field_4[j] = 0xffff;
                field_5[j] = 0xffffffff;
                field_6[j] = 0xffffffff;
                field_7[j] = 0xffffffff;
                field_8[j] = 0xffffffff;
                field_9[j] = 0xffffffff;
                field_10[j] = 0xffffffffffffffff;
                field_11[j] = 0xffffffffffffffff;
                field_12[j] = 0xffffffffffffffff;
                field_13[j] = 0xffffffffffffffff;
                field_14[j] = 0xffffffffffffffff;
                field_15[j] = FLT_MAX;
                field_16[j] = DBL_MAX;
                field_17[j] = 1;
                strcpy (field_18[j], "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
            }

            prof_test_api_all_repeated (client,
                                        n_field_1, field_1, n_field_2, field_2,
                                        n_field_3, field_3, n_field_4, field_4,
                                        n_field_5, field_5, n_field_6, field_6,
                                        n_field_7, field_7, n_field_8, field_8,
                                        n_field_9, field_9, n_field_10, field_10,
                                        n_field_11, field_11, n_field_12, field_12,
                                        n_field_13, field_13, n_field_14, field_14,
                                        n_field_15, field_15, n_field_16, field_16,
                                        n_field_17, field_17, n_field_18, field_18,
                                        &result_field_1);

            free (field_1);
            free (field_2);
            free (field_3);
            free (field_4);
            free (field_5);
            free (field_6);
            free (field_7);
            free (field_8);
            free (field_9);
            free (field_10);
            free (field_11);
            free (field_12);
            free (field_13);
            free (field_14);
            free (field_15);
            free (field_16);
            free (field_17);

            for (j = 0; j < s; j++)
                free (field_18[j]);

            free (field_18);

            CMSG_PROF_TIME_LOG_STOP (&client->prof, "all repeated", size_byte);
            CMSG_PROF_DISABLE (&client->prof);
        }
    }
}

void
client_test (char *log_file)
{
    int32_t result_field_1 = 0;
    cmsg_transport *transport = cmsg_transport_new (CMSG_TRANSPORT_RPC_TIPC);

    transport->config.socket.sockaddr.tipc.family = AF_TIPC;
    transport->config.socket.sockaddr.tipc.addrtype = TIPC_ADDR_NAME;
    transport->config.socket.sockaddr.tipc.addr.name.name.type = 19999; //TIPC PORT
    transport->config.socket.sockaddr.tipc.addr.name.name.instance = 1; //MEMBER ID
    transport->config.socket.sockaddr.tipc.addr.name.domain = 0;
    transport->config.socket.sockaddr.tipc.scope = TIPC_NODE_SCOPE;

    cmsg_client *client = cmsg_client_new (transport, CMSG_DESCRIPTOR (prof, test));

    //run the tests
    client_test_api_all (client, log_file);
    client_test_api_all_repeated (client, log_file);

    CALL_PROF_REPEATED (int8_t, int8, 0xff);
    CALL_PROF_REPEATED (uint8_t, uint8, 0xff);
    CALL_PROF_REPEATED (int16_t, int16, 0xffff);
    CALL_PROF_REPEATED (uint16_t, uint16, 0xffff);
    CALL_PROF_REPEATED (int32_t, int32, 0xffffffff);
    CALL_PROF_REPEATED (int32_t, sint32, 0xffffffff);
    CALL_PROF_REPEATED (int32_t, sfixed32, 0xffffffff);
    CALL_PROF_REPEATED (uint32_t, uint32, 0xffffffff);
    CALL_PROF_REPEATED (uint32_t, fixed32, 0xffffffff);
    CALL_PROF_REPEATED (int64_t, int64, 0xffffffffffffffff);
    CALL_PROF_REPEATED (int64_t, sint64, 0xffffffffffffffff);
    CALL_PROF_REPEATED (int64_t, sfixed64, 0xffffffffffffffff);
    CALL_PROF_REPEATED (uint64_t, uint64, 0xffffffffffffffff);
    CALL_PROF_REPEATED (uint64_t, fixed64, 0xffffffffffffffff);
    CALL_PROF_REPEATED (float, float, FLT_MAX);
    CALL_PROF_REPEATED (double, double, DBL_MAX);
    CALL_PROF_REPEATED (cmsg_bool_t, bool, 1);
    cmsg_client_destroy (client);
    cmsg_transport_destroy (transport);
}

void
prof_test_impl_all (const void *service,
                    int8_t field_1, uint8_t field_2, int16_t field_3, uint16_t field_4,
                    int32_t field_5, int32_t field_6, int32_t field_7, uint32_t field_8,
                    uint32_t field_9, int64_t field_10, int64_t field_11, int64_t field_12,
                    uint64_t field_13, uint64_t field_14, float field_15, double field_16,
                    cmsg_bool_t field_17, char *field_18)
{
    prof_test_server_allSend (service, 0xffffffff);
}

void
prof_test_impl_all_repeated (const void *service,
                             size_t n_field_1, int8_t *field_1, size_t n_field_2,
                             uint8_t *field_2, size_t n_field_3, int16_t *field_3,
                             size_t n_field_4, uint16_t *field_4, size_t n_field_5,
                             int32_t *field_5, size_t n_field_6, int32_t *field_6,
                             size_t n_field_7, int32_t *field_7, size_t n_field_8,
                             uint32_t *field_8, size_t n_field_9, uint32_t *field_9,
                             size_t n_field_10, int64_t *field_10, size_t n_field_11,
                             int64_t *field_11, size_t n_field_12, int64_t *field_12,
                             size_t n_field_13, uint64_t *field_13, size_t n_field_14,
                             uint64_t *field_14, size_t n_field_15, float *field_15,
                             size_t n_field_16, double *field_16, size_t n_field_17,
                             cmsg_bool_t *field_17, size_t n_field_18, char **field_18)
{
    prof_test_server_all_repeatedSend (service, 0xffffffff);
}

//implement the rest of the implementations using macros
IMPLEMENT_PROF_REPEATED (int8_t *, int8, 0xffffffff);
IMPLEMENT_PROF_REPEATED (uint8_t *, uint8, 0xffffffff);
IMPLEMENT_PROF_REPEATED (int16_t *, int16, 0xffffffff);
IMPLEMENT_PROF_REPEATED (uint16_t *, uint16, 0xffffffff);
IMPLEMENT_PROF_REPEATED (int32_t *, int32, 0xffffffff);
IMPLEMENT_PROF_REPEATED (int32_t *, sint32, 0xffffffff);
IMPLEMENT_PROF_REPEATED (int32_t *, sfixed32, 0xffffffff);
IMPLEMENT_PROF_REPEATED (uint32_t *, uint32, 0xffffffff);
IMPLEMENT_PROF_REPEATED (uint32_t *, fixed32, 0xffffffff);
IMPLEMENT_PROF_REPEATED (int64_t *, int64, 0xffffffff);
IMPLEMENT_PROF_REPEATED (int64_t *, sint64, 0xffffffff);
IMPLEMENT_PROF_REPEATED (int64_t *, sfixed64, 0xffffffff);
IMPLEMENT_PROF_REPEATED (uint64_t *, uint64, 0xffffffff);
IMPLEMENT_PROF_REPEATED (uint64_t *, fixed64, 0xffffffff);
IMPLEMENT_PROF_REPEATED (float *, float, 0xffffffff);
IMPLEMENT_PROF_REPEATED (double *, double, 0xffffffff);
IMPLEMENT_PROF_REPEATED (cmsg_bool_t *, bool, 0xffffffff);
IMPLEMENT_PROF_REPEATED (char **, string, 0xffffffff);

void
server_test (char *log_file)
{
    cmsg_server *server;
    cmsg_transport *transport = cmsg_transport_new (CMSG_TRANSPORT_RPC_TIPC);
    transport->config.socket.sockaddr.tipc.family = AF_TIPC;
    transport->config.socket.sockaddr.tipc.addrtype = TIPC_ADDR_NAME;
    transport->config.socket.sockaddr.tipc.addr.name.name.type = 19999; //TIPC PORT
    transport->config.socket.sockaddr.tipc.addr.name.name.instance = 1; //MEMBER ID
    transport->config.socket.sockaddr.tipc.addr.name.domain = 0;
    transport->config.socket.sockaddr.tipc.scope = TIPC_NODE_SCOPE;
    server = cmsg_server_new (transport, CMSG_SERVICE (prof, test));
    if (!server)
    {
        printf ("server could not initialize\n");
        exit (0);
    }

    int fd = cmsg_server_get_socket (server);
    int fd_max = fd + 1;
    if (!fd)
    {
        printf ("initialized rpc failed (socket %d)\n", fd);
    }

    fd_set readfds;
    FD_ZERO (&readfds);
    FD_SET (fd, &readfds);

    while (1)
    {
        cmsg_server_receive_poll (server, 1000, &readfds, &fd_max);
    }

    cmsg_server_destroy (server);
    cmsg_transport_destroy (transport);
}

int
main (int argc, char *argv[])
{
    pid_t pid;
    file_name_prefix[0] = '\0';

    extern char *__progname;
    printf ("__progname: %s\n", __progname);

    if (argc <= 1)
    {
        printf ("Usage:\n");
        printf ("cmsg-prof log_file_prefix\n");
        printf ("or\n");
        printf ("cmsg-prof --server\n");
        printf ("or\n");
        printf ("cmsg-prof --client log_file_prefix\n");
        return 0;
    }

    sprintf (file_name_prefix, "%s", argv[1]);

    if (!strcmp (argv[1], "--server"))
    {
        printf ("starting server\n");
        char log_file[256];
        sprintf (log_file, "%s-cmsg_server_prof.csv", file_name_prefix);
        server_test (log_file);
    }
    else if (!strcmp (argv[1], "--client"))
    {
        if (argv[2])
        {
            sprintf (file_name_prefix, "%s", argv[2]);
        }
        else
        {
            sprintf (file_name_prefix, "noprefix");
        }
        printf ("starting client\n");
        char log_file[256];
        sprintf (log_file, "%s-cmsg_prof.csv", file_name_prefix);
        client_test (log_file);
    }
    else
    {
        pid = fork ();
        if (pid == -1)
        {
            exit (0);
        }
        else if (pid == 0)  //child
        {
            printf ("starting server\n");
            char log_file[256];
            sprintf (log_file, "%s-cmsg_server_prof.csv", file_name_prefix);
            server_test (log_file);

            _exit (0);
        }
        else
        {
            sleep (3);

            printf ("starting client\n");
            char log_file[256];
            sprintf (log_file, "%s-cmsg_prof.csv", file_name_prefix);
            client_test (log_file);

            kill (0, SIGTERM);
        }
    }
    return 0;
}
