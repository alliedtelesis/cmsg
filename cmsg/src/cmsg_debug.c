/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * Functions for dumping the contents of protobuf messages in a user-readable way.
 * The initial implementation was based on protobuf2json code
 * (Copyright (c) 2014-2016 Oleg Efimov <efimovov@gmail.com> MIT license)
 *
 * Copyright 2018, Allied Telesis Labs New Zealand, Ltd
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <protobuf-c/protobuf-c.h>
#ifdef HAVE_TRACE
#include <protobuf2json.h>
#endif
#include "cmsg_debug.h"

/**
 * Return the size of a protobuf value type for conversion. (Currently doesn't support
 * UINT8 or UINT16)
 */
static size_t
cmsg_protobuf_value_size_by_type (ProtobufCType type)
{
    switch (type)
    {
    case PROTOBUF_C_TYPE_INT32:
    case PROTOBUF_C_TYPE_SINT32:
    case PROTOBUF_C_TYPE_SFIXED32:
    case PROTOBUF_C_TYPE_UINT32:
    case PROTOBUF_C_TYPE_FIXED32:
        return 4;
    case PROTOBUF_C_TYPE_INT64:
    case PROTOBUF_C_TYPE_SINT64:
    case PROTOBUF_C_TYPE_SFIXED64:
    case PROTOBUF_C_TYPE_UINT64:
    case PROTOBUF_C_TYPE_FIXED64:
        return 8;
    case PROTOBUF_C_TYPE_FLOAT:
        return 4;
    case PROTOBUF_C_TYPE_DOUBLE:
        return 8;
    case PROTOBUF_C_TYPE_BOOL:
        return sizeof (protobuf_c_boolean);
    case PROTOBUF_C_TYPE_ENUM:
        return 4;
    case PROTOBUF_C_TYPE_STRING:
        return sizeof (char *);
    case PROTOBUF_C_TYPE_BYTES:
        return sizeof (ProtobufCBinaryData);
    case PROTOBUF_C_TYPE_MESSAGE:
        return sizeof (ProtobufCMessage *);
    default:
        return 0;
    }
}

/**
 * Print a protobuf message field value. helper function for cmsg_dump_protobuf_msg
 * (Currently doesn't support UINT8 or UINT16 or oneof type.)
 * @param field_descriptor descriptor of field to print
 * @param protobuf_value pointer to value to print in the message
 * @param is_set Whether the value is set - if not then <not-set> will be printed.
 * @param indent Number of spaces to indent sub-message.
 */
static void
cmsg_dump_protobuf_value (FILE *fp, const ProtobufCFieldDescriptor *field_descriptor,
                          const void *protobuf_value, bool is_set, int indent)
{
    if (!is_set)
    {
        fprintf (fp, "<not-set>\n");
        return;
    }

    switch (field_descriptor->type)
    {
    case PROTOBUF_C_TYPE_INT32:
    case PROTOBUF_C_TYPE_SINT32:
    case PROTOBUF_C_TYPE_SFIXED32:
    case PROTOBUF_C_TYPE_BOOL:
        fprintf (fp, "%" PRId32 "\n", *(int32_t *) protobuf_value);
        break;
    case PROTOBUF_C_TYPE_UINT32:
    case PROTOBUF_C_TYPE_FIXED32:
        fprintf (fp, "%" PRIu32 "\n", *(uint32_t *) protobuf_value);
        break;
        break;
    case PROTOBUF_C_TYPE_INT64:
    case PROTOBUF_C_TYPE_SINT64:
    case PROTOBUF_C_TYPE_SFIXED64:
        fprintf (fp, "%" PRId64 "\n", *(int64_t *) protobuf_value);
        break;
    case PROTOBUF_C_TYPE_UINT64:
    case PROTOBUF_C_TYPE_FIXED64:
        fprintf (fp, "%" PRIu64 "\n", *(uint64_t *) protobuf_value);
        break;
    case PROTOBUF_C_TYPE_FLOAT:
        fprintf (fp, "%f\n", *(float *) protobuf_value);
        break;
    case PROTOBUF_C_TYPE_DOUBLE:
        fprintf (fp, "%e\n", *(double *) protobuf_value);
        break;
    case PROTOBUF_C_TYPE_ENUM:
        {
            const ProtobufCEnumValue *protobuf_enum_value =
                protobuf_c_enum_descriptor_get_value (field_descriptor->descriptor,
                                                      *(int *) protobuf_value);

            if (protobuf_enum_value)
            {
                fprintf (fp, "%s\n", (char *) protobuf_enum_value->name);
            }
            else
            {
                fprintf (fp, "<unknown enum value>\n");
            }
            break;
        }
    case PROTOBUF_C_TYPE_STRING:
        fprintf (fp, "%s\n", *(char **) protobuf_value);
        break;
    case PROTOBUF_C_TYPE_BYTES:
        {
            fprintf (fp, "<not printing bytes>\n");
            break;
        }
    case PROTOBUF_C_TYPE_MESSAGE:
        {
            const ProtobufCMessage **protobuf_message =
                (const ProtobufCMessage **) protobuf_value;

            fprintf (fp, "\n");
            cmsg_dump_protobuf_msg (fp, *protobuf_message, indent + 2);
            break;
        }
    default:
        fprintf (fp, "<unknown-type>\n");
    }
}

