/*
 * Copyright 2016, Allied Telesis Labs New Zealand, Ltd
 */
#include "cmsg.h"
#include "cmsg_private.h"
#include "cmsg_error.h"
#include <gmem_diag.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define CMSG_REPEATED_BLOCK_SIZE 64

static int cmsg_mtype = 0;

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
    cmsg_tlv_header_type tlv_type;
    uint32_t tlv_total_length;

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
        tlv_type = ntohl (tlv_header->type);
        tlv_total_length = CMSG_TLV_SIZE (ntohl (tlv_header->tlv_value_length));

        /* Make sure there is enough extra_header_size for the entire tlv header */
        if (extra_header_size >= tlv_total_length)
        {
            switch (tlv_type)
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
                                    tlv_type);
                return CMSG_RET_ERR;
            }

            buf = buf + tlv_total_length;
            extra_header_size = extra_header_size - tlv_total_length;
        }
        else
        {
            /* TLV value size is longer than the header size so cannot be
             * correct.
             * Prevent integer overflows by not processing anymore of the
             * header and returning an error.
             */
            CMSG_LOG_GEN_ERROR
                ("Unable to process TLV header, %u tlv length is longer than the remaining header size %u",
                 tlv_total_length, extra_header_size);
            return CMSG_RET_ERR;
        }
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

