/**
 * cmsg_pub.c
 *
 * Implements the CMSG publisher which can be used to publish messages
 * to interested subscribers.
 *
 * Copyright 2019, Allied Telesis Labs New Zealand, Ltd
 */

#include "cmsg_pub.h"
#include "cmsg_pss_api_private.h"
#include "cmsg_error.h"

struct cmsg_publisher
{
    //this is a hack to get around a check when a client method is called
    //to not change the order of the first two
    const ProtobufCServiceDescriptor *descriptor;
    int32_t (*invoke) (ProtobufCService *service,
                       uint32_t method_index,
                       const ProtobufCMessage *input,
                       ProtobufCClosure closure, void *closure_data);
    cmsg_client *client;
    cmsg_object self;
    cmsg_object parent;
};

/**
 * Invoke function for the cmsg publisher. Simply creates the cmsg packet
 * for the given message and sends this to cmsg_pssd to be published to all
 * subscribers.
 */
static int32_t
cmsg_pub_invoke (ProtobufCService *service,
                 uint32_t method_index,
                 const ProtobufCMessage *input,
                 ProtobufCClosure closure, void *closure_data)
{
    bool ret;
    int result;
    cmsg_publisher *publisher = (cmsg_publisher *) service;
    const char *method_name;
    uint8_t *packet = NULL;
    uint32_t total_message_size = 0;
    const char *service_name = NULL;

    CMSG_ASSERT_RETURN_VAL (service != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (service->descriptor != NULL, CMSG_RET_ERR);
    CMSG_ASSERT_RETURN_VAL (input != NULL, CMSG_RET_ERR);

    method_name = service->descriptor->methods[method_index].name;

    result = cmsg_client_create_packet (publisher->client, method_name, input,
                                        &packet, &total_message_size);
    if (result != CMSG_RET_OK)
    {
        return CMSG_RET_ERR;
    }

    service_name = cmsg_service_name_get (service->descriptor);
    ret = cmsg_pss_publish_message (publisher->client, service_name, method_name,
                                    packet, total_message_size);
    CMSG_FREE (packet);

    return (ret ? CMSG_RET_OK : CMSG_RET_ERR);
}

/**
 * Create a cmsg publisher for the given service.
 *
 * @param service - The service to create the publisher for.
 *
 * @returns A pointer to the publisher on success, NULL otherwise.
 */
cmsg_publisher *
cmsg_publisher_create (const ProtobufCServiceDescriptor *service)
{
    CMSG_ASSERT_RETURN_VAL (service != NULL, NULL);

    cmsg_publisher *publisher = (cmsg_publisher *) CMSG_CALLOC (1, sizeof (*publisher));
    if (!publisher)
    {
        CMSG_LOG_GEN_ERROR ("[%s] Unable to create publisher.", service->name);
        return NULL;
    }

    publisher->self.object_type = CMSG_OBJ_TYPE_PUB;
    publisher->self.object = publisher;
    strncpy (publisher->self.obj_id, service->name, CMSG_MAX_OBJ_ID_LEN);

    publisher->parent.object_type = CMSG_OBJ_TYPE_NONE;
    publisher->parent.object = NULL;

    publisher->descriptor = service;
    publisher->invoke = &cmsg_pub_invoke;

    publisher->client = cmsg_pss_create_publisher_client ();

    return publisher;
}

/**
 * Destroy a cmsg publisher.
 *
 * @param publisher - The publisher to destroy.
 */
void
cmsg_publisher_destroy (cmsg_publisher *publisher)
{
    CMSG_ASSERT_RETURN_VOID (publisher != NULL);
    cmsg_destroy_client_and_transport (publisher->client);
    CMSG_FREE (publisher);
}