/**
 * Dump an arbitrary protobuf message to stdout in a human readable format. Useful for
 * checking if all values have been set correctly. Mostly useful for non web-facing APIs,
 * or event publishers as for web-facing APIs it is usually possible to get similar output
 * using curl or a web browser.
 * @param protobuf_message pointer to message to print
 * @param indent Number of spaces to indent message. This function is called recursively for
 *               sub-messages, which will increase this value.
 */
void
cmsg_dump_protobuf_msg (FILE *fp, const ProtobufCMessage *protobuf_message, int indent)
{
    unsigned i;

    fprintf (fp, "%*s%s:\n", indent, "", protobuf_message->descriptor->name);

    for (i = 0; i < protobuf_message->descriptor->n_fields; i++)
    {
        const ProtobufCFieldDescriptor *field_descriptor =
            protobuf_message->descriptor->fields + i;
        const void *protobuf_value =
            ((const char *) protobuf_message) + field_descriptor->offset;
        const void *protobuf_value_quantifier =
            ((const char *) protobuf_message) + field_descriptor->quantifier_offset;

        fprintf (fp, "%*s%s: ", indent + 2, "", field_descriptor->name);

        if (field_descriptor->label == PROTOBUF_C_LABEL_REQUIRED)
        {
            cmsg_dump_protobuf_value (fp, field_descriptor, protobuf_value, true,
                                      indent + 2);
        }
        else if (field_descriptor->label == PROTOBUF_C_LABEL_OPTIONAL)
        {
            protobuf_c_boolean is_set = 0;

            if (field_descriptor->flags & PROTOBUF_C_FIELD_FLAG_ONEOF)
            {
                if (*(uint32_t *) protobuf_value_quantifier != field_descriptor->id)
                {
                    fprintf (fp, "<not-this-one-of>\n");
                    continue;
                }
            }
            if (field_descriptor->type == PROTOBUF_C_TYPE_MESSAGE ||
                field_descriptor->type == PROTOBUF_C_TYPE_STRING)
            {
                if (*(const void *const *) protobuf_value)
                {
                    is_set = 1;
                }
            }
            else
            {
                if (*(const protobuf_c_boolean *) protobuf_value_quantifier)
                {
                    is_set = 1;
                }
            }

            cmsg_dump_protobuf_value (fp, field_descriptor, protobuf_value, is_set,
                                      indent + 2);

        }
        else
        { // PROTOBUF_C_LABEL_REPEATED
            const size_t *protobuf_values_count =
                (const size_t *) protobuf_value_quantifier;
            fprintf (fp, "[\n");

            if (*protobuf_values_count)
            {
                size_t value_size =
                    cmsg_protobuf_value_size_by_type (field_descriptor->type);
                if (!value_size)
                {
                    fprintf (fp, "<Can't calculate value size>\n%*s]\n", indent + 2, "");
                    continue;
                }

                unsigned j;
                for (j = 0; j < *protobuf_values_count; j++)
                {
                    const char *protobuf_value_repeated =
                        (*(char *const *) protobuf_value) + j * value_size;
                    fprintf (fp, "%*s[%u]: ", indent + 2, "", j);
                    cmsg_dump_protobuf_value (fp, field_descriptor,
                                              (const void *) protobuf_value_repeated, true,
                                              indent + 4);
                }
            }
            fprintf (fp, "%*s]\n", indent + 2, "");
        }
    }
}

#ifdef HAVE_TRACE

#define CMSG_TRACE_CONFIG "/flash/.configs/cmsg_trace"

/**
 * Convert a protobuf message plus trace metadata into a compact JSON string.
 * The returned string must be freed by the caller.
 */
