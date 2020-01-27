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
#include "cmsg.h"
#include "cmsg_client.h"

#define LIB_PATH "/usr/lib"
#define MSG_BUF_LEN 200

typedef int (*cmsg_api_func_ptr) ();

typedef enum transport_type_e
{
    TRANSPORT_TYPE_NONE,
    TRANSPORT_TYPE_UNIX,
    TRANSPORT_TYPE_TIPC,
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
    int32_t tipc_member_id;
    struct in_addr tcp_ip_address;
    bool valid_ip_address;
    bool oneway;
    bool disable_error_logs;
} program_args;

typedef struct pbc_descriptors_s
{
    const ProtobufCServiceDescriptor *service_descriptor;
    const ProtobufCMessageDescriptor *input_msg_descriptor;
    const ProtobufCMessageDescriptor *output_msg_descriptor;
    cmsg_api_func_ptr api_ptr;
} pbc_descriptors;

#define SHELL_OPTIONS    "a:f:hi:m:n:op:s:t:r:"

static struct option longopts[] = {
    { "api_name", required_argument, NULL, 'a' },
    { "file_name", required_argument, NULL, 'f' },
    { "help", no_argument, NULL, 'h' },
    { "tipc_member_id", required_argument, NULL, 'i' },
    { "message_data", required_argument, NULL, 'm' },
    { "port_service_name", required_argument, NULL, 'n' },
    { "one_way", no_argument, NULL, 'o' },
    { "disable_error_logs", no_argument, NULL, 'q' },
    { "package_name", required_argument, NULL, 'p' },
    { "service_name", required_argument, NULL, 's' },
    { "transport_type", required_argument, NULL, 't' },
    { "ip_address", required_argument, NULL, 'r' },
    { 0 }
};

/**
 * Display usage of the application.
 */
