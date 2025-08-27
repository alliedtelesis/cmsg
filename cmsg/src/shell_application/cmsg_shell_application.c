/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * cmsg_shell_application.c
 *
 * A simple application that can be called from the shell to call a CMSG API.
 *
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <dirent.h>
#include <glib.h>
#include <protobuf2json.h>
#include <arpa/inet.h>
#include <cmsg/cmsg.h>
#include <cmsg/cmsg_client.h>
#include <cmsg/cmsg_pthread_helpers.h>
#include "cmsg_protobuf-c.h"

#define LIB_PATH "/usr/lib"
#define MSG_BUF_LEN 200

volatile sig_atomic_t stop = 0;
void
handle_sigint (int sig)
{
    stop = 1;
}

typedef int (*cmsg_api_func_ptr) ();

typedef enum transport_type_e
{
    TRANSPORT_TYPE_NONE,
    TRANSPORT_TYPE_UNIX,
    TRANSPORT_TYPE_TCP,
} transport_type_t;

typedef struct program_args_s
{
    transport_type_t transport_type;
    char *file_name;
    char *package_name;
    char *service_name;
    char *api_name;
    char *message_data;
    char *port_service_name;
    struct in_addr tcp_ip_address;
    bool valid_ip_address;
    bool oneway;
    bool disable_error_logs;
    bool list_methods;
    uint32_t timeout;
    bool monitor;
} program_args;

typedef struct pbc_descriptors_s
{
    const cmsg_api_descriptor *cmsg_service_descriptor;
    const ProtobufCMessageDescriptor *input_msg_descriptor;
    const ProtobufCMessageDescriptor *output_msg_descriptor;
    int method_index;
} pbc_descriptors;

#define SHELL_OPTIONS    "a:f:hi:lm:n:op:s:t:r:T:w"

static struct option longopts[] = {
    { "api_name", required_argument, NULL, 'a' },
    { "file_name", required_argument, NULL, 'f' },
    { "help", no_argument, NULL, 'h' },
    { "list_methods", no_argument, NULL, 'l' },
    { "monitor", no_argument, NULL, 'w' },
    { "message_data", required_argument, NULL, 'm' },
    { "port_service_name", required_argument, NULL, 'n' },
    { "one_way", no_argument, NULL, 'o' },
    { "disable_error_logs", no_argument, NULL, 'q' },
    { "package_name", required_argument, NULL, 'p' },
    { "service_name", required_argument, NULL, 's' },
    { "transport_type", required_argument, NULL, 't' },
    { "ip_address", required_argument, NULL, 'r' },
    { "timeout", required_argument, NULL, 'T' },
    { 0 }
};

/**
 * Display usage of the application.
 */
static void
usage (void)
{
    fprintf (stderr,
             "Usage: cmsg [-t {unix|tcp}] | [-f FILE_NAME] | [-p PACKAGE_NAME] |\n"
             "            [-s CMSG_SERVICE_NAME] | [-a API_NAME] | [-m MESSAGE_DATA] |\n"
             "            [-o] | [-n PORT_SERVICE_NAME] | [-T TIMEOUT]\n"
             "       cmsg [-l] | [-f FILE_NAME] | [-p PACKAGE_NAME] | [-s CMSG_SERVICE_NAME]\n"
             "\n"
             "Options:\n"
             "  -h                      Display this message.\n"
             "  -l                      List methods for a service.\n"
             "  -w                      Monitor notifications.\n"
             "  -t TRANSPORT_TYPE       The type of transport to use for the cmsg client.\n"
             "  -f FILE_NAME            The name of the .proto file defining the service (do\n"
             "                          not include the '.proto' part).\n"
             "  -p PACKAGE_NAME         The name of the package.\n"
             "  -s CMSG_SERVICE_NAME    The name of the CMSG service.\n"
             "  -a API_NAME             The name of the api/rpc to call.\n"
             "  -m MESSAGE_DATA         The message to call the api/rpc with. This should be\n"
             "                          in JSON format.\n"
             "  -o                      The CMSG client should be oneway (defaults to two-way/rpc).\n"
             "  -q                      Disable the printing of any error logs that may occur.\n"
             "  -n PORT_SERVICE_NAME    The service name for the port specified in the /etc/services file\n"
             "                          (if using a TCP transport).\n"
             "  -r TCP_IP_ADDRESS       The IP address of the server to connect to (if using a\n"
             "                          TCP transport). Currently this must be an IPv4 address.\n"
             "  -T TIMEOUT              Receive timeout in seconds (default: 100)\n\n");
}

