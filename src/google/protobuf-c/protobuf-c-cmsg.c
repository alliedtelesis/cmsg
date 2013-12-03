#include "protobuf-c-cmsg.h"


static int cmsg_mtype = 0;

uint32_t
cmsg_common_uint32_to_le (uint32_t le)
{
#if IS_LITTLE_ENDIAN
    return le;
#else
    return (le << 24) | (le >> 24) | ((le >> 8) & 0xff00) | ((le << 8) & 0xff0000);
#endif
}

void
cmsg_buffer_print (void *buffer, uint32_t size)
{
#if DEBUG_BUFFER
    char output_str[1024 * 4];
    char *output_ptr = (char *) &output_str;
    output_ptr += sprintf (output_ptr, "[Buffer] #################################\n");
    output_ptr += sprintf (output_ptr, "[Buffer] %d bytes of data %p\n", size, buffer);

    if (buffer)
    {
        char *buf = (char *) buffer;
        int i;
        char buf_str[512];
        char *buf_ptr = (char *) &buf_str;
        int line_count = 0;
        int line_length = 8;
        int offset = 0;

        if (size > 512)
        {
            size = 512;
            output_ptr += sprintf (output_ptr,
                                   "[Buffer] warning buffer bigger than 512 throwing shit away (scott)\n");
        }

        output_ptr += sprintf (output_ptr, "[Buffer] 00 01 02 03 04 05 06 07    offset\n");
        output_ptr += sprintf (output_ptr, "[Buffer] ---------------------------------\n");

        for (i = 0; i < size; i++)
        {
            buf_ptr += sprintf (buf_ptr, "%02X ", (uint8_t) buf[i]);
            line_count++;
            if (line_count >= line_length)
            {
                *(buf_ptr - 1) = 0;
                output_ptr += sprintf (output_ptr,
                                       "[Buffer] %s    %06X\n", buf_str, offset);

                //reset pointer to start again
                buf_ptr = (char *) &buf_str;
                line_count = 0;
                offset += line_length;
            }
        }
        //print last line add spaces
        if (line_count != 0)
        {
            for (i = line_count; i < line_length; i++)
                buf_ptr += sprintf (buf_ptr, "   ");

            *(buf_ptr - 1) = 0;
            output_ptr += sprintf (output_ptr, "[Buffer] %s    %06X\n", buf_str, offset);
        }
    }
    else
    {
        output_ptr += sprintf (output_ptr, "[Buffer] buffer is NULL\n");
    }

    output_ptr += sprintf (output_ptr, "[Buffer] #################################\n");
    DEBUG (CMSG_INFO, "%s", output_str);
#endif
}


/**
 * Creates the header depending upon the msg_type.
 *
 * Adds sub headers as appropriate and returns header in network byte order
 */
cmsg_header
cmsg_header_create (cmsg_msg_type msg_type, uint32_t packed_size, uint32_t method_index,
                    cmsg_status_code status_code)
{
    cmsg_header header;

    header.msg_type = (cmsg_msg_type) htonl ((uint32_t) msg_type);
    header.message_length = htonl (packed_size);
    header.header_length = htonl (sizeof (cmsg_header));
    header.method_index = htonl (method_index);
    header.status_code = (cmsg_status_code) htonl ((uint32_t) status_code);

    return header;
}

/**
 * Converts the header received into something we know about, does data checking
 * and converts from network byte order to host.
 */
int32_t
cmsg_header_process (cmsg_header *header_received, cmsg_header *header_converted)
{
    //we have network byte order on the wire
    header_converted->msg_type =
        (cmsg_msg_type) ntohl ((uint32_t) header_received->msg_type);
    header_converted->header_length = ntohl (header_received->header_length);
    header_converted->message_length = ntohl (header_received->message_length);
    header_converted->method_index = ntohl (header_received->method_index);
    header_converted->status_code =
        (cmsg_status_code) ntohl ((uint32_t) header_received->status_code);

    DEBUG (CMSG_INFO, "[TRANSPORT] received header\n");
    cmsg_buffer_print ((void *) &header_received, sizeof (cmsg_header));

    DEBUG (CMSG_INFO,
           "[TRANSPORT] msg_type host: %d, wire: %d\n",
           header_converted->msg_type, header_received->msg_type);

    DEBUG (CMSG_INFO,
           "[TRANSPORT] header_length host: %d, wire: %d\n",
           header_converted->header_length, header_received->header_length);

    DEBUG (CMSG_INFO,
           "[TRANSPORT] message_length host: %d, wire: %d\n",
           header_converted->message_length, header_received->message_length);

    DEBUG (CMSG_INFO,
           "[TRANSPORT] method_index   host: %d, wire: %d\n",
           header_converted->method_index, header_received->method_index);

    DEBUG (CMSG_INFO,
           "[TRANSPORT] status_code host: %d, wire: %d\n",
           header_converted->status_code, header_received->status_code);

    // Check the data for correctness
    switch (header_converted->msg_type)
    {
    case CMSG_MSG_TYPE_METHOD_REQ:
    case CMSG_MSG_TYPE_METHOD_REPLY:
    case CMSG_MSG_TYPE_ECHO_REQ:
    case CMSG_MSG_TYPE_ECHO_REPLY:
        // Known values
        break;

    default:
        // Unknown msg type
        CMSG_LOG_ERROR ("Processing header, bad msg type value - %d",
                        header_converted->msg_type);
        return CMSG_RET_ERR;
        break;
    }

    return CMSG_RET_OK;
}

int
cmsg_service_port_get (const char *name, const char *proto)
{
    struct servent *result;
    struct servent result_buf;
    int port, ret;

    const int buf_size = 1024;
    char buf[buf_size];

    ret = getservbyname_r (name, proto, &result_buf, buf, buf_size, &result);
    if (result == NULL || ret != 0)
    {
        return 0;
    }

    port = ntohs (result->s_port);

    return port;
}

void *
cmsg_malloc (size_t size, const char *filename, int line)
{
    void *p = NULL;

    p = malloc (size);

#ifndef LOCAL_INSTALL
    if (cmsg_mtype > 0)
    {
        g_mem_record_alloc (p, cmsg_mtype, filename, line);
    }
#endif

    if (p || size == 0)
        return p;

    return NULL;
}

void *
cmsg_calloc (size_t nmemb, size_t size, const char *filename, int line)
{
    void *p = NULL;

    p = calloc (nmemb, size);

#ifndef LOCAL_INSTALL
    if (cmsg_mtype > 0)
    {
        g_mem_record_alloc (p, cmsg_mtype, filename, line);
    }
#endif

    if (p || size == 0)
        return p;

    return NULL;
}

void
cmsg_free (void *ptr, const char *filename, int line)
{
    if (ptr == NULL)
        return;

#ifndef LOCAL_INSTALL
    if (cmsg_mtype > 0)
    {
        g_mem_record_free (ptr, cmsg_mtype, filename, line);
    }
#endif

    free (ptr);
}

void
cmsg_malloc_init (int mtype)
{
    cmsg_mtype = mtype;
}