static void
usage (void)
{
    fprintf (stderr,
             "Usage: cmsg [-t {unix|tipc|tcp}] | [-f FILE_NAME] | [-p PACKAGE_NAME] |\n"
             "            [-s CMSG_SERVICE_NAME] | [-a API_NAME] | [-m MESSAGE_DATA] |\n"
             "            [-o] | [-n PORT_SERVICE_NAME] | [-i TIPC_MEMBER_ID]\n"
             "\n"
             "Options:\n"
             "  -h                      Display this message.\n"
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
             "                          (if using a TIPC or TCP transport).\n"
             "  -r TCP_IP_ADDRESS       The IP address of the server to connect to (if using a\n"
             "                          TCP transport). Currently this must be an IPv4 address.\n"
             "  -i TIPC_MEMBER_ID       The TIPC node to connect to (if using TIPC transport).\n"
             "                          This assumes TIPC_CLUSTER_SCOPE.\n\n");
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
    else if (strcmp (type_string, "tipc") == 0)
    {
        type = TRANSPORT_TYPE_TIPC;
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
 * Convert the tipc member id argument string to an integer value.
 *
 * @param id - The input argument string
 *
 * @returns The converted integer value on success,
 *          -1 it could not be converted.
 */
static int32_t
get_tipc_member_id (const char *id)
{
    int32_t value = -1;
    int32_t ret = -1;
    char *endptr = NULL;

    value = strtol (id, &endptr, 10);
    if (*endptr == '\0')
    {
        ret = value;
    }

    return ret;
}

/**
 * Initialise a 'program_args' args structure to suitable default values.
 *
 * @param args - The 'program_args' structure to initialise.
 */
static void
program_args_init (program_args *args)
{
    args->transport_type = TRANSPORT_TYPE_NONE;
    args->file_name = NULL;
    args->package_name = NULL;
    args->service_name = NULL;
    args->api_name = NULL;
    args->message_data = NULL;
    args->port_service_name = NULL;
    args->tipc_member_id = -1;
    args->oneway = false;
    args->valid_ip_address = false;
    args->disable_error_logs = false;
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
    if (args->api_name == NULL)
    {
        fprintf (stderr, "An api/rpc name must be supplied.\n");
        return false;
    }
    if (args->transport_type == TRANSPORT_TYPE_TIPC)
    {
        if (args->port_service_name == NULL)
        {
            fprintf (stderr, "A service name for the port must be supplied.\n");
            return false;
        }
        if (args->tipc_member_id == -1)
        {
            fprintf (stderr, "A TIPC member id must be supplied.\n");
            return false;
        }
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
        case 'f':
            args->file_name = optarg;
            break;
        case 'h':
            usage ();
            exit (EXIT_SUCCESS);
        case 'i':
            args->tipc_member_id = get_tipc_member_id (optarg);
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

    return check_input_arguments (args);
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
    char *symbol_name = NULL;
    const ProtobufCMethodDescriptor *method = NULL;
    const ProtobufCServiceDescriptor *service_descriptor;

    if (asprintf (&symbol_name, "%s__%s__descriptor", args->package_name,
                  args->service_name) < 0)
    {
        fprintf (stderr, "Unable to allocate memory.\n");
        return false;
    }

    service_descriptor = dlsym (lib_handle, symbol_name);
    if (service_descriptor == NULL)
    {
        fprintf (stderr, "Unable to locate service descriptor (symbol = %s).\n",
                 symbol_name);
        return false;
    }
    free (symbol_name);
    descriptors->service_descriptor = service_descriptor;

    method = protobuf_c_service_descriptor_get_method_by_name (service_descriptor,
                                                               args->api_name);
    if (method == NULL)
    {
        fprintf (stderr, "Unable to locate method descriptor (method = %s).\n",
                 args->api_name);
        return false;
    }

    descriptors->input_msg_descriptor = method->input;
    descriptors->output_msg_descriptor = method->output;

    if (asprintf (&symbol_name, "%s_%s_api_%s", args->package_name, args->service_name,
                  args->api_name) < 0)
    {
        fprintf (stderr, "Unable to allocate memory.\n");
        return false;
    }

    descriptors->api_ptr = dlsym (lib_handle, symbol_name);
    if (descriptors->api_ptr == NULL)
    {
        fprintf (stderr, "Unable to locate api method pointer (symbol = %s).\n",
                 symbol_name);
        return false;
    }

    free (symbol_name);
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
    else if (args->transport_type == TRANSPORT_TYPE_TIPC)
    {
        if (args->oneway)
        {
            client = cmsg_create_client_tipc_oneway (args->port_service_name,
                                                     args->tipc_member_id,
                                                     TIPC_CLUSTER_SCOPE,
                                                     service_descriptor);
        }
        else
        {
            client = cmsg_create_client_tipc_rpc (args->port_service_name,
                                                  args->tipc_member_id,
                                                  TIPC_CLUSTER_SCOPE, service_descriptor);
        }
    }
    else if (args->transport_type == TRANSPORT_TYPE_TCP)
    {
        if (args->oneway)
        {
            client = cmsg_create_client_tcp_ipv4_oneway (args->port_service_name,
                                                         &args->tcp_ip_address,
                                                         service_descriptor);
        }
        else
        {
            client = cmsg_create_client_tcp_ipv4_rpc (args->port_service_name,
                                                      &args->tcp_ip_address,
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
    bool no_input_arg = (strcmp (descriptors->input_msg_descriptor->name, "dummy") == 0);
    bool no_output_arg = (strcmp (descriptors->output_msg_descriptor->name, "dummy") == 0);

    client = create_client (args, descriptors->service_descriptor);
    if (client == NULL)
    {
        fprintf (stderr, "Failed to create CMSG client\n");
        return false;
    }

    if (no_input_arg && no_output_arg)
    {
        cmsg_ret = descriptors->api_ptr (client);
    }
    else if (no_input_arg)
    {
        cmsg_ret = descriptors->api_ptr (client, output_proto_message);
    }
    else if (no_output_arg)
    {
        cmsg_ret = descriptors->api_ptr (client, input_proto_message);
    }
    else
    {
        cmsg_ret = descriptors->api_ptr (client, input_proto_message, output_proto_message);
    }

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
convert_and_print_output (ProtobufCMessage *output_proto_message)
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

int
main (int argc, char **argv)
{
    program_args args = { 0 };
    pbc_descriptors descriptors = { 0 };
    static void *lib_handle = NULL;
    ProtobufCMessage *input_proto_message = NULL;
    ProtobufCMessage *output_proto_message = NULL;

    if (!parse_input_arguments (argc, argv, &args))
    {
        exit (EXIT_FAILURE);
    }

    lib_handle = load_library (args.file_name);
    if (!lib_handle)
    {
        exit (EXIT_FAILURE);
    }

    if (!find_descriptors (lib_handle, &args, &descriptors))
    {
        dlclose (lib_handle);
        exit (EXIT_FAILURE);
    }

    if (!convert_input (args.message_data, descriptors.input_msg_descriptor,
                        &input_proto_message))
    {
        dlclose (lib_handle);
        exit (EXIT_FAILURE);
    }

    if (!call_api (&args, &descriptors, input_proto_message, &output_proto_message))
    {
        dlclose (lib_handle);
        exit (EXIT_FAILURE);
    }

    if (output_proto_message && !convert_and_print_output (output_proto_message))
    {
        CMSG_FREE_RECV_MSG (output_proto_message);
        dlclose (lib_handle);
        exit (EXIT_FAILURE);
    }

    CMSG_FREE_RECV_MSG (output_proto_message);
    dlclose (lib_handle);
    return EXIT_SUCCESS;
}