/**
 * Convert the transport type argument string to the related enum value.
 *
 * @param type_string - The input argument string
 *
 * @returns The converted transport_type_t enum value on success,
 *          TRANSPORT_TYPE_NONE if it could not be converted.
 */
static transport_type_t
get_transport_type (const char *type_string)
{
    transport_type_t type = TRANSPORT_TYPE_NONE;

    if (strcmp (type_string, "unix") == 0)
    {
        type = TRANSPORT_TYPE_UNIX;
    }
    else if (strcmp (type_string, "tcp") == 0)
    {
        type = TRANSPORT_TYPE_TCP;
    }

    return type;
}

static void
get_ipv4_address (program_args *args, const char *addr_string)
{
    if (inet_pton (AF_INET, addr_string, &args->tcp_ip_address) == 1)
    {
        args->valid_ip_address = true;
    }
    else
    {
        args->valid_ip_address = false;
    }
}

/**
 * Initialise a 'program_args' args structure to suitable default values.
 *
 * @param args - The 'program_args' structure to initialise.
 */
static void
program_args_init (program_args *args)
{
    const char *timeout_str;

    args->transport_type = TRANSPORT_TYPE_NONE;
    args->file_name = NULL;
    args->package_name = NULL;
    args->service_name = NULL;
    args->api_name = NULL;
    args->message_data = NULL;
    args->port_service_name = NULL;
    args->oneway = false;
    args->valid_ip_address = false;
    args->disable_error_logs = false;
    args->timeout = 0;
    args->monitor = false;

    /* Choose the default timeout if it's set via environment variable */
    timeout_str = getenv ("CMSG_TIMEOUT");
    if (timeout_str)
    {
        args->timeout = atoi (timeout_str);
    }
}

/**
 * Check that the user of the application has entered all of the required arguments
 * to identify a particular CMSG service.
 *
 * @param args - The 'program_args' structure containing the parsed arguments.
 *
 * @returns true if all of the required arguments have been entered,
 *          false otherwise.
 */
static bool
check_service_input_arguments (program_args *args)
{
    if (args->file_name == NULL)
    {
        fprintf (stderr, "A file name must be supplied.\n");
        return false;
    }
    if (args->package_name == NULL)
    {
        fprintf (stderr, "A package name must be supplied.\n");
        return false;
    }
    if (args->service_name == NULL)
    {
        fprintf (stderr, "A service name must be supplied.\n");
        return false;
    }

    return true;
}

/**
 * Check that the user of the application has entered all of the required arguments.
 *
 * @param args - The 'program_args' structure containing the parsed arguments.
 *
 * @returns true if all of the required arguments have been entered,
 *          false otherwise.
 */
static bool
check_input_arguments (program_args *args)
{
    if (args->transport_type == TRANSPORT_TYPE_NONE)
    {
        fprintf (stderr, "A transport type must be supplied.\n");
        return false;
    }
    if (!check_service_input_arguments (args))
    {
        return false;
    }
    if (args->api_name == NULL && !args->monitor)
    {
        fprintf (stderr, "An api/rpc name must be supplied.\n");
        return false;
    }
    if (args->transport_type == TRANSPORT_TYPE_TCP)
    {
        if (args->port_service_name == NULL)
        {
            fprintf (stderr, "A service name for the port must be supplied.\n");
            return false;
        }
        if (args->valid_ip_address == false)
        {
            fprintf (stderr, "A valid IP address of the server must be supplied.\n");
            return false;
        }
    }

    return true;
}

/**
 * Parse the arguments the program was called with.
 *
 * @param argc - Passed into the main function.
 * @param argv - Passed into the main function.
 * @param args - The 'program_args' structure to store the parsed arguments in.
 *
 * @returns true if all of the arguments were parsed correctly and the user has supplied
 *          all of the arguments that are required.
 *          false otherwise.
 */
