/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#include "cmsg.h"
#include "cmsg_private.h"
#include "cmsg_error.h"

#define CMSG_REPEATED_BLOCK_SIZE 64

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
    CMSG_DEBUG (CMSG_INFO, "%s", output_str);
#endif
}

/**
 * Creates the header depending upon the msg_type.
 *
 * Adds sub headers as appropriate and returns header in network byte order
 */
cmsg_header
cmsg_header_create (cmsg_msg_type msg_type, uint32_t extra_header_size,
                    uint32_t packed_size, cmsg_status_code status_code)
{
    cmsg_header header;
    uint32_t header_len = sizeof (cmsg_header) + extra_header_size;

    header.msg_type = (cmsg_msg_type) htonl ((uint32_t) msg_type);
    header.message_length = htonl (packed_size);
    header.header_length = htonl (header_len);
    header.status_code = (cmsg_status_code) htonl ((uint32_t) status_code);

    return header;
}

/**
 * Creates CMSG TLV header
 */
void
cmsg_tlv_method_header_create (uint8_t *buf, cmsg_header header, uint32_t type,
                               uint32_t length, const char *method_name)
{
    uint32_t hton_type = htonl (type);
    uint32_t hton_length = htonl (length);

    memcpy ((void *) buf, &header, sizeof (header));

    uint8_t *buffer_tlv_data_type = buf + sizeof (header);

    memcpy (buffer_tlv_data_type, &hton_type, sizeof (hton_type));

    uint8_t *buffer_tlv_data_length = buffer_tlv_data_type + sizeof (hton_type);

    memcpy (buffer_tlv_data_length, &hton_length, sizeof (hton_length));

    uint8_t *buffer_tlv_data_method = buffer_tlv_data_length + sizeof (hton_length);
    strncpy ((char *) buffer_tlv_data_method, method_name, length);

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
    header_converted->status_code =
        (cmsg_status_code) ntohl ((uint32_t) header_received->status_code);

    CMSG_DEBUG (CMSG_INFO, "[TRANSPORT] received header\n");
    cmsg_buffer_print ((void *) &header_received, sizeof (cmsg_header));

    CMSG_DEBUG (CMSG_INFO,
                "[TRANSPORT] msg_type host: %d, wire: %d\n",
                header_converted->msg_type, header_received->msg_type);

    CMSG_DEBUG (CMSG_INFO,
                "[TRANSPORT] header_length host: %d, wire: %d\n",
                header_converted->header_length, header_received->header_length);

    CMSG_DEBUG (CMSG_INFO,
                "[TRANSPORT] message_length host: %d, wire: %d\n",
                header_converted->message_length, header_received->message_length);

    CMSG_DEBUG (CMSG_INFO,
                "[TRANSPORT] method_index   host: %d, wire: %d\n",
                header_converted->method_index, header_received->method_index);

    CMSG_DEBUG (CMSG_INFO,
                "[TRANSPORT] status_code host: %d, wire: %d\n",
                header_converted->status_code, header_received->status_code);

    // Check the data for correctness
    switch (header_converted->msg_type)
    {
    case CMSG_MSG_TYPE_METHOD_REQ:
    case CMSG_MSG_TYPE_METHOD_REPLY:
    case CMSG_MSG_TYPE_ECHO_REQ:
    case CMSG_MSG_TYPE_ECHO_REPLY:
    case CMSG_MSG_TYPE_CONN_OPEN:
        // Known values
        break;

    default:
        // Unknown msg type
        CMSG_LOG_GEN_ERROR ("Processing header, bad msg type value - %d",
                            header_converted->msg_type);
        return CMSG_RET_ERR;
        break;
    }

    return CMSG_RET_OK;
}


/**
 * Process the tlv header(s). Performs error checking on the received tlv header(s), and
 * processes the information sent in them.
 */