uint16_t
cmsg_service_port_get (const char *name, const char *proto)
{
    struct servent *result;
    struct servent result_buf;
    uint16_t port;
    int ret;

    const int buf_size = 1024;
    char buf[buf_size];

    ret = getservbyname_r (name, proto, &result_buf, buf, buf_size, &result);
    if (result == NULL || ret != 0)
    {
        char *errstr = strerror (errno);
        CMSG_LOG_GEN_ERROR ("getservbyname_r(%s/%s) failure: %s", name, proto, errstr);
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

    if (cmsg_mtype > 0)
    {
        g_mem_record_alloc (p, cmsg_mtype, filename, line);
    }

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

    if (cmsg_mtype > 0)
    {
        g_mem_record_alloc (p, cmsg_mtype, filename, line);
    }

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

    if (cmsg_mtype > 0)
    {
        g_mem_record_alloc (*strp, cmsg_mtype, filename, line);
    }

    va_end (ap);
    return ret;
}

char *
cmsg_strdup (const char *strp, const char *filename, int line)
{
    char *p = strdup (strp);

    if (cmsg_mtype > 0)
    {
        g_mem_record_alloc (p, cmsg_mtype, filename, line);
    }

    return p;
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
_cmsg_free (void *ptr, const char *filename, int line)
{
    if (ptr == NULL)
    {
        return;
    }

    if (cmsg_mtype > 0)
    {
        g_mem_record_free (ptr, cmsg_mtype, filename, line);
    }

    free (ptr);
}

void
cmsg_free (void *ptr)
{
    CMSG_FREE (ptr);
}

void
cmsg_malloc_init (int mtype)
{
    cmsg_mtype = mtype;
}

/**
 * Allocates a zeroed single message struct, but does not allocate memory for
 * any sub-fields.  It is up to the user to call the appropriate init function for the struct.
 * @note You should use CMSG_MSG_ALLOC() rather than calling this function directly
 * @returns a pointer to the allocated message.
 */
void *
cmsg_msg_alloc (size_t struct_size, const char *file, int line)
{
    return cmsg_calloc (struct_size, 1, file, line);
}

/**
 * Frees a message struct allocated by cmsg_msg_alloc()
 * @note You should use CMSG_MSG_FREE() rather than calling this function directly
 */
void
cmsg_msg_free (void *msg_array, const char *file, int line)
{
    _cmsg_free (msg_array, file, line);
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
    _cmsg_free (msg_array, file, line);
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
 * of elements. The returned pointer can be freed with CMSG_MSG_ARRAY_FREE but freeing the
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

/**
 * This function uses realloc to increase the length of the passed in
 * uint32_t array by 1 and sets the last element to point to the passed in value. The
 * num_elems field is also incremented if this is done.  If reallocation fails,
 * the original array is returned untouched.  Can be called when no elements are in
 * the array yet.
 * This function is designed to be called by CMSG_REPEATED_APPEND_UINT32
 * @param msg_ptr_array pointer to array of pointers to elements for a repeated field
 * @param num_elems Pointer to field containing number of pointers stored in msg_ptr_array.
 *        This is updated if the number is changed.
 * @param value value to add at the end of the array.
 * @param file file this is being called from
 * @param line this is being called from.
 * @returns the new pointer to the repeated array and updates *num_elems with the new number
 * of elements. The returned pointer can be freed with CMSG_MSG_ARRAY_FREE.
 */
void
cmsg_repeated_append_uint32 (uint32_t **msg_ptr_array, size_t *num_elems,
                             uint32_t value, const char *file, int line)
{
    uint32_t *new_array_ptr = NULL;
    size_t new_size;

    /* Optimization to reduce reallocations. Allocate a block of pointers
     * and use until exhausted, rather than reallocating for every append.
     * Allocate every time num_elems % allocation block size is 0.
     */
    if ((*num_elems % CMSG_REPEATED_BLOCK_SIZE) == 0)
    {
        new_size = (*num_elems + CMSG_REPEATED_BLOCK_SIZE) * sizeof (uint32_t);
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
        new_array_ptr[(*num_elems)++] = value;
        *msg_ptr_array = new_array_ptr;
    }
}

/**
 * This function uses realloc to increase the length of the passed in
 * int32_t array by 1 and sets the last element to point to the passed in value. The
 * num_elems field is also incremented if this is done.  If reallocation fails,
 * the original array is returned untouched.  Can be called when no elements are in
 * the array yet.
 * This function is designed to be called by CMSG_REPEATED_APPEND_INT32
 * @param msg_ptr_array pointer to array of pointers to elements for a repeated field
 * @param num_elems Pointer to field containing number of pointers stored in msg_ptr_array.
 *        This is updated if the number is changed.
 * @param value value to add at the end of the array.
 * @param file file this is being called from
 * @param line this is being called from.
 * @returns the new pointer to the repeated array and updates *num_elems with the new number
 * of elements. The returned pointer can be freed with CMSG_MSG_ARRAY_FREE.
 */
void
cmsg_repeated_append_int32 (int32_t **msg_ptr_array, size_t *num_elems,
                            int32_t value, const char *file, int line)
{
    int32_t *new_array_ptr = NULL;
    size_t new_size;

    /* Optimization to reduce reallocations. Allocate a block of pointers
     * and use until exhausted, rather than reallocating for every append.
     * Allocate every time num_elems % allocation block size is 0.
     */
    if ((*num_elems % CMSG_REPEATED_BLOCK_SIZE) == 0)
    {
        new_size = (*num_elems + CMSG_REPEATED_BLOCK_SIZE) * sizeof (int32_t);
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
        new_array_ptr[(*num_elems)++] = value;
        *msg_ptr_array = new_array_ptr;
    }
}

/**
 * Free the contents of a string field in a received message, recording that it has been freed.
 * Then duplicate and record the allocation of the passed in string and set it in the message.
 * Should be called using CMSG_UPDATE_RECV_MSG_STRING_FIELD macro.
 * @param field Field that needs to be updated
 * @param new_val string to put into the field
 * @param file Calling file
 * @param line Calling line
 */
void
cmsg_update_recv_msg_string_field (char **field, const char *new_val,
                                   const char *file, int line)
{
    char *strp = NULL;
    _cmsg_free (*field, file, line);

    if (new_val)
    {
        strp = cmsg_strdup (new_val, file, line);
    }

    *field = strp;
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

/**
 * Trying to set cmsg thread name
 *
 * Takes a name which would normally be a descriptor service name
 * prefix is used for types of services.
 */
void
cmsg_pthread_setname (pthread_t thread, const char *cmsg_name, const char *prefix)
{
#define NAMELEN 16
    char thread_name[NAMELEN];

    snprintf (thread_name, NAMELEN, "%s%.*s", prefix,
              (int) (NAMELEN - strlen (prefix) - 1), cmsg_name ? : "cmsg");

    pthread_setname_np (thread, thread_name);
}

/**
 * Helper function for serialising a protobuf message to bytes.
 *
 * @param msg - The protobuf message to serialise.
 * @param packed_data_size - Pointer to store the number of bytes used to serialise
 *                           the message.
 *
 * @returns Pointer to the serialised data on success, NULL otherwise. Note that the
 *          serialised data must be freed by the caller.
 */
static uint8_t *
cmsg_pack_msg (const ProtobufCMessage *msg, uint32_t *packed_data_size)
{
    uint8_t *packed_data = NULL;
    uint32_t message_size = 0;
    uint32_t ret;

    message_size = protobuf_c_message_get_packed_size (msg);
    packed_data = (uint8_t *) calloc (1, message_size);

    ret = protobuf_c_message_pack (msg, packed_data);
    if (ret < message_size)
    {
        CMSG_LOG_GEN_ERROR ("Underpacked message data. Packed %d of %d bytes.", ret,
                            message_size);
        free (packed_data);
        return NULL;
    }
    else if (ret > message_size)
    {
        CMSG_LOG_GEN_ERROR ("Overpacked message data. Packed %d of %d bytes.", ret,
                            message_size);
        free (packed_data);
        return NULL;
    }

    *packed_data_size = message_size;
    return packed_data;
}

/**
 * Serialises the given protobuf message to bytes and writes these to
 * the given file name.
 *
 * @param msg - The protobuf message to serialise and dump to a file.
 * @param file_name - The name/path of the file to dump the message to.
 *
 * @returns CMSG_RET_OK on success, CMSG_RET_ERR otherwise.
 */
int32_t
cmsg_dump_msg_to_file (const ProtobufCMessage *msg, const char *file_name)
{
    FILE *fp;
    uint8_t *packed_data = NULL;
    uint32_t packed_data_len = 0;
    size_t len;
    char *tmp_file_name = NULL;
    int32_t ret = CMSG_RET_ERR;

    if (CMSG_ASPRINTF (&tmp_file_name, "%s.tmp", file_name) < 0)
    {
        CMSG_LOG_GEN_ERROR ("Failed to allocate temporary file name string.");
        return CMSG_RET_ERR;
    }

    fp = fopen (tmp_file_name, "w");
    if (!fp)
    {
        CMSG_LOG_GEN_ERROR ("Failed to open temporary file.");
        free (tmp_file_name);
        return CMSG_RET_ERR;
    }

    packed_data = cmsg_pack_msg (msg, &packed_data_len);
    if (packed_data)
    {
        len = fwrite (packed_data, sizeof (*packed_data), packed_data_len, fp);
        if (len == packed_data_len)
        {
            ret = CMSG_RET_OK;
        }
        else
        {
            CMSG_LOG_GEN_ERROR
                ("Failed to dump message data (expected = %u, written = %zu).",
                 packed_data_len, len);
        }

        free (packed_data);
    }

    fclose (fp);
    rename (tmp_file_name, file_name);
    free (tmp_file_name);

    return ret;
}

/**
 * Reads a serialised protobuf message from a file, unserialises it and returns it
 * to the caller.
 *
 * @param desc - The ProtobufCMessageDescriptor of the message in the file.
 * @param file_name - The name/path of the file to read the message from.
 *
 * @returns Pointer to the message on success, NULL otherwise. This message must be freed
 *          by the caller using CMSG_FREE_RECV_MSG.
 */
ProtobufCMessage *
cmsg_get_msg_from_file (const ProtobufCMessageDescriptor *desc, const char *file_name)
{
    ProtobufCMessage *message = NULL;
    ProtobufCAllocator *allocator = &cmsg_memory_allocator;
    struct stat file_info;
    uint8_t *packed_data = NULL;
    FILE *fp;
    size_t len;

    if (stat (file_name, &file_info) != 0)
    {
        return NULL;
    }

    fp = fopen (file_name, "r");
    if (!fp)
    {
        CMSG_LOG_GEN_ERROR ("Failed to open file %s", file_name);
        return NULL;
    }

    packed_data = malloc (file_info.st_size);
    if (!packed_data)
    {
        CMSG_LOG_GEN_ERROR ("Failed to allocate memory");
        fclose (fp);
        return NULL;
    }

    len = fread (packed_data, sizeof (*packed_data), file_info.st_size, fp);
    if (len != file_info.st_size)
    {
        CMSG_LOG_GEN_ERROR
            ("Failed to read packed message data (expected = %u, read = %zu).",
             (uint32_t) file_info.st_size, len);
        free (packed_data);
        fclose (fp);
        return NULL;
    }
    fclose (fp);

    message = protobuf_c_message_unpack (desc, allocator, len, packed_data);
    if (!message)
    {
        CMSG_LOG_GEN_ERROR ("Failed to unpack message");
        free (packed_data);
        return NULL;
    }
    free (packed_data);

    return message;
}