static bool
parse_input_arguments (int argc, char **argv, program_args *args)
{
    int opt = 0;

    program_args_init (args);

    while (1)
    {
        opt = getopt_long (argc, argv, SHELL_OPTIONS, longopts, 0);

        if (opt == EOF)
        {
            break;
        }

        switch (opt)
        {
        case 0:
            break;
        case 'a':
            args->api_name = optarg;
            break;
        case 'w':
            args->monitor = true;
            break;
        case 'f':
            args->file_name = optarg;
            break;
        case 'h':
            usage ();
            exit (EXIT_SUCCESS);
        case 'l':
            args->list_methods = true;
            break;
        case 'm':
            args->message_data = optarg;
            break;
        case 'n':
            args->port_service_name = optarg;
            break;
        case 'o':
            args->oneway = true;
            break;
        case 'q':
            args->disable_error_logs = true;
            break;
        case 'p':
            args->package_name = optarg;
            break;
        case 's':
            args->service_name = optarg;
            break;
        case 't':
            args->transport_type = get_transport_type (optarg);
            break;
        case 'r':
            get_ipv4_address (args, optarg);
            break;
        case 'T':
            args->timeout = atoi (optarg);
            break;
        case '?':
            /* getopt_long already printed an error message. */
            exit (EXIT_FAILURE);
            break;
        default:
            exit (EXIT_FAILURE);
            fprintf (stderr, "Failed to parse program arguments");
            break;
        }
    }

    if (args->list_methods)
    {
        return check_service_input_arguments (args);
    }
    else
    {
        return check_input_arguments (args);
    }
}

/**
 * Load a CMSG proto_api library based on the input file_name. This file_name
 * is the name of the proto file (without '.proto') that the API the caller wishes
 * to call was defined in.
 *
 * @param file_name - The name of the file name the API to call was defined in.
 *
 * @returns A pointer containing the handler of the library if it was successfully loaded.
 *          NULL on failure.
 *
 */
static void *
load_library (const char *file_name)
{
    char *library_path = NULL;
    void *handle = NULL;

    if (asprintf (&library_path, "%s/lib%s_proto_api.so", LIB_PATH, file_name) < 0)
    {
        fprintf (stderr, "Unable to allocate memory.\n");
        return NULL;
    }

    handle = dlopen (library_path, RTLD_NOW | RTLD_GLOBAL);
    if (handle == NULL)
    {
        fprintf (stderr, "%s\n", dlerror ());
        return NULL;
    }
    free (library_path);

    return handle;
}

/**
 * Load the cmsg_service_descriptor from the dynamically loaded library.
 *
 * @param lib_handle - The handle returned from opening the library using dlopen().
 * @param args - The 'program_args' structure containing the arguments entered
 *               when calling the application.
 * @param cmsg_service_descriptor - Pointer to hold the loaded service descriptor.
 *
 * @returns true if the descriptor was loaded.
 *          false otherwise.
 */
static bool
load_cmsg_service_descriptor (void *lib_handle, program_args *args,
                              const cmsg_api_descriptor **cmsg_service_descriptor)
{
    char *symbol_name = NULL;

    if (asprintf (&symbol_name, "%s_%s_cmsg_api_descriptor", args->package_name,
                  args->service_name) < 0)
    {
        fprintf (stderr, "Unable to allocate memory.\n");
        return false;
    }

    *cmsg_service_descriptor = dlsym (lib_handle, symbol_name);
    if (*cmsg_service_descriptor == NULL)
    {
        fprintf (stderr, "Unable to locate service descriptor (symbol = %s).\n",
                 symbol_name);
        return false;
    }
    free (symbol_name);

    return true;
}

/**
 * Find the protobuf-c descriptors and CMSG API function pointer inside the
 * dynamically loaded library.
 *
 * @param lib_handle - The handle returned from opening the library using dlopen().
 * @param args - The 'program_args' structure containing the arguments entered
 *               when calling the application.
 * @param descriptors - A 'pbc_descriptors' structure to store the descriptors and
 *                      API function pointer found in the library.
 *
 * @returns true if the descriptors were found,
 *          false otherwise.
 */