int
cmsg_tlv_header_process (uint8_t *buf, cmsg_server_request *server_request,
                         uint32_t extra_header_size,
                         const ProtobufCServiceDescriptor *descriptor)
{
    cmsg_tlv_method_header *tlv_method_header;
    cmsg_tlv_header *tlv_header;

    /* If there is no tlv header, we have nothing to process */
    if (extra_header_size == 0)
    {
        return CMSG_RET_OK;
    }

    /* Make sure that there is at least enough extra_header_size for the minimum tlv
     * header */
    while (extra_header_size >= CMSG_TLV_SIZE (0))
    {
        tlv_header = (cmsg_tlv_header *) buf;
        tlv_header->type = ntohl (tlv_header->type);
        tlv_header->tlv_value_length = ntohl (tlv_header->tlv_value_length);

        /* Make sure there is enough extra_header_size for the entire tlv header */
        if (extra_header_size >= CMSG_TLV_SIZE (tlv_header->tlv_value_length))
        {
            switch (tlv_header->type)
            {
            case CMSG_TLV_METHOD_TYPE:
                tlv_method_header = (cmsg_tlv_method_header *) buf;

                server_request->method_index =
                    protobuf_c_service_descriptor_get_method_index_by_name
                    (descriptor, tlv_method_header->method);
                /*
                 * It is possible that we could receive a method that we do not know. In
                 * this case, there is nothing we can do to process the message. We need to
                 * reply to the client to unblock it (if the transport is two-way).
                 * Therefore, we overwrite the msg_type, and return
                 * CMSG_RET_METHOD_NOT_FOUND.
                 */
                if (!(IS_METHOD_DEFINED (server_request->method_index)))
                {
                    CMSG_LOG_GEN_INFO ("Undefined Method - %s", tlv_method_header->method);
                    return CMSG_RET_METHOD_NOT_FOUND;
                }

                strncpy (server_request->method_name_recvd, tlv_method_header->method,
                         MIN (tlv_method_header->method_length,
                              CMSG_SERVER_REQUEST_MAX_NAME_LENGTH));
                break;

            default:
                CMSG_LOG_GEN_ERROR ("Processing TLV header, bad TLV type value - %d",
                                    tlv_header->type);
                return CMSG_RET_ERR;
            }
        }

        buf = buf + CMSG_TLV_SIZE (tlv_header->tlv_value_length);
        extra_header_size =
            extra_header_size - CMSG_TLV_SIZE (tlv_header->tlv_value_length);
    }

    /* At this point, there should be no extra_header_size left over. If there is, this
     * is a problem that we should track. */
    if (extra_header_size != 0)
    {
        CMSG_LOG_GEN_ERROR ("Finished processing TLV header, %u bytes unused",
                            extra_header_size);
        return CMSG_RET_ERR;
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

const char *
cmsg_service_name_get (const ProtobufCServiceDescriptor *descriptor)
{
    return descriptor->name;
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
    {
        return p;
    }

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
    {
        return p;
    }

    return NULL;
}

int
cmsg_asprintf (const char *filename, int line, char **strp, const char *fmt, ...)
{
    int ret;
    va_list ap;

    va_start (ap, fmt);

    ret = vasprintf (strp, fmt, ap);

#ifndef LOCAL_INSTALL
    if (cmsg_mtype > 0)
    {
        g_mem_record_alloc (*strp, cmsg_mtype, filename, line);
    }
#endif

    va_end (ap);
    return ret;
}

void *
cmsg_realloc (void *ptr, size_t size, const char *filename, int line)
{
    void *p = NULL;

    if (cmsg_mtype > 0)
    {
        /* if realloc fails it returns NULL and ptr is left unchanged.  However, we have
         * already marked it as freed.  If realloc fails, we have bigger problems, so
         * just ignore that case.
         */
        g_mem_record_free (ptr, cmsg_mtype, filename, line);
    }
    p = realloc (ptr, size);
    if (cmsg_mtype > 0)
    {
        g_mem_record_alloc (p, cmsg_mtype, filename, line);
    }

    return p;
}

void
cmsg_free (void *ptr, const char *filename, int line)
{
    if (ptr == NULL)
    {
        return;
    }

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

/**
 * Allocates a single piece of memory to hold two arrays: an array of message
 * structs and an array of pointers to these structs. Sets up the pointer array
 * so it's ready for use and returns this.
 * @note You should use CMSG_MSG_ARRAY_ALLOC() rather than calling this function
 * directly
 * @returns an array of message pointers (the actual message structs these
 * pointers reference follows after, on the same block of malloc'd memory).
 */
void **
cmsg_msg_array_alloc (size_t struct_size, uint32_t num_structs, const char *file, int line)
{
    void *mem_block;
    void **ptr_array;
    void *struct_array;
    size_t total_ptr_size;
    size_t total_struct_size;
    int i;

    /* we need to allocate memory to hold all the CMSG message structs, as well
     * as pointers to the structs. CMSG messages may only keep a single pointer
     * to this data, so allocate it all in one block so that we can safely free
     * it after the cmsg has been sent */
    total_struct_size = (struct_size * num_structs);
    total_ptr_size = (sizeof (void *) * num_structs);

    /* we want to use the file/line of the caller code for memory diagnostics,
     * so call cmsg_malloc() directly here rather than using CMSG_MALLOC() */
    mem_block = cmsg_malloc (total_struct_size + total_ptr_size, file, line);

    /* Setup the memory. We'll return the pointer array, so this is the first
     * piece of memory, and the array of structs will go after it */
    ptr_array = mem_block;
    struct_array = mem_block + total_ptr_size;

    /* update each pointer so it points to the corresponding message struct.
     * The first entry just points to the start of the struct array, the next
     * entry is one struct-size further on in memory, and so on */
    for (i = 0; i < num_structs; i++)
    {
        ptr_array[i] = struct_array + (i * struct_size);
    }

    return ptr_array;
}

/**
 * Frees a message array allocated by cmsg_msg_array_alloc()
 * @note You should use CMSG_MSG_ARRAY_FREE() rather than calling this function
 * directly
 */
void
cmsg_msg_array_free (void *msg_array, const char *file, int line)
{
    cmsg_free (msg_array, file, line);
}

/**
 * If ptr is non-null, this function uses realloc to increase the length of the passed in
 * pointer array by 1 and sets the last element to point to the passed in ptr. The
 * num_elems field is also incremented if this is done.  If reallocation fails or ptr is
 * NULL, the original array is returned untouched.  Can be called when no elements are in
 * the array yet.
 * This function is designed to be called by CMSG_REPEATED_APPEND
 * @param msg_ptr_array pointer to array of pointers to elements for a repeated field
 * @param num_elems Pointer to field containing number of pointers stored in msg_ptr_array.
 *        This is updated if the number is changed.
 * @param ptr pointer to add at the end of the array.  If this is NULL, this function
 *        does nothing.
 * @param file file this is being called from
 * @param line this is being called from.
 * @returns the new pointer to the repeated array and updates *num_elems with the new number
 * of elements. The returned pointer can be freed with CMSG_ARRAY_FREE but freeing the
 * contents of the elements it points to is left up to the caller.
 */
void
cmsg_repeated_append (void ***msg_ptr_array, size_t *num_elems, const void *ptr,
                      const char *file, int line)
{
    void **new_array_ptr = NULL;
    size_t new_size;

    if (!ptr)
    {
        return;
    }

    /* Optimization to reduce reallocations. Allocate a block of pointers
     * and use until exhausted, rather than reallocating for every append.
     * Allocate every time num_elems % allocation block size is 0.
     */
    if ((*num_elems % CMSG_REPEATED_BLOCK_SIZE) == 0)
    {
        new_size = (*num_elems + CMSG_REPEATED_BLOCK_SIZE) * sizeof (void *);
        new_array_ptr = cmsg_realloc (*msg_ptr_array, new_size, file, line);
    }
    else
    {
        /* We have previously allocated space we can use */
        new_array_ptr = *msg_ptr_array;
    }

    if (new_array_ptr)
    {
        /* Add new element to array and increment number of elements */
        new_array_ptr[(*num_elems)++] = (void *) ptr;
        *msg_ptr_array = new_array_ptr;
    }
}

static void *
cmsg_memory_alloc (void *allocator_data, size_t size)
{
    return CMSG_MALLOC (size);
}

static void
cmsg_memory_free (void *allocator_data, void *data)
{
    CMSG_FREE (data);
}

/**
 * The memory allocator CMSG uses with the protobuf-c library.
 * This is done so that memory usage in the protobuf-c library
 * can be tracked.
 */
ProtobufCAllocator cmsg_memory_allocator = {
    .alloc = &cmsg_memory_alloc,
    .free = &cmsg_memory_free,
    .allocator_data = NULL,
};

#ifdef HAVE_CMSG_PROFILING
uint32_t
cmsg_prof_diff_time_in_us (struct timeval start, struct timeval end)
{
    uint32_t elapsed_us;
    elapsed_us = (end.tv_sec - start.tv_sec) * 1000000;
    elapsed_us += end.tv_usec;
    elapsed_us -= start.tv_usec;
    return elapsed_us;
}

void
cmsg_prof_time_tic (cmsg_prof *prof)
{
    if (!prof)
    {
        return;
    }

    if (!prof->enable)
    {
        return;
    }

    gettimeofday (&prof->start_tic, NULL);
}

uint32_t
cmsg_prof_time_toc (cmsg_prof *prof)
{
    if (!prof)
    {
        return 0;
    }

    if (!prof->enable)
    {
        return 0;
    }

    gettimeofday (&prof->now, NULL);
    return cmsg_prof_diff_time_in_us (prof->start_tic, prof->now);
}

void
cmsg_prof_time_log_start (cmsg_prof *prof, char *filename)
{
    if (!prof || !filename)
    {
        return;
    }

    if (!prof->enable)
    {
        return;
    }

    if (!prof->file_ptr)
    {
        prof->file_ptr = fopen (filename, "w");
        if (!prof->file_ptr)
        {
            CMSG_LOG_GEN_ERROR ("couldn't open file: %s", filename);
        }
    }

    prof->text_ptr = (char *) prof->text;

    gettimeofday (&prof->start, NULL);
}

void
cmsg_prof_time_log_add_time (cmsg_prof *prof, char *description, uint32_t time)
{
    if (!prof || !description)
    {
        return;
    }

    if (!prof->enable)
    {
        return;
    }

    if (prof->text_ptr)
    {
        prof->text_ptr += sprintf (prof->text_ptr, "[%s]%d;", description, time);
    }
}

void
cmsg_prof_time_log_stop (cmsg_prof *prof, char *type, int msg_size)
{
    if (!prof)
    {
        return;
    }

    if (!prof->enable)
    {
        return;
    }

    gettimeofday (&prof->now, NULL);
    uint32_t elapsed_us = cmsg_prof_diff_time_in_us (prof->start, prof->now);

    if (prof->file_ptr)
    {
        fprintf (prof->file_ptr, "%s[type]%s;[size]%d;[total]%d;\n", prof->text, type,
                 msg_size, elapsed_us);
    }

    //when do we close the file?
}

void
cmsg_prof_enable (cmsg_prof *prof)
{
    if (!prof)
    {
        return;
    }

    prof->enable = 1;
}

void
cmsg_prof_disable (cmsg_prof *prof)
{
    if (!prof)
    {
        return;
    }

    prof->enable = 0;
}
#endif //HAVE_CMSG_PROFILING
