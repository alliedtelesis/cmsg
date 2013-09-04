#include "protobuf-c-cmsg.h"


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
cmsg_buffer_print (void *buffer, unsigned int size)
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

cmsg_header_request
cmsg_request_header_create (uint32_t method_index, uint32_t packed_size,
                            uint32_t request_id)
{
    cmsg_header_request header;
    header.method_index = cmsg_common_uint32_to_le (method_index);
    header.message_length = cmsg_common_uint32_to_le (packed_size);
    header.request_id = request_id;

    return header;
}