static bool
find_descriptors (void *lib_handle, program_args *args, pbc_descriptors *descriptors)
{
    int method_index;
    const cmsg_api_descriptor *cmsg_service_descriptor;
    const ProtobufCServiceDescriptor *service_descriptor;

    if (!load_cmsg_service_descriptor (lib_handle, args, &cmsg_service_descriptor))
    {
        return false;
    }

    descriptors->cmsg_service_descriptor = cmsg_service_descriptor;
    service_descriptor = cmsg_service_descriptor->service_desc;
    method_index =
        protobuf_c_service_descriptor_get_method_index_by_name (service_descriptor,
                                                                args->api_name);
    if (method_index == UNDEFINED_METHOD)
    {
        fprintf (stderr, "Unable to locate method descriptor (method = %s).\n",
                 args->api_name);
        return false;
    }

    descriptors->method_index = method_index;
    descriptors->input_msg_descriptor =
        CMSG_INPUT_MSG_DESCRIPTOR (service_descriptor, method_index);
    descriptors->output_msg_descriptor =
        CMSG_OUTPUT_MSG_DESCRIPTOR (service_descriptor, method_index);

    return true;
}

/**
 * Convert the input JSON string to a ProtobufCMessage message.
 *
 * @param input_json_string - The JSON string to convert.
 * @param input_msg_descriptor - The ProtobufCMessageDescriptor defining the
 *                               ProtobufCMessage to convert to.
 * @param input_proto_message - Pointer to a ProtobufCMessage structure to
 *                              store the converted message.
 *
 * @returns true if the conversion was successful,
 *          false otherwise.
 */
static bool
convert_input (const char *input_json_string,
               const ProtobufCMessageDescriptor *input_msg_descriptor,
               ProtobufCMessage **input_proto_message)
{
    char conversion_message[MSG_BUF_LEN] = { 0 };
    json_t *json_obj = NULL;
    json_error_t error;
    int res;
    bool ret = true;

    json_obj = json_loads (input_json_string, JSON_DECODE_ANY, &error);
    if (!json_obj)
    {
        fprintf (stderr, "Invalid input JSON (%s).\n", error.text);
        return false;
    }

    res = json2protobuf_object (json_obj, input_msg_descriptor, input_proto_message,
                                conversion_message, MSG_BUF_LEN);
    if (res < 0)
    {
        fprintf (stderr, "Error converting JSON to protobuf (%s).\n", conversion_message);
        ret = false;
    }

    json_decref (json_obj);
    return ret;
}

/**
 * Create a cmsg_client based on the arguments specified when the application
 * was called.
 *
 * @param args - The 'program_args' structure containing the arguments entered
 *               when calling the application.
 * @param service_descriptor - The service_descriptor to create the client for.
 *
 * @returns A pointer a cmsg_client on success. This should be freed using
 *          'cmsg_destroy_client_and_transport'.
 *          NULL on failure.
 */
static cmsg_client *
create_client (program_args *args, const ProtobufCServiceDescriptor *service_descriptor)
{
    cmsg_client *client = NULL;

    if (args->transport_type == TRANSPORT_TYPE_UNIX)
    {
        if (args->oneway)
        {
            client = cmsg_create_client_unix_oneway (service_descriptor);
        }
        else
        {
            client = cmsg_create_client_unix (service_descriptor);
        }
    }
    else if (args->transport_type == TRANSPORT_TYPE_TCP)
    {
        if (args->oneway)
        {
            client = cmsg_create_client_tcp_ipv4_oneway (args->port_service_name,
                                                         &args->tcp_ip_address, NULL,
                                                         service_descriptor);
        }
        else
        {
            client = cmsg_create_client_tcp_ipv4_rpc (args->port_service_name,
                                                      &args->tcp_ip_address, NULL,
                                                      service_descriptor);
        }
    }

    if (client && args->disable_error_logs)
    {
        cmsg_client_suppress_error (client, true);
    }

    return client;
}

/**
 * Call the required CMSG API function.
 *
 * @param args - The 'program_args' structure containing the arguments entered
 *               when calling the application.
 * @param descriptors - The 'pbc_descriptors' structure containing the protobuf-c
 *                      descriptors and api function pointer loaded dynamically
 *                      from the loaded library.
 * @param input_proto_message - The ProtobufCMessage message to call the api function
 *                              with.
 * @param output_proto_message - Pointer to a ProtobufCMessage structure to store any
 *                               message received from the CMSG api call.
 *
 * @returns true if the CMSG API was successfully called,
 *          false otherwise.
 */
