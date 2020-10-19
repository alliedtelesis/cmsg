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
#include <protoc-c/c_helpers_cmsg.h>
#include <google/protobuf/io/printer.h>
#include "validation.pb.h"
#include "supported_service.pb.h"
#include "file_response.pb.h"

namespace google {
namespace protobuf {
namespace compiler {
namespace c {

AtlCodeGenerator::AtlCodeGenerator(const ServiceDescriptor* descriptor)
  : descriptor_(descriptor) {
  vars_["name"] = descriptor_->name();
  vars_["fullname"] = descriptor_->full_name();
  vars_["cname"] = cmsg::FullNameToC(descriptor_->full_name());
  vars_["lcfullname"] = cmsg::FullNameToLower(descriptor_->full_name());
  vars_["ucfullname"] = cmsg::FullNameToUpper(descriptor_->full_name());
  vars_["lcfullpadd"] = cmsg::ConvertToSpaces(vars_["lcfullname"]);
  vars_["package"] = descriptor_->file()->package();
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

void AtlCodeGenerator::GenerateHttpProxyArrayEntry(const ::google::api::HttpRule &http_rule, io::Printer* printer)
{
    printer->Indent();
    printer->Print("{\n");

    printer->Indent();
    printer->Print(vars_, ".service_descriptor = &$lcfullname$_descriptor,\n");
    printer->Print(vars_, ".input_msg_descriptor = &$inputname$_descriptor,\n");
    printer->Print(vars_, ".output_msg_descriptor = &$outputname$_descriptor,\n");
    printer->Print(vars_, ".api_ptr = &$lcfullname$_api_$method$,\n");

    vars_["body"] = http_rule.body();

    if (!http_rule.get().empty())
    {
        vars_["url"] = http_rule.get();
        printer->Print(vars_, ".url_string = \"$url$\",\n");
        printer->Print(".http_verb = CMSG_HTTP_GET,\n");
        printer->Print(vars_, ".body_string = \"$body$\",\n");
    }
    else if (!http_rule.put().empty())
    {
        vars_["url"] = http_rule.put();
        printer->Print(vars_, ".url_string = \"$url$\",\n");
        printer->Print(".http_verb = CMSG_HTTP_PUT,\n");
        printer->Print(vars_, ".body_string = \"$body$\",\n");
    }
    else if (!http_rule.post().empty())
    {
        vars_["url"] = http_rule.post();
        printer->Print(vars_, ".url_string = \"$url$\",\n");
        printer->Print(".http_verb = CMSG_HTTP_POST,\n");
        printer->Print(vars_, ".body_string = \"$body$\",\n");
    }
    else if (!http_rule.delete_().empty())
    {
        vars_["url"] = http_rule.delete_();
        printer->Print(vars_, ".url_string = \"$url$\",\n");
        printer->Print(".http_verb = CMSG_HTTP_DELETE,\n");
        printer->Print(vars_, ".body_string = \"$body$\",\n");
    }
    else if (!http_rule.patch().empty())
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
    string lcname = cmsg::CamelToLower(method.name());
    vars_["method"] = lcname;
    vars_["inputname"] = cmsg::FullNameToLower(method.input_type()->full_name());
    vars_["outputname"] = cmsg::FullNameToLower(method.output_type()->full_name());

    if (method.options().HasExtension(::google::api::http))
    {
        ::google::api::HttpRule http_rule = method.options().GetExtension(::google::api::http);
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
  string lcname = cmsg::CamelToLower(method.name());
  vars_["method"] = lcname;
  vars_["method_input"] = cmsg::FullNameToC(method.input_type()->full_name());
  vars_["method_output"] = cmsg::FullNameToC(method.output_type()->full_name());

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

static void
print_supported_service_check (const ServiceDescriptor* descriptor_, const MethodDescriptor *method,
                               io::Printer* printer)
{
    ServiceSupportInfo info = descriptor_->options().GetExtension(service_support_check);
    std::map<string, string> vars_;
    vars_["file_path"] = info.file_path();
    vars_["output_typename"] = cmsg::FullNameToC(method->output_type()->full_name());

    printer->Print("\n");
    printer->Print("/* Service support check */\n");
    printer->Print(vars_, "if (access (\"$file_path$\", F_OK) == -1)\n");
    printer->Print("{\n");
    printer->Indent();

    printer->Print(vars_, "ant_result *ant_result_msg = CMSG_MALLOC (sizeof (ant_result));\n");
    printer->Print("ant_result_init (ant_result_msg);\n");
    if (info.has_message())
    {
        vars_["message"] = info.message();
        printer->Print(vars_, "CMSG_SET_FIELD_PTR (ant_result_msg, message, CMSG_STRDUP (\"$message$\"));\n");
    }
    if (info.has_code())
    {
        vars_["code"] = info.code();
        printer->Print(vars_, "CMSG_SET_FIELD_VALUE (ant_result_msg, code, $code$);\n");
    }

    if (strcmp (method->output_type()->full_name().c_str(), "ant_result") == 0)
    {
        printer->Print("_recv_msg[0] = ant_result_msg;\n");
    }
    else
    {
        printer->Print(vars_, "$output_typename$ *send_msg = CMSG_MALLOC (sizeof ($output_typename$));\n");
        printer->Print(vars_, "$output_typename$_init (send_msg);\n");
        printer->Print("CMSG_SET_FIELD_PTR (send_msg, _error_info, ant_result_msg);\n");
        printer->Print("_recv_msg[0] = send_msg;\n");
    }

    printer->Print("return CMSG_RET_OK;\n");
    printer->Outdent();
    printer->Print("}\n");
}

static void
print_file_response (const MethodDescriptor *method, io::Printer* printer)
{
    FileResponseInfo info = method->options().GetExtension(file_response);
    std::map<string, string> vars_;
    vars_["file_path"] = info.file_path();
    vars_["output_typename"] = cmsg::FullNameToC(method->output_type()->full_name());

    printer->Print("\n");
    printer->Print("/* File response */\n");
    printer->Print(vars_, "if (access (\"$file_path$\", F_OK) == -1)\n");
    printer->Print("{\n");
    printer->Indent();

    printer->Print(vars_, "ant_result *ant_result_msg = CMSG_MALLOC (sizeof (ant_result));\n");
    printer->Print("ant_result_init (ant_result_msg);\n");
    printer->Print(vars_, "CMSG_SET_FIELD_VALUE (ant_result_msg, code, ANT_CODE_OK);\n");
    printer->Print(vars_, "$output_typename$ *send_msg = CMSG_MALLOC (sizeof ($output_typename$));\n");
    printer->Print(vars_, "$output_typename$_init (send_msg);\n");
    printer->Print("CMSG_SET_FIELD_PTR (send_msg, _error_info, ant_result_msg);\n");
    printer->Print("_recv_msg[0] = send_msg;\n");
    printer->Print("return CMSG_RET_OK;\n");
    printer->Outdent();
    printer->Print("}\n");
    printer->Print("else\n");
    printer->Print("{\n");
    printer->Indent();
    printer->Print(vars_, "$output_typename$ *read_data_msg = ($output_typename$ *)\n");
    printer->Print(vars_, "    cmsg_get_msg_from_file (CMSG_MSG_DESCRIPTOR ($output_typename$),\n");
    printer->Print(vars_, "                            \"$file_path$\");\n");
    printer->Print("if (read_data_msg == NULL)\n");
    printer->Print("{\n");
    printer->Indent();
    printer->Print("return CMSG_RET_ERR;\n");
    printer->Outdent();
    printer->Print("}\n");
    printer->Print("else\n");
    printer->Print("{\n");
    printer->Indent();
    printer->Print("_recv_msg[0] = read_data_msg;\n");
    printer->Print("return CMSG_RET_OK;\n");
    printer->Outdent();
    printer->Print("}\n");
    printer->Outdent();
    printer->Print("}\n");
}

void AtlCodeGenerator::GenerateAtlApiImplementation(io::Printer* printer)
{
  //
  // go through all rpc methods defined for this service and generate the api function
  //
  for (int i = 0; i < descriptor_->method_count(); i++)
  {
    const MethodDescriptor *method = descriptor_->method(i);
    vars_["method"] = cmsg::FullNameToC(method->full_name());
    vars_["input_typename"] = cmsg::FullNameToC(method->input_type()->full_name());
    vars_["input_typename_upper"] = cmsg::FullNameToUpper(method->input_type()->full_name());
    vars_["output_typename"] = cmsg::FullNameToC(method->output_type()->full_name());
    vars_["output_typename_upper"] = cmsg::FullNameToUpper(method->output_type()->full_name());
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
      printer->Print("\ncmsg_api_recv_ptr_null_check (_client, (void **) (_recv_msg), __func__);\n");
    }

    printer->Print("\n");

    //
    // If the service is using the 'service_support_check' option then check
    // that the service is supported.
    //
    if (descriptor_->options().HasExtension(service_support_check) &&
        !method->options().HasExtension(disable_service_support_check))
    {
        print_supported_service_check (descriptor_, method, printer);
    }

    //
    // If the service is using the 'file_response' option then generate
    // the code now.
    //
    if (method->options().HasExtension(file_response))
    {
        print_file_response (method, printer);
        printer->Outdent();
        printer->Print("}\n\n");
        continue;
    }

    printer->Print("\n");

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

    printer->Print("cmsg_client_closure_data _closure_data[CMSG_RECV_ARRAY_SIZE] = {{NULL, NULL, CMSG_RET_ERR}};\n");

    //
    // now send!
    //
    printer->Print("/* Send! */\n");
    vars_["closure_name"] = GetAtlClosureFunctionName(*method);
    vars_["lcfullname"] = cmsg::FullNameToLower(descriptor_->full_name());
    vars_["method_lcname"] = cmsg::CamelToLower(method->name());

    printer->Print(vars_, "$lcfullname$_$method_lcname$ (_service, $send_msg_name$, NULL, $closure_data_name$);\n\n");

    printer->Print("\n");

    //
    // copy the return values (if any are expected)
    //
    printer->Print("int i = 0;\n");
    printer->Print("/* sanity check our returned message pointer */\n");
    printer->Print("while (_closure_data[i].message != NULL)\n");
    printer->Print("{\n");
    printer->Indent();

    if (method->output_type()->field_count() > 0)
    {
      printer->Print("/* Update developer output msg to point to received message from invoke */\n");
      printer->Print(vars_, "_recv_msg[i] = ($output_typename$ *) _closure_data[i].message;\n");
    }
    else
    {
      printer->Print("/* Free the received message since the caller does not expect to receive it */\n");
      printer->Print("CMSG_FREE_RECV_MSG (_closure_data[i].message);\n");
    }
    printer->Print("i++;\n");
    printer->Print("\n");
    printer->Outdent();
    printer->Print("}\n");

    //
    // finally return something
    //
    printer->Print("return _closure_data[0].retval;\n");
    printer->Outdent();
    printer->Print("}\n\n");

  }

}

//
// Methods to generate the server side code (IMPL calling functions)
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
    vars_["method"] = cmsg::FullNameToC(method->full_name());
    vars_["input_typename"] = cmsg::FullNameToC(method->input_type()->full_name());
    vars_["input_typename_upper"] = cmsg::FullNameToUpper(method->input_type()->full_name());
    vars_["input_typename_lower"] = cmsg::FullNameToLower(method->input_type()->full_name());
    vars_["output_typename"] = cmsg::FullNameToC(method->output_type()->full_name());
    vars_["output_typename_upper"] = cmsg::FullNameToUpper(method->output_type()->full_name());

    //
    // Generate the server function
    //
    // get the definition
    GenerateAtlServerDefinition(*method, printer, false);

    if (method->options().HasExtension(file_response)) {
        printer->Print("{\n");
        printer->Print("}\n");
        continue;
    }

    // start filling it in
    printer->Print("{\n");
    printer->Indent();

    printer->Print("cmsg_server_closure_info closure_info;\n");
    printer->Print("\n");

    printer->Print("if (input == NULL)\n");
    printer->Print("{\n");
    printer->Indent();
    printer->Print("_closure (NULL, _closure_data);\n");
    printer->Print("return;\n");
    printer->Outdent();
    printer->Print("}\n");

    printer->Print("\n");
    printer->Print("// these are needed in 'Send' function for sending reply back to the client\n");
    printer->Print("closure_info.closure = _closure;\n");
    printer->Print("closure_info.closure_data = _closure_data;\n");
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
            printer->Print(vars_, "$lcfullname$_server_$method$Send (&closure_info, &ant_result_msg);\n");
        }
        else
        {
            printer->Print(vars_, "$output_typename$ send_msg = $output_typename_upper$_INIT;\n");
            printer->Print("CMSG_SET_FIELD_PTR (&send_msg, _error_info, &ant_result_msg);\n");
            printer->Print(vars_, "$lcfullname$_server_$method$Send (&closure_info, &send_msg);\n");
        }
        printer->Outdent();
        printer->Print("}\n");
    }

