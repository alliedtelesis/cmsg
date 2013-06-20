/* Generated by the protocol buffer compiler.  DO NOT EDIT! */

#ifndef PROTOBUF_C_protobuf_2dc_2dcmsg_2dsub_2dservice_2eproto__INCLUDED
#define PROTOBUF_C_protobuf_2dc_2dcmsg_2dsub_2dservice_2eproto__INCLUDED

#include <google/protobuf-c/protobuf-c.h>

PROTOBUF_C_BEGIN_DECLS


typedef struct _Cmsg__SubEntry Cmsg__SubEntry;
typedef struct _Cmsg__SubEntryResponse Cmsg__SubEntryResponse;


/* --- enums --- */


/* --- messages --- */

struct  _Cmsg__SubEntry
{
  ProtobufCMessage base;
  uint32_t add;
  char *method_name;
  uint32_t transport_type;
  protobuf_c_boolean has_in_sin_addr_s_addr;
  uint32_t in_sin_addr_s_addr;
  protobuf_c_boolean has_in_sin_port;
  uint32_t in_sin_port;
  protobuf_c_boolean has_tipc_family;
  uint32_t tipc_family;
  protobuf_c_boolean has_tipc_addrtype;
  uint32_t tipc_addrtype;
  protobuf_c_boolean has_tipc_addr_name_name_type;
  uint32_t tipc_addr_name_name_type;
  protobuf_c_boolean has_tipc_addr_name_name_instance;
  uint32_t tipc_addr_name_name_instance;
  protobuf_c_boolean has_tipc_addr_name_domain;
  uint32_t tipc_addr_name_domain;
  protobuf_c_boolean has_tipc_scope;
  uint32_t tipc_scope;
};
#define CMSG__SUB_ENTRY__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&cmsg__sub_entry__descriptor) \
    , 0, NULL, 0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0 }


struct  _Cmsg__SubEntryResponse
{
  ProtobufCMessage base;
  int32_t return_value;
};
#define CMSG__SUB_ENTRY_RESPONSE__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&cmsg__sub_entry_response__descriptor) \
    , 0 }


/* Cmsg__SubEntry methods */
void   cmsg__sub_entry__init
                     (Cmsg__SubEntry         *message);
size_t cmsg__sub_entry__get_packed_size
                     (const Cmsg__SubEntry   *message);
size_t cmsg__sub_entry__pack
                     (const Cmsg__SubEntry   *message,
                      uint8_t             *out);
size_t cmsg__sub_entry__pack_to_buffer
                     (const Cmsg__SubEntry   *message,
                      ProtobufCBuffer     *buffer);
Cmsg__SubEntry *
       cmsg__sub_entry__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   cmsg__sub_entry__free_unpacked
                     (Cmsg__SubEntry *message,
                      ProtobufCAllocator *allocator);
/* Cmsg__SubEntryResponse methods */
void   cmsg__sub_entry_response__init
                     (Cmsg__SubEntryResponse         *message);
size_t cmsg__sub_entry_response__get_packed_size
                     (const Cmsg__SubEntryResponse   *message);
size_t cmsg__sub_entry_response__pack
                     (const Cmsg__SubEntryResponse   *message,
                      uint8_t             *out);
size_t cmsg__sub_entry_response__pack_to_buffer
                     (const Cmsg__SubEntryResponse   *message,
                      ProtobufCBuffer     *buffer);
Cmsg__SubEntryResponse *
       cmsg__sub_entry_response__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   cmsg__sub_entry_response__free_unpacked
                     (Cmsg__SubEntryResponse *message,
                      ProtobufCAllocator *allocator);
/* --- per-message closures --- */

typedef void (*Cmsg__SubEntry_Closure)
                 (const Cmsg__SubEntry *message,
                  void *closure_data);
typedef void (*Cmsg__SubEntryResponse_Closure)
                 (const Cmsg__SubEntryResponse *message,
                  void *closure_data);

/* --- services --- */

typedef struct _Cmsg__SubService_Service Cmsg__SubService_Service;
struct _Cmsg__SubService_Service
{
  ProtobufCService base;
  void (*subscribe)(Cmsg__SubService_Service *service,
                    const Cmsg__SubEntry *input,
                    Cmsg__SubEntryResponse_Closure closure,
                    void *closure_data);
};
typedef void (*Cmsg__SubService_ServiceDestroy)(Cmsg__SubService_Service *);
void cmsg__sub_service__init (Cmsg__SubService_Service *service,
                              Cmsg__SubService_ServiceDestroy destroy);
#define CMSG__SUB_SERVICE__BASE_INIT \
    { &cmsg__sub_service__descriptor, protobuf_c_service_invoke_internal, NULL }
#define CMSG__SUB_SERVICE__INIT(function_prefix__) \
    { CMSG__SUB_SERVICE__BASE_INIT,\
      function_prefix__ ## subscribe  }
void cmsg__sub_service__subscribe(ProtobufCService *service,
                                  const Cmsg__SubEntry *input,
                                  Cmsg__SubEntryResponse_Closure closure,
                                  void *closure_data);

/* --- descriptors --- */

extern const ProtobufCMessageDescriptor cmsg__sub_entry__descriptor;
extern const ProtobufCMessageDescriptor cmsg__sub_entry_response__descriptor;
extern const ProtobufCServiceDescriptor cmsg__sub_service__descriptor;

PROTOBUF_C_END_DECLS


#endif  /* PROTOBUF_protobuf_2dc_2dcmsg_2dsub_2dservice_2eproto__INCLUDED */