static bool
call_api (program_args *args, pbc_descriptors *descriptors,
          ProtobufCMessage *input_proto_message, ProtobufCMessage **output_proto_message)
{
    int cmsg_ret;
    cmsg_client *client = NULL;
    bool ret = true;

    client = create_client (args, descriptors->cmsg_service_descriptor->service_desc);
    if (client == NULL)
    {
        fprintf (stderr, "Failed to create CMSG client\n");
        return false;
    }

    /* Change the default receive timeout with the user-specified value */
    if (args->timeout != 0)
    {
        cmsg_client_set_receive_timeout (client, args->timeout);
    }

    cmsg_ret = cmsg_api_invoke (client, descriptors->cmsg_service_descriptor,
                                descriptors->method_index,
                                input_proto_message, output_proto_message);
    if (cmsg_ret != CMSG_RET_OK)
    {
        fprintf (stderr, "CMSG API call failed (ret = %d).\n", ret);
        ret = false;
    }

    cmsg_destroy_client_and_transport (client);
    return ret;
}

/**
 * Convert the message received from calling the CMSG API into a JSON
 * string and print it to stdout.
 *
 * @param output_proto_message - The message received from the CMSG API call.
 *
 * @returns true if the message was successfully converted to JSON and printed to stdout.
 *          false otherwise.
 */
static bool
convert_and_print_output (const char *root, ProtobufCMessage *output_proto_message)
{
    char conversion_message[MSG_BUF_LEN] = { 0 };
    json_t *output_json = NULL;
    char *json_string = NULL;

    if (protobuf2json_object (output_proto_message, &output_json,
                              conversion_message, MSG_BUF_LEN) < 0)
    {
        fprintf (stderr, "Error converting protobuf to JSON (%s).\n", conversion_message);
        return false;
    }

    if (root)
    {
        json_t *root_json = json_object ();
        json_object_set_new (root_json, root, output_json);
        output_json = root_json;
    }

    json_string = json_dumps (output_json, JSON_INDENT (4));
    if (json_string == NULL)
    {
        json_decref (output_json);
        fprintf (stderr, "Error dumping json object to string.\n");
        return false;
    }

    json_decref (output_json);
    fprintf (stdout, "%s\n", json_string);
    free (json_string);

    return true;
}

/**
 * Handle API request passed in via the arguments and print result
 *
 * @param lib_handle - The handle returned from opening the library using dlopen().
 * @param args - The 'program_args' structure containing the arguments entered
 *               when calling the application.
 *
 * @returns - true on success or false on failure.
 */
bool
handle_request (void *lib_handle, program_args *args)
{
    pbc_descriptors descriptors = { 0 };
    ProtobufCMessage *input_proto_message = NULL;
    ProtobufCMessage *output_proto_message = NULL;
    bool ret = true;

    if (!find_descriptors (lib_handle, args, &descriptors))
    {
        return false;
    }

    if (!convert_input (args->message_data, descriptors.input_msg_descriptor,
                        &input_proto_message))
    {
        return false;
    }

    if (!call_api (args, &descriptors, input_proto_message, &output_proto_message))
    {
        return false;
    }

    if (output_proto_message && !convert_and_print_output (NULL, output_proto_message))
    {
        ret = false;
    }

    CMSG_FREE_RECV_MSG (output_proto_message);

    return ret;
}

/**
 * List methods for the service passed in via the arguments
 *
 * @param lib_handle - The handle returned from opening the library using dlopen().
 * @param args - The 'program_args' structure containing the arguments entered
 *               when calling the application.
 *
 * @returns - true on success or false on failure.
 */
bool
list_methods (void *lib_handle, program_args *args)
{
    const cmsg_api_descriptor *cmsg_service_descriptor;
    const ProtobufCServiceDescriptor *service_descriptor;
    int method_index;

    if (!load_cmsg_service_descriptor (lib_handle, args, &cmsg_service_descriptor))
    {
        return false;
    }

    service_descriptor = cmsg_service_descriptor->service_desc;
    printf ("Methods for service '%s' - index: method (in, out):\n",
            service_descriptor->name);

    for (method_index = 0; method_index < service_descriptor->n_methods; method_index++)
    {
        printf ("%3d: %s (%s, %s)\n", method_index,
                service_descriptor->methods[method_index].name,
                CMSG_INPUT_MSG_DESCRIPTOR (service_descriptor, method_index)->name,
                CMSG_OUTPUT_MSG_DESCRIPTOR (service_descriptor, method_index)->name);
    }

    return true;
}