    printer->Print("if (call_impl)\n");
    printer->Print("{\n");
    printer->Indent();
    // now pass the pbc struct to the new impl function
    printer->Print(vars_, "$lcfullname$_impl_$method$ (&closure_info");
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
    printer->Print("\n");

    // end of the function
    printer->Outdent();
    printer->Print("}\n\n");
  }

}

void AtlCodeGenerator::GenerateAtlServerDefinitions(io::Printer* printer, bool forHeader)
{
  printer->Print(vars_, "extern $cname$_Service $lcfullname$_service;\n");

  for (int i = 0; i < descriptor_->method_count(); i++) {
    const MethodDescriptor *method = descriptor_->method(i);
    // only declare the server send in the header file
    if (!method->options().HasExtension(file_response)) {
      GenerateAtlServerSendDefinition(*method, printer);
    }
  }

  printer->Print("\n");

  for (int i = 0; i < descriptor_->method_count(); i++) {
    const MethodDescriptor *method = descriptor_->method(i);
    if (!method->options().HasExtension(file_response)) {
      GenerateAtlServerImplDefinition(*method, printer, forHeader);
    }
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
  string lcname = cmsg::CamelToLower(method.name());
  string lcfullname = cmsg::FullNameToLower(descriptor_->full_name());
  vars_["method"] = lcname;
  vars_["input_typename"] = cmsg::FullNameToC(method.input_type()->full_name());
  vars_["output_typename"] = cmsg::FullNameToC(method.output_type()->full_name());
  vars_["padddddddddddddddddddddddd"] = cmsg::ConvertToSpaces(lcfullname + "_server_" + lcname);

  printer->Print(vars_,
                 "void $lcfullname$_server_$method$ ($cname$_Service *_service,\n"
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
  vars_["method_input"] = cmsg::FullNameToC(method.input_type()->full_name());

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

void AtlCodeGenerator::GenerateAtlServerSendDefinition(const MethodDescriptor &method, io::Printer* printer)
{
  string lcname = CamelToLower(method.name());
  vars_["method"] = lcname;
  vars_["method_output"] = cmsg::FullNameToC(method.output_type()->full_name());
  vars_["method_output_upper"] = cmsg::FullNameToUpper(method.output_type()->full_name());

  printer->Print(vars_, "static inline void\n$lcfullname$_server_$method$Send (const void *_service");
  if (method.output_type()->field_count() > 0)
  {
    printer->Print(vars_, ", const $method_output$ *send_msg");
  }
  printer->Print(")\n");
  printer->Print("{\n");
  printer->Indent();
  if (method.output_type()->field_count() == 0)
  {
    printer->Print(vars_, "$method_output$ send_msg = $method_output_upper$_INIT;\n");
    vars_["send_msg_name"] = "&send_msg";
  }
  else
  {
    vars_["send_msg_name"] = "send_msg";
  }
  printer->Print(vars_,"cmsg_server_send_response ((const struct ProtobufCMessage *) ($send_msg_name$), _service);\n");
  printer->Outdent();
  printer->Print("}\n\n");
}

//
// Utility methods
//
string AtlCodeGenerator::GetAtlClosureFunctionName(const MethodDescriptor &method)
{
  string closure_name = "handle_" + cmsg::FullNameToLower(method.full_name()) + "_response";
  return closure_name;
}

// This generates a server impl stub function that initialises a response message of the correct type
// and sends it back empty.
void AtlCodeGenerator::GenerateAtlServerImplStub(const MethodDescriptor &method, io::Printer* printer)
{
  string lcname = CamelToLower(method.name());
  vars_["method"] = lcname;
  vars_["method_input"] = cmsg::FullNameToC(method.input_type()->full_name());
  vars_["method_output"] = cmsg::FullNameToC(method.output_type()->full_name());
  vars_["method_output_upper"] = cmsg::FullNameToUpper(method.output_type()->full_name());

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

}  // namespace c
}  // namespace compiler
}  // namespace protobuf
}  // namespace google
