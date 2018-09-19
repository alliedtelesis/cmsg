// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.
// http://code.google.com/p/protobuf/
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Author: kenton@google.com (Kenton Varda)
//  Based on original Protocol Buffers design by
//  Sanjay Ghemawat, Jeff Dean, and others.

// Modified to implement C code by Dave Benson.

#include <protoc-c/c_atl_generator.h>
#include <protoc-c/c_helpers.h>
#include <google/protobuf/io/printer.h>
#include "validation.pb.h"

namespace google {
namespace protobuf {
namespace compiler {
namespace c {

AtlCodeGenerator::AtlCodeGenerator(const ServiceDescriptor* descriptor,
                                   const string& dllexport_decl)
  : descriptor_(descriptor) {
  vars_["name"] = descriptor_->name();
  vars_["fullname"] = descriptor_->full_name();
  vars_["cname"] = FullNameToC(descriptor_->full_name());
  vars_["lcfullname"] = FullNameToLower(descriptor_->full_name());
  vars_["ucfullname"] = FullNameToUpper(descriptor_->full_name());
  vars_["lcfullpadd"] = ConvertToSpaces(vars_["lcfullname"]);
  vars_["package"] = descriptor_->file()->package();
  if (dllexport_decl.empty()) {
    vars_["dllexport"] = "";
  } else {
    vars_["dllexport"] = dllexport_decl + " ";
  }
}

AtlCodeGenerator::~AtlCodeGenerator() {}

void AtlCodeGenerator::GenerateDescriptorDeclarations(io::Printer* printer)
{
  printer->Print(vars_, "extern const ProtobufCServiceDescriptor $lcfullname$_descriptor;\n");
}

// Generate the client header file
void AtlCodeGenerator::GenerateClientHeaderFile(io::Printer* printer)
{
  // generate the api declaration
  printer->Print("\n/* Start of API definition */\n\n");
  GenerateAtlApiDefinitions(printer, true);
  printer->Print("\n/* End of API definition */\n");
}

// Generate the server header file
void AtlCodeGenerator::GenerateServerHeaderFile(io::Printer* printer)
{
  // generate the server declaration
  printer->Print("\n/* Start of Server definition */\n\n");
  GenerateAtlServerDefinitions(printer, true);
  printer->Print("\n/* End of Server definition */\n");
}

// Generate the client source file
void AtlCodeGenerator::GenerateClientCFile(io::Printer* printer)
{
  printer->Print("\n/* Start of API Implementation */\n\n");
  GenerateAtlApiImplementation(printer);
  printer->Print("\n/* End of API Implementation */\n");
}

// Generate the server source file
void AtlCodeGenerator::GenerateServerCFile(io::Printer* printer)
{
  printer->Print("\n/* Start of local server definitions */\n\n");
  GenerateAtlServerCFileDefinitions(printer);
  printer->Print("\n/* End of local server definitions */\n\n");

  printer->Print("\n/* Start of Server Implementation */\n\n");
  GenerateAtlServerImplementation(printer);
  printer->Print("\n/* End of Server Implementation */\n");
}

// Generate the http proxy array entries
void AtlCodeGenerator::GenerateHttpProxyArrayEntries(io::Printer* printer)
{
    for (int i = 0; i < descriptor_->method_count(); i++) {
        const MethodDescriptor *method = descriptor_->method(i);
        GenerateHttpProxyArrayEntriesPerMethod(*method, printer);
    }
}

// Generate the http proxy array functions
void AtlCodeGenerator::GenerateHttpProxyArrayFunctions(io::Printer* printer)
{
    // generate the cmsg proxy array functions
    printer->Print(vars_, "cmsg_service_info *cmsg_proxy_array_get (void)\n");
    printer->Print("{\n");
    printer->Indent();
    printer->Print("return service_info_entries;\n");
    printer->Outdent();
    printer->Print("}\n\n");

    printer->Print(vars_, "int cmsg_proxy_array_size (void)\n");
    printer->Print("{\n");
    printer->Indent();
    printer->Print("return num_service_info_entries;\n");
    printer->Outdent();
    printer->Print("}\n\n");
}

void AtlCodeGenerator::GenerateHttpProxyArrayFunctionDefs(io::Printer* printer)
{
    // generate the cmsg proxy array functions
    printer->Print(vars_, "cmsg_service_info *cmsg_proxy_array_get (void);\n");
    printer->Print(vars_, "int cmsg_proxy_array_size (void);\n");
}

void AtlCodeGenerator::GenerateHttpProxyArrayEntry(const HttpRule &http_rule, io::Printer* printer)
{
    printer->Indent();
    printer->Print("{\n");

    printer->Indent();
    printer->Print(vars_, ".service_descriptor = &$lcfullname$_descriptor,\n");
    printer->Print(vars_, ".input_msg_descriptor = &$inputname$_descriptor,\n");
    printer->Print(vars_, ".output_msg_descriptor = &$outputname$_descriptor,\n");
    printer->Print(vars_, ".api_ptr = &$lcfullname$_api_$method$,\n");

    vars_["body"] = http_rule.body();

    if (http_rule.has_get())
    {
        vars_["url"] = http_rule.get();
        printer->Print(vars_, ".url_string = \"$url$\",\n");
        printer->Print(".http_verb = CMSG_HTTP_GET,\n");
        printer->Print(vars_, ".body_string = \"$body$\",\n");
    }
    else if (http_rule.has_put())
    {
        vars_["url"] = http_rule.put();
        printer->Print(vars_, ".url_string = \"$url$\",\n");
        printer->Print(".http_verb = CMSG_HTTP_PUT,\n");
        printer->Print(vars_, ".body_string = \"$body$\",\n");
    }
    else if (http_rule.has_post ())
    {
        vars_["url"] = http_rule.post();
        printer->Print(vars_, ".url_string = \"$url$\",\n");
        printer->Print(".http_verb = CMSG_HTTP_POST,\n");
        printer->Print(vars_, ".body_string = \"$body$\",\n");
    }
    else if (http_rule.has_delete_())
    {
        vars_["url"] = http_rule.delete_();
        printer->Print(vars_, ".url_string = \"$url$\",\n");
        printer->Print(".http_verb = CMSG_HTTP_DELETE,\n");
        printer->Print(vars_, ".body_string = \"$body$\",\n");
    }
    else if (http_rule.has_patch())
    {
        vars_["url"] = http_rule.patch();
        printer->Print(vars_, ".url_string = \"$url$\",\n");
        printer->Print(".http_verb = CMSG_HTTP_PATCH,\n");
        printer->Print(vars_, ".body_string = \"$body$\",\n");
    }
    else
    {
        assert (false && "Error: a valid HTTP verb must be specified");
    }

    printer->Outdent();
    printer->Print("},\n");
    printer->Outdent();
}

void AtlCodeGenerator::GenerateHttpProxyArrayEntriesPerMethod(const MethodDescriptor &method,
                                                              io::Printer* printer)
{
    string lcname = CamelToLower(method.name());
    vars_["method"] = lcname;
    vars_["inputname"] = FullNameToLower(method.input_type()->full_name());
    vars_["outputname"] = FullNameToLower(method.output_type()->full_name());

    if (method.options().HasExtension(http))
    {
        HttpRule http_rule = method.options().GetExtension(http);
        GenerateHttpProxyArrayEntry(http_rule, printer);

        // Generate an entry for each additional binding
        for (int i = 0; i < http_rule.additional_bindings_size(); i++)
        {
            GenerateHttpProxyArrayEntry(http_rule.additional_bindings(i), printer);
        }
    }
}

//
// Methods to generate the client side code (API)
//
void AtlCodeGenerator::GenerateAtlApiDefinitions(io::Printer* printer, bool forHeader)
{
  for (int i = 0; i < descriptor_->method_count(); i++) {
      const MethodDescriptor *method = descriptor_->method(i);
      GenerateAtlApiDefinition(*method, printer, forHeader);
  }
}

void AtlCodeGenerator::GenerateAtlApiDefinition(const MethodDescriptor &method, io::Printer* printer, bool forHeader)
{
  string lcname = CamelToLower(method.name());
  vars_["method"] = lcname;
  vars_["method_input"] = FullNameToC(method.input_type()->full_name());
  vars_["method_output"] = FullNameToC(method.output_type()->full_name());

  printer->Print(vars_, "int $lcfullname$_api_$method$ (cmsg_client *_client");

  if (method.input_type()->field_count() > 0)
  {
    printer->Print(vars_, ", const $method_input$ *_send_msg");
  }
  //
  // only add the rpc return message to the parameter list if its not empty
  if (method.output_type()->field_count() > 0)
  {
    printer->Print(vars_, ", $method_output$ **_recv_msg");
  }
  printer->Print(")");
  if (forHeader)
  {
    printer->Print(";");
  }
  printer->Print("\n");

}

void AtlCodeGenerator::GenerateAtlApiImplementation(io::Printer* printer)
{
  //
  // go through all rpc methods defined for this service and generate the api function
  //
  for (int i = 0; i < descriptor_->method_count(); i++)
  {
    const MethodDescriptor *method = descriptor_->method(i);
    vars_["method"] = FullNameToC(method->full_name());
    vars_["input_typename"] = FullNameToC(method->input_type()->full_name());
    vars_["input_typename_upper"] = FullNameToUpper(method->input_type()->full_name());
    vars_["output_typename"] = FullNameToC(method->output_type()->full_name());
    vars_["output_typename_upper"] = FullNameToUpper(method->output_type()->full_name());
    //
    // create the names of the send and recv messages that will passed to the send function.
    // this allows us to change the names (if e.g. the recv message is null) without doing
    // multiple "ifs" to generate the call to the send function.
    vars_["send_msg_name"] = "_send_msg";
    vars_["closure_data_name"] = "&_closure_data";

    //
    // generate the api function
    //
    // get the definition
    GenerateAtlApiDefinition(*method, printer, false);

    //start filling it in
    printer->Print("{\n");
    printer->Indent();
    printer->Print("int32_t _return_status = CMSG_RET_ERR;\n");

    //
    // must create send message if it is not supplied by the developer
    // (ie when it has no fields)
    //
    if(method->input_type()->field_count() <= 0)
    {
      printer->Print("/* Create a local send message since the developer hasn't supplied one. */\n");
      printer->Print(vars_, "$input_typename$ _send_msg = $input_typename_upper$_INIT;\n");
      // change the name of the send message so that we can get the address of it for the send call
      vars_["send_msg_name"] = "&_send_msg";
    }
    //
    // only create response msg when response has some fields
    //
    if(method->output_type()->field_count() > 0)
    {
      printer->Print("cmsg_client_closure_data _closure_data[CMSG_RECV_ARRAY_SIZE] = {{NULL, NULL}};\n");
    }
    else
    {
      // no fields so set our send message name to NULL
      vars_["closure_data_name"] = "NULL";
    }
    printer->Print(vars_, "ProtobufCService *_service = (ProtobufCService *)_client;\n");

    //
    // test that the pointer to the client is valid before doing anything else
    //
    printer->Print("\n");
    printer->Print("/* test that the pointer to the client is valid before doing anything else */\n");
    printer->Print("if (_service == NULL)\n");
    printer->Print("{\n");
    printer->Indent();
    printer->Print("return CMSG_RET_ERR;\n");
    printer->Outdent();
    printer->Print("}\n");

    //
    // finally, test that the recv msg pointer is NULL.
    // if it is, set it to NULL, but yell loudly that this is happening
    // (in case this is a memory leak).
    //
    if (method->output_type()->field_count() > 0)
    {
      printer->Print("\n");
      printer->Print("/* test that the pointer to the recv msg is NULL. If it isn't, set it to\n");
      printer->Print(" * NULL but complain loudly that the api is not being used correctly  */\n");
      printer->Print("if (*(_recv_msg) != NULL)\n");
      printer->Print("{\n");
      printer->Indent();
      printer->Print("*(_recv_msg) = NULL;\n");
      printer->Print("CMSG_LOG_CLIENT_DEBUG (_client, \"WARNING: %s API called with Non-NULL recv_msg! Setting to NULL! (This may be a leak!)\", __FUNCTION__);\n");
      printer->Outdent();
      printer->Print("}\n");
    }


    printer->Print("\n");

    //
    // now send!
    //
    printer->Print("/* Send! */\n");
    vars_["closure_name"] = GetAtlClosureFunctionName(*method);
    vars_["lcfullname"] = FullNameToLower(descriptor_->full_name());
    vars_["method_lcname"] = CamelToLower(method->name());

    printer->Print(vars_, "_return_status = $lcfullname$_$method_lcname$ (_service, $send_msg_name$, NULL, $closure_data_name$);\n\n");

    printer->Print("\n");

    //
    // copy the return values (if any are expected)
    //
    if (method->output_type()->field_count() > 0)
    {
      printer->Print("int i = 0;\n");
      printer->Print("/* sanity check our returned message pointer */\n");
      printer->Print("while (_closure_data[i].message != NULL)\n");
      printer->Print("{\n");
      printer->Indent();

      printer->Print("/* Update developer output msg to point to received message from invoke */\n");
      printer->Print("_recv_msg[i] = _closure_data[i].message;\n");
      printer->Print("i++;\n");
      printer->Print("\n");
      printer->Outdent();
      printer->Print("}\n"); //while (_closure_data[i].message != NULL)
    }
    //
    // finally return something
    //
    printer->Print("return _return_status;\n");
    printer->Outdent();
    printer->Print("}\n\n");

  }

}

//
// Methods to generate the server side code (IMPL and SEND functions)
//
void AtlCodeGenerator::GenerateAtlServerImplementation(io::Printer* printer)
{
  //
  // Service initialization
  //
  printer->Print(vars_, "$cname$_Service $lcfullname$_service = $ucfullname$_INIT($lcfullname$_server_);\n\n");

  //
  // go through all rpc methods defined for this service and generate the server function
  //
  for (int i = 0; i < descriptor_->method_count(); i++)
  {
    const MethodDescriptor *method = descriptor_->method(i);
    vars_["method"] = FullNameToC(method->full_name());
    vars_["input_typename"] = FullNameToC(method->input_type()->full_name());
    vars_["input_typename_upper"] = FullNameToUpper(method->input_type()->full_name());
    vars_["input_typename_lower"] = FullNameToLower(method->input_type()->full_name());
    vars_["output_typename"] = FullNameToC(method->output_type()->full_name());
    vars_["output_typename_upper"] = FullNameToUpper(method->output_type()->full_name());

    //
    // Generate the server function
    //
    // get the definition
    GenerateAtlServerDefinition(*method, printer, false);

    // start filling it in
    printer->Print("{\n");
    printer->Indent();

    printer->Print("\n");
    printer->Print("if (input == NULL)\n");
    printer->Print("{\n");
    printer->Indent();
    printer->Print("_closure (NULL, _closure_data);\n");
    printer->Print("return CMSG_RET_ERR;\n");
    printer->Outdent();
    printer->Print("}\n");

    printer->Print("\n");
    printer->Print("// these are needed in 'Send' function for sending reply back to the client\n");
    printer->Print("_service->closure = _closure;\n");
    printer->Print("_service->closure_data = _closure_data;\n");
    printer->Print("\n");

    //
    // call _impl user function
    //
    printer->Print("bool call_impl = true;\n");
    if (method->options().HasExtension(auto_validation) && method->options().GetExtension(auto_validation))
    {
        printer->Print("char err_str[512];\n");
        printer->Print(vars_, "if (!$input_typename_lower$_validate (input, err_str, sizeof (err_str)))\n");
        printer->Print("{\n");
        printer->Indent();
        printer->Print("call_impl = false;\n");
        printer->Print(vars_, "ant_result ant_result_msg = ANT_RESULT_INIT;\n");
        printer->Print("CMSG_SET_FIELD_VALUE (&ant_result_msg, code, ANT_CODE_INVALID_ARGUMENT);\n");
        printer->Print("CMSG_SET_FIELD_PTR (&ant_result_msg, message, err_str);\n");
        if (strcmp (method->output_type()->full_name().c_str(), "ant_result") == 0)
        {
            printer->Print(vars_, "$lcfullname$_server_$method$Send (_service, &ant_result_msg);\n");
        }
        else
        {
            printer->Print(vars_, "$output_typename$ send_msg = $output_typename_upper$_INIT;\n");
            printer->Print("CMSG_SET_FIELD_PTR (&send_msg, _error_info, &ant_result_msg);\n");
            printer->Print(vars_, "$lcfullname$_server_$method$Send (_service, &send_msg);\n");
        }
        printer->Outdent();
        printer->Print("}\n");
    }

    printer->Print("if (call_impl)\n");
    printer->Print("{\n");
    printer->Indent();
    // now pass the pbc struct to the new impl function
    printer->Print(vars_, "$lcfullname$_impl_$method$ (_service");
    if (method->input_type()->field_count() > 0)
    {
      printer->Print(", input");
    }
    printer->Print(");\n");
    printer->Outdent();
    printer->Print("}\n");

    //
    // call closure()
    //
    printer->Print("// clean up\n");
    printer->Print("_service->closure = NULL;\n");
    printer->Print("_service->closure_data = NULL;\n");
    printer->Print("return CMSG_RET_OK;\n");

    // end of the function
    printer->Outdent();
    printer->Print("}\n\n");

    // we need to generate a closure function for the api to call on return
    // of the rpc call from the server
    //
    GenerateAtlServerSendImplementation(*method, printer);
  }

}

void AtlCodeGenerator::GenerateAtlServerDefinitions(io::Printer* printer, bool forHeader)
{
  printer->Print(vars_, "extern $cname$_Service $lcfullname$_service;\n");

  for (int i = 0; i < descriptor_->method_count(); i++) {
    const MethodDescriptor *method = descriptor_->method(i);
    // only declare the server send in the header file
    GenerateAtlServerSendDefinition(*method, printer, forHeader);
  }

  printer->Print("\n");

  for (int i = 0; i < descriptor_->method_count(); i++) {
    const MethodDescriptor *method = descriptor_->method(i);
    GenerateAtlServerImplDefinition(*method, printer, forHeader);
  }
}

void AtlCodeGenerator::GenerateAtlServerCFileDefinitions(io::Printer* printer)
{
  for (int i = 0; i < descriptor_->method_count(); i++) {
    const MethodDescriptor *method = descriptor_->method(i);
    GenerateAtlServerDefinition(*method, printer, true);
  }
}

void AtlCodeGenerator::GenerateAtlServerDefinition(const MethodDescriptor &method, io::Printer* printer, bool forHeader)
{
  string lcname = CamelToLower(method.name());
  string lcfullname = FullNameToLower(descriptor_->full_name());
  vars_["method"] = lcname;
  vars_["input_typename"] = FullNameToC(method.input_type()->full_name());
  vars_["output_typename"] = FullNameToC(method.output_type()->full_name());
  vars_["padddddddddddddddddddddddd"] = ConvertToSpaces(lcfullname + "_server_" + lcname);

  printer->Print(vars_,
                 "int32_t $lcfullname$_server_$method$ ($cname$_Service *_service,\n"
                 "        $padddddddddddddddddddddddd$  const $input_typename$ *input,\n"
                 "        $padddddddddddddddddddddddd$  $output_typename$_Closure _closure,\n"
                 "        $padddddddddddddddddddddddd$  void *_closure_data)");
  if (forHeader)
  {
    printer->Print(";");
  }
  printer->Print("\n");

}

void AtlCodeGenerator::GenerateAtlServerImplDefinition(const MethodDescriptor &method, io::Printer* printer, bool forHeader)
{
  string lcname = CamelToLower(method.name());
  vars_["method"] = lcname;
  vars_["method_input"] = FullNameToC(method.input_type()->full_name());

  printer->Print(vars_, "void $lcfullname$_impl_$method$ (const void *service");
  if (method.input_type()->field_count() > 0)
  {
    printer->Print(vars_, ", const $method_input$ *recv_msg");
  }
  printer->Print(")");
  if (forHeader)
  {
    printer->Print(";");
  }
  printer->Print("\n");
}

void AtlCodeGenerator::GenerateAtlServerSendImplementation(const MethodDescriptor &method, io::Printer* printer)
{
  vars_["method"] = FullNameToLower(method.name());
  vars_["input_typename"] = FullNameToC(method.input_type()->full_name());
  vars_["output_typename"] = FullNameToC(method.output_type()->full_name());
  vars_["send_msg_name"] = "send_msg";

  GenerateAtlServerSendDefinition(method, printer, false);

  printer->Print("{\n");
  printer->Indent();

  printer->Print(vars_, "$output_typename$_Closure _closure = ((const $cname$_Service *)_service)->closure;\n");
  printer->Print(vars_, "void *_closure_data = ((const $cname$_Service *)_service)->closure_data;\n");

  if (method.output_type()->field_count() == 0)
  {
    printer->Print(vars_, "$output_typename$ send_msg = $output_typename_upper$_INIT;\n");
    vars_["send_msg_name"] = "&send_msg";
  }
  printer->Print("\n");

  printer->Print(vars_, "_closure ($send_msg_name$, _closure_data);\n");

  printer->Print("\n");

  printer->Outdent();
  printer->Print("}\n\n");
}

void AtlCodeGenerator::GenerateAtlServerSendDefinition(const MethodDescriptor &method, io::Printer* printer, bool forHeader)
{
  string lcname = CamelToLower(method.name());
  vars_["method"] = lcname;
  vars_["method_output"] = FullNameToC(method.output_type()->full_name());

  printer->Print(vars_, "void $lcfullname$_server_$method$Send (const void *_service");
  if (method.output_type()->field_count() > 0)
  {
    printer->Print(vars_, ", const $method_output$ *send_msg");
  }
  printer->Print(")");
  if (forHeader)
  {
    printer->Print(";");
  }
  printer->Print("\n");
}

//
// Utility methods
//
string AtlCodeGenerator::GetAtlClosureFunctionName(const MethodDescriptor &method)
{
  string closure_name = "handle_" + FullNameToLower(method.full_name()) + "_response";
  return closure_name;
}

// This generates a server impl stub function that initialises a response message of the correct type
// and sends it back empty.
void AtlCodeGenerator::GenerateAtlServerImplStub(const MethodDescriptor &method, io::Printer* printer)
{
  string lcname = CamelToLower(method.name());
  vars_["method"] = lcname;
  vars_["method_input"] = FullNameToC(method.input_type()->full_name());
  vars_["method_output"] = FullNameToC(method.output_type()->full_name());
  vars_["method_output_upper"] = FullNameToUpper(method.output_type()->full_name());

  GenerateAtlServerImplDefinition(method, printer, false);

  printer->Print("{\n");
  printer->Indent();

  if (method.output_type()->field_count() > 0)
  {
    printer->Print(vars_, "$method_output$ send_msg = $method_output_upper$_INIT;\n");
    printer->Print("\n");
  }

  printer->Print(vars_, "$lcfullname$_server_$method$Send (service");
  if (method.output_type()->field_count() > 0)
  {
    printer->Print(", &send_msg");
  }
  printer->Print(");\n");

  printer->Outdent();
  printer->Print("}\n");
  printer->Print("\n");
}

// This generates some stubs that initialise a response message and send it back empty.
// These can be copied into the server implementation to get things building.
void AtlCodeGenerator::GenerateAtlServerImplStubs(io::Printer* printer)
{
  for (int i = 0; i < descriptor_->method_count(); i++) {
    const MethodDescriptor *method = descriptor_->method(i);
    GenerateAtlServerImplStub(*method, printer);
  }
}

// This can be useful for debugging message generation.
void AtlCodeGenerator::PrintMessageFields(io::Printer* printer, const Descriptor *message)
{

  vars_["message_name"] = message->full_name();
  printer->Print(vars_, "message: $message_name$\n");
  printer->Indent();
  if (message->nested_type_count() > 0)
  {
    printer->Print("contains nested types\n");
  }
  else
  {
    printer->Print("doesn't contain nested types\n");
  }
  for (int i = 0; i < message->field_count(); i++) {
    const FieldDescriptor *field = message->field(i);
    if (field->type() == FieldDescriptor::TYPE_MESSAGE)
    {
      PrintMessageFields(printer, field->message_type());
    }
    else
    {
      vars_["field_name"] = FieldName(field);
      vars_["field_type"] = TypeToString(field->type());

      printer->Print(vars_, "type = $field_type$, name = $field_name$\n");
    }
  }
  printer->Outdent();
}

// This is used by the PrintMessageFields method
string AtlCodeGenerator::TypeToString(FieldDescriptor::Type type)
{
        string description = "";
        switch (type) {
    case FieldDescriptor::TYPE_DOUBLE:
        description = "double";
        break;
    case FieldDescriptor::TYPE_FLOAT:
        description = "float";
        break;
    case FieldDescriptor::TYPE_INT64:
        description = "int64_t";
        break;
    case FieldDescriptor::TYPE_UINT64:
        description = "uint64_t";
        break;
    case FieldDescriptor::TYPE_INT32:
        description = "int32_t";
        break;
    case FieldDescriptor::TYPE_FIXED64:
        description = "uint64_t";
        break;
    case FieldDescriptor::TYPE_FIXED32:
        description = "uint32_t";
        break;
    case FieldDescriptor::TYPE_BOOL:
        description = "protobuf_c_boolean";
        break;
    case FieldDescriptor::TYPE_STRING:
        description = "char *";
        break;
    case FieldDescriptor::TYPE_GROUP:
        description = "";
        break;
    case FieldDescriptor::TYPE_MESSAGE:
        description = "struct";
        break;
    case FieldDescriptor::TYPE_BYTES:
        description = "ProtobufCBinaryData";
        break;
    case FieldDescriptor::TYPE_UINT32:
        description = "uint32_t";
        break;
    case FieldDescriptor::TYPE_ENUM:
        description = "uint32_t";
        break;
    case FieldDescriptor::TYPE_SFIXED32:
        description = "int32_t";
        break;
    case FieldDescriptor::TYPE_SFIXED64:
        description = "int64_t";
        break;
    case FieldDescriptor::TYPE_SINT32:
        description = "int32_t";
        break;
    case FieldDescriptor::TYPE_SINT64:
        description = "int64_t";
        break;
    case FieldDescriptor::TYPE_INT8:
        description = "int8_t";
        break;
    case FieldDescriptor::TYPE_UINT8:
        description = "uint8_t";
        break;
    case FieldDescriptor::TYPE_INT16:
        description = "int16_t";
        break;
    case FieldDescriptor::TYPE_UINT16:
        description = "uint16_t";
        break;
    default:
        break;
        }
        return description;
}

}  // namespace c
}  // namespace compiler
}  // namespace protobuf
}  // namespace google