/**
 * Handler for published notifications
 */
static void
monitor_invoke (ProtobufCService *service, unsigned method_index,
                const ProtobufCMessage *input, ProtobufCClosure closure, void *closure_data)
{
    if (method_index < service->descriptor->n_methods)
    {
        const char *method = service->descriptor->methods[method_index].name;
        convert_and_print_output (method, (ProtobufCMessage *) input);
    }
}

/**
 * Monitor notifications for the service passed in via the arguments
 *
 * @param lib_handle - The handle returned from opening the library using dlopen().
 * @param args - The 'program_args' structure containing the arguments entered
 *               when calling the application.
 *
 * @returns - true on success or false on failure.
 */
static bool
monitor (void *lib_handle, program_args *args)
{
    bool local = args->transport_type == TRANSPORT_TYPE_UNIX;
    const cmsg_api_descriptor *cmsg_descriptor;
    pthread_t subscriber_processing_thread;
    cmsg_subscriber *subscriber;
    cmsg_server *server;
    cmsg_service service_full = {
        .base = {
                 .invoke = monitor_invoke,
                 },
    };
    ProtobufCService *service = &service_full.base;
    int method_index;

    /* Load the requested service descriptor */
    if (!load_cmsg_service_descriptor (lib_handle, args, &cmsg_descriptor))
    {
        return false;
    }
    service->descriptor = cmsg_descriptor->service_desc;

    /* Create the subscriber */
    if (args->transport_type == TRANSPORT_TYPE_UNIX)
    {
        subscriber = cmsg_subscriber_create_unix (service);
        server = subscriber ? cmsg_sub_unix_server_get (subscriber) : NULL;
    }
    else
    {
        subscriber =
            cmsg_subscriber_create_tcp (args->port_service_name, args->tcp_ip_address, NULL,
                                        service);
        server = subscriber ? cmsg_sub_tcp_server_get (subscriber) : NULL;
    }
    if (!subscriber)
    {
        return false;
    }

    /* Subscribe to one or all events (locally if UNIX, remotely if TCP) */
    if (args->api_name)
    {
        if (local)
        {
            cmsg_sub_subscribe_local (subscriber, args->api_name);
        }
        else
        {
            cmsg_sub_subscribe_remote (subscriber, args->api_name, args->tcp_ip_address);
        }
    }
    else
    {
        for (method_index = 0; method_index < service->descriptor->n_methods;
             method_index++)
        {
            if (local)
            {
                cmsg_sub_subscribe_local (subscriber,
                                          service->descriptor->methods[method_index].name);
            }
            else
            {
                cmsg_sub_subscribe_remote (subscriber,
                                           service->descriptor->methods[method_index].name,
                                           args->tcp_ip_address);
            }
        }
    }

    /* Start the processing thread */
    if (!cmsg_pthread_server_init (&subscriber_processing_thread, server))
    {
        fprintf (stderr, "Failed to start subscriber processing thread");
        cmsg_subscriber_destroy (subscriber);
        return false;
    }

    /* Wait for the user to cancel */
    while (!stop)
    {
        pause ();
    }

    /* Cleanup */
    pthread_cancel (subscriber_processing_thread);
    pthread_join (subscriber_processing_thread, NULL);
    cmsg_subscriber_destroy (subscriber);

    return true;
}

int
main (int argc, char **argv)
{
    bool ret;
    program_args args = { 0 };
    static void *lib_handle = NULL;

    signal (SIGINT, handle_sigint);

    if (!parse_input_arguments (argc, argv, &args))
    {
        exit (EXIT_FAILURE);
    }

    lib_handle = load_library (args.file_name);
    if (!lib_handle)
    {
        exit (EXIT_FAILURE);
    }

    if (args.list_methods)
    {
        ret = list_methods (lib_handle, &args);
    }
    else if (args.monitor)
    {
        ret = monitor (lib_handle, &args);
    }
    else
    {
        ret = handle_request (lib_handle, &args);
    }

    dlclose (lib_handle);

    if (!ret)
    {
        exit (EXIT_FAILURE);
    }

    return EXIT_SUCCESS;
}