static char *
cmsg_dump_protobuf_json (const ProtobufCMessage *input,
                         const char *timestamp,
                         const char *service_name, const char *method_name)
{
    json_t *trace_json = json_object ();
    char *trace_json_string = NULL;

    if (!trace_json)
    {
        return NULL;
    }

    json_object_set_new (trace_json, "timestamp", json_string (timestamp));
    json_object_set_new (trace_json, "service", json_string (service_name));
    json_object_set_new (trace_json, "method", json_string (method_name));

    if (input)
    {
        char conversion_message[200] = { 0 };
        json_t *msg_json = NULL;

        if (protobuf2json_object ((ProtobufCMessage *) input, &msg_json,
                                  conversion_message, sizeof (conversion_message)) < 0)
        {
            json_object_set_new (trace_json, "error", json_string (conversion_message));
            json_object_set_new (trace_json, "msg", json_null ());
        }
        else
        {
            json_object_set_new (trace_json, "msg", msg_json);
        }
    }
    else
    {
        json_object_set_new (trace_json, "msg", json_null ());
    }

    trace_json_string = json_dumps (trace_json, JSON_COMPACT);
    json_decref (trace_json);

    return trace_json_string;
}

/**
 * Check whether tracing is enabled for the given service name by reading
 * CMSG_TRACE_CONFIG. The file is only re-read when its mtime changes.
 * Service names may appear as standalone lines or delimited by spaces, commas,
 * tabs, or carriage-returns.
 */
static bool
cmsg_trace_service_enabled (const char *service_name)
{
    static bool initialized = false;
    static time_t last_mtime = 0;
    static char config_buf[256];
    struct stat st;
    size_t name_len;
    const char *p;

    if (stat (CMSG_TRACE_CONFIG, &st) != 0)
    {
        initialized = false;
        last_mtime = 0;
        config_buf[0] = '\0';
        return false;
    }

    if (!initialized || st.st_mtime != last_mtime)
    {
        FILE *fp = fopen (CMSG_TRACE_CONFIG, "r");
        initialized = true;
        last_mtime = st.st_mtime;
        config_buf[0] = '\0';
        if (fp)
        {
            size_t n = fread (config_buf, 1, sizeof (config_buf) - 1, fp);
            config_buf[n] = '\0';
            fclose (fp);
        }
    }

    if (config_buf[0] == '\0')
    {
        return false;
    }

    /* Search for service_name as a whole token (delimited by space, comma, newline, tab, CR) */
    name_len = strlen (service_name);
    p = config_buf;
    while ((p = strstr (p, service_name)) != NULL)
    {
        char before = (p == config_buf) ? '\0' : *(p - 1);
        char after = *(p + name_len);
        bool before_ok = (before == '\0' || before == ' ' || before == ',' ||
                          before == '\n' || before == '\t' || before == '\r');
        bool after_ok = (after == '\0' || after == ' ' || after == ',' ||
                         after == '\n' || after == '\t' || after == '\r');
        if (before_ok && after_ok)
        {
            return true;
        }
        p += name_len;
    }

    return false;
}

/**
 * If the given service is listed in CMSG_TRACE_CONFIG, append a trace record
 * for the IPC message to /tmp/<service_name>.cmsg_trace.txt.
 *
 * Trace records are emitted as one JSON object per line, for easy parsing:
 * {"timestamp":"...","service":"...","method":"...","msg":{...}}
 */
void
cmsg_trace_message (const char *service_name, const char *method_name,
                    const ProtobufCMessage *input)
{
    char trace_path[256];
    FILE *trace_fp;

    if (!cmsg_trace_service_enabled (service_name))
    {
        return;
    }

    snprintf (trace_path, sizeof (trace_path), "/tmp/%s.cmsg_trace.txt", service_name);
    trace_fp = fopen (trace_path, "a");
    if (trace_fp)
    {
        struct timespec ts;
        struct tm tm;
        char timebuf[32];
        char timestamp[48];
        char *trace_json_string = NULL;

        clock_gettime (CLOCK_REALTIME, &ts);
        localtime_r (&ts.tv_sec, &tm);
        strftime (timebuf, sizeof (timebuf), "%Y-%m-%dT%H:%M:%S", &tm);
        snprintf (timestamp, sizeof (timestamp), "%s.%03ld", timebuf, ts.tv_nsec / 1000000);

        trace_json_string = cmsg_dump_protobuf_json (input, timestamp,
                                                     service_name, method_name);
        if (trace_json_string)
        {
            fprintf (trace_fp, "%s\n", trace_json_string);
            free (trace_json_string);
        }
        fclose (trace_fp);
    }
}

#endif /* HAVE_TRACE */
