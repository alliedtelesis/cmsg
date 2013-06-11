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

#include <google/protobuf/compiler/c/c_atl_generator.h>
#include <google/protobuf/compiler/c/c_helpers.h>
#include <google/protobuf/io/printer.h>

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

// Header stuff.
void AtlCodeGenerator::GenerateMainHFile(io::Printer* printer)
{
  // first check if we need to generate any structs from the input and/or output messages
    // note: need to do this before the api is generated as the api will take these
    // structs as parameters.
    //printer->Print("\n/* Start of Struct definitions \n");
    //GenerateAtlStructDefinitions(printer);
    //printer->Print("\nEnd of Struct definitions */\n");

    // next, generate the api declaration
    printer->Print("\n/* Start of API definition */\n\n");
    GenerateAtlApiDefinitions(printer, true);
    printer->Print("\n/* End of API definition */\n");

    // next, generate the server declaration
    printer->Print("\n/* Start of Server definition */\n\n");
    GenerateAtlServerDefinitions(printer, true);
    printer->Print("\n/* End of Server definition */\n");

    // finally dump out the message structures to help with debug
    printer->Print("\n/* Start of Message description \n");
    DumpMessageDefinitions(printer);
    printer->Print("\nEnd of Message description */\n");

  //GenerateVfuncs(printer);
  //GenerateInitMacros(printer);
  //GenerateCallersDeclarations(printer);
  //GenerateAtlHeader(printer);
}
void AtlCodeGenerator::GenerateVfuncs(io::Printer* printer)
{
  printer->Print(vars_,
		 "typedef struct _$cname$_Service $cname$_Service;\n"
		 "struct _$cname$_Service\n"
		 "{\n"
		 "  ProtobufCService base;\n");
  for (int i = 0; i < descriptor_->method_count(); i++) {
    const MethodDescriptor *method = descriptor_->method(i);
    string lcname = CamelToLower(method->name());
    vars_["method"] = lcname;
    vars_["metpad"] = ConvertToSpaces(lcname);
    vars_["input_typename"] = FullNameToC(method->input_type()->full_name());
    vars_["output_typename"] = FullNameToC(method->output_type()->full_name());
    printer->Print(vars_,
                   "  void (*$method$)($cname$_Service *service,\n"
                   "         $metpad$  const $input_typename$ *input,\n"
                   "         $metpad$  $output_typename$_Closure closure,\n"
                   "         $metpad$  void *closure_data);\n");
  }
  printer->Print(vars_,
		 "};\n");
  printer->Print(vars_,
		 "typedef void (*$cname$_ServiceDestroy)($cname$_Service *);\n"
		 "void $lcfullname$__init ($cname$_Service *service,\n"
		 "     $lcfullpadd$        $cname$_ServiceDestroy destroy);\n");
}
void AtlCodeGenerator::GenerateInitMacros(io::Printer* printer)
{
  printer->Print(vars_,
		 "#define $ucfullname$__BASE_INIT \\\n"
		 "    { &$lcfullname$__descriptor, protobuf_c_service_invoke_internal, NULL }\n"
		 "#define $ucfullname$__INIT(function_prefix__) \\\n"
		 "    { $ucfullname$__BASE_INIT");
  for (int i = 0; i < descriptor_->method_count(); i++) {
    const MethodDescriptor *method = descriptor_->method(i);
    string lcname = CamelToLower(method->name());
    vars_["method"] = lcname;
    vars_["metpad"] = ConvertToSpaces(lcname);
    printer->Print(vars_,
                   ",\\\n      function_prefix__ ## $method$");
  }
  printer->Print(vars_,
		 "  }\n");
}
void AtlCodeGenerator::GenerateCallersDeclarations(io::Printer* printer)
{
  for (int i = 0; i < descriptor_->method_count(); i++) {
    const MethodDescriptor *method = descriptor_->method(i);
    string lcname = CamelToLower(method->name());
    string lcfullname = FullNameToLower(descriptor_->full_name());
    vars_["method"] = lcname;
    vars_["metpad"] = ConvertToSpaces(lcname);
    vars_["input_typename"] = FullNameToC(method->input_type()->full_name());
    vars_["output_typename"] = FullNameToC(method->output_type()->full_name());
    vars_["padddddddddddddddddd"] = ConvertToSpaces(lcfullname + "__" + lcname);
    printer->Print(vars_,
                   "void $lcfullname$__$method$(ProtobufCService *service,\n"
                   "     $padddddddddddddddddd$ const $input_typename$ *input,\n"
                   "     $padddddddddddddddddd$ $output_typename$_Closure closure,\n"
                   "     $padddddddddddddddddd$ void *closure_data);\n");
  }
}

void AtlCodeGenerator::GenerateAtlHeader(io::Printer* printer)
{


}

void AtlCodeGenerator::GenerateAtlStructDefinitions(io::Printer* printer)
{

  for (int i = 0; i < descriptor_->method_count(); i++) {
    const MethodDescriptor *method = descriptor_->method(i);
    string lcname = CamelToLower(method->name());
    vars_["method"] = lcname;

    if (MessageContainsSubMessages(printer, method->input_type()))
    {
      GenerateStructDefinitionsFromMessage(printer, method->input_type(), true);
    }
    printer->Print("\n");
    if (MessageContainsSubMessages(printer, method->output_type()))
    {
      GenerateStructDefinitionsFromMessage(printer, method->output_type(), true);
    }

    printer->Print("\n");
  }
}

void AtlCodeGenerator::GenerateStructDefinitionsFromMessage(io::Printer* printer, const Descriptor *message, bool firstMessage)
{

  vars_["message_name"] = FullNameToC(message->full_name());
  printer->Print(vars_, "struct $message_name$ {\n");
  printer->Indent();
  for (int i = 0; i < message->field_count(); i++) {
    const FieldDescriptor *field = message->field(i);
    if (field->type() == FieldDescriptor::TYPE_MESSAGE)
    {
      GenerateStructDefinitionsFromMessage(printer, field->message_type(), false);
      vars_["sub_name"] = FieldName(field);
    }
    else
    {
      vars_["field_name"] = FieldName(field);
      vars_["field_type"] = TypeToString(field->type());

      printer->Print(vars_, "$field_type$ $field_name$;\n");
    }
  }
  printer->Outdent();
  if (firstMessage)
  {
    printer->Print(vars_, "};\n");
  }
  else if (vars_["sub_name"] != "")
  {
    printer->Print(vars_, "} $sub_name$;\n");
  }
  else
  {
    printer->Print(vars_, "};\n");
  }
}

void AtlCodeGenerator::DumpMessageDefinitions(io::Printer* printer)
{
  for (int i = 0; i < descriptor_->method_count(); i++) {
    const MethodDescriptor *method = descriptor_->method(i);
    string lcname = CamelToLower(method->name());
    vars_["method"] = lcname;

    // print out the full message hierarchy to help with debug

    printer->Print(vars_, "Messages for rpc method \"$method$\":\n");
    printer->Print("Send ");
    PrintMessageFields(printer, method->input_type());
    printer->Print("\n");
    printer->Print("Return ");
    PrintMessageFields(printer, method->output_type());
    printer->Print("\n");
  }
}

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

  printer->Print(vars_, "int $lcfullname$_api_$method$(ProtobufC_RPC_Client *client");

  if (method.input_type()->field_count() > 0)
  {
    printer->Print(", ");
    GenerateParameterListFromMessage(printer, method.input_type(), false);
  }
  //
  // only add the rpc return message to the parameter list if its not empty
  if (method.output_type()->field_count() > 0)
  {
    printer->Print(", ");
    GenerateParameterListFromMessage(printer, method.output_type(), true);
  }
  printer->Print(")");
  if (forHeader)
  {
    printer->Print(";");
  }
  printer->Print("\n");

}

void AtlCodeGenerator::GenerateParameterListFromMessage(io::Printer* printer, const Descriptor *message, bool output)
{
  //if (message->field_count() <= 0) {
  //  vars_["message_name"] = FullNameToC(message->full_name());
  //  printer->Print(vars_, "$message_name$");
  //}
  //else {

    string prefix;
    if (output)
      prefix = "result_";
    else
      prefix = "";

    for (int i = 0; i < message->field_count(); i++) {
      const FieldDescriptor *field = message->field(i);

      if (!output)
      {
        if (field->type() == FieldDescriptor::TYPE_MESSAGE || field->type() == FieldDescriptor::TYPE_STRING)
          printer->Print("const ");
      }

      vars_["field_name"] = prefix + FieldName(field);
      // if the field type is message we will print this out as a struct
      // ie we will print field_type (struct), message_name (struct name), and field_name (variable name)
      if (field->type() == FieldDescriptor::TYPE_MESSAGE) {
        vars_["message_name"] = FullNameToC(field->message_type()->full_name());
        printer->Print(vars_, "$message_name$");
        printer->Print(vars_, " *$field_name$");
      }
      else
      {
        //vars_["field_type"] = TypeToString(field->type());
        printer->Print("$field_type$", "field_type", TypeToString(field->type()));
        if (field->type() != FieldDescriptor::TYPE_STRING)
        {
          printer->Print(" ");
        }
        if (output)
        {
          printer->Print("*"); //output parms are always pointers
        }
        printer->Print(vars_, "$field_name$");
      }

      // if there are more fields to print, add a comma and space before the next one
      if ( i < (message->field_count() - 1))
      {
        printer->Print(", ");
      }
    }
  //}
}

void AtlCodeGenerator::GenerateImplParameterListFromMessage(io::Printer* printer, const Descriptor *message, const string prefix, bool output)
{
    vars_["prefix"] = prefix;

    for (int i = 0; i < message->field_count(); i++) {
      const FieldDescriptor *field = message->field(i);

      vars_["field_name"] = FieldName(field);
      // if the field type is message we will print this out as a struct
      // ie we will print field_type (struct), message_name (struct name), and field_name (variable name)
      if (field->type() == FieldDescriptor::TYPE_MESSAGE) {
        vars_["message_name"] = FullNameToC(field->message_type()->full_name());
        printer->Print(vars_, "$prefix$$field_name$");
      }
      else
      {
        if (output)
        {
          printer->Print(vars_, "&"); //output parms are always pointers
        }
        printer->Print(vars_, "$prefix$$field_name$");
      }

      // if there are more fields to print, add a comma and space before the next one
      if ( i < (message->field_count() - 1))
      {
        printer->Print(", ");
      }
    }
}

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
    	description = "bool_t";
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
    	description = "uint8_t *";
    	break;
    case FieldDescriptor::TYPE_UINT32:
    	description = "uint32_t";
    	break;
    case FieldDescriptor::TYPE_ENUM:
    	description = "uint8_t";
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
    default:
    	break;
	}
	return description;
}

bool AtlCodeGenerator::MessageContainsSubMessages(io::Printer* printer, const Descriptor *message)
{
  for (int i = 0; i < message->field_count(); i++)
  {
    const FieldDescriptor *field = message->field(i);
    if (field->type() == FieldDescriptor::TYPE_MESSAGE)
    {
      return true;
    }
  }
  return false;
}

bool AtlCodeGenerator::MessageContainsRepeatedFields(io::Printer* printer, const Descriptor *message)
{
  int i = 0;
  bool found = false;
  while ((i < message->field_count()) && (found == false))
  {
    const FieldDescriptor *field = message->field(i);
    if (field->is_repeated())
    {
      found = true;
    }
    else if(field->type() == FieldDescriptor::TYPE_MESSAGE)
    {
      found = MessageContainsRepeatedFields(printer, field->message_type());
    }
    i++;
  }
  return found;
}

void AtlCodeGenerator::GenerateDescriptorDeclarations(io::Printer* printer)
{
  printer->Print(vars_, "extern const ProtobufCServiceDescriptor $lcfullname$__descriptor;\n");
}


// Source file stuff.
void AtlCodeGenerator::GenerateCFile(io::Printer* printer)
{
  printer->Print("\n/* Start of API Implementation */\n\n");
  GenerateAtlApiImplementation(printer);
  printer->Print("\n/* End of API Implementation */\n");

  printer->Print("\n/* Start of Server Implementation */\n\n");
  GenerateAtlServerImplementation(printer);
  printer->Print("\n/* End of Server Implementation */\n");
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
    //
    // we need to generate a closure function for the api to call on return
    // of the rpc call from the server
    //
    GenerateAtlApiClosureFunction(*method, printer);
    //
    // next generate the api function
    //
    // get the definition
    GenerateAtlApiDefinition(*method, printer, false);

    //start filling it in
    printer->Print("{\n");
    printer->Indent();
    printer->Print(vars_, "$output_typename$_pbc msgR;\n");
    printer->Print(vars_, "$input_typename$_pbc msgS = $input_typename_upper$_PBC__INIT;\n");
    printer->Print(vars_, "ProtobufCService *service = (ProtobufCService *)client;\n");

    //
    // if our return type has repeated fields, we'll need a counter to loop over the array of pointers
    //
    if (MessageContainsRepeatedFields (printer, method->output_type()))
    {
      printer->Print("int i = 0;\n");
    }

    //
    // copy the input parameters into the outgoing message
    //
    printer->Print("\n");
    GenerateMessageCopyCode(method->input_type(), "msgS.", "", printer, false, true);
    printer->Print("\n");

    //
    // now send!
    //
    vars_["closure_name"] = GetAtlClosureFunctionName(*method);
    vars_["lcfullname"] = FullNameToLower(descriptor_->full_name());
    vars_["method_lcname"] = CamelToLower(method->name());
    printer->Print(vars_, "$lcfullname$__$method_lcname$ (service, &msgS, $closure_name$, &msgR);\n\n");

    //
    // copy the return values
    //
    //GenerateReceiveMessageCopyCode(method->output_type(), "msgR",  printer);
    GenerateMessageCopyCode(method->output_type(), "*result_", "msgR.", printer, false);
    printer->Print("\n");

    //
    // now the return values are copied we need to cleanup our temporary memory used to
    // transfer values in the closure function
    //
    GenerateCleanupMessageMemoryCode(method->output_type(), "msgR.", printer);
    //
    // finally return something
    //
    printer->Print("return 0;\n");
    printer->Outdent();
    printer->Print("}\n\n");

  }

}

void AtlCodeGenerator::GenerateAtlApiClosureFunction(const MethodDescriptor &method, io::Printer* printer)
{
  vars_["method"] = FullNameToLower(method.full_name());
  vars_["input_typename"] = FullNameToC(method.input_type()->full_name());
  vars_["output_typename"] = FullNameToC(method.output_type()->full_name());
  vars_["closure_name"] = GetAtlClosureFunctionName(method);

  printer->Print(vars_, "static void $closure_name$ (const $output_typename$_pbc *result, void *closure_data)\n");
  printer->Print("{\n");
  printer->Indent();
  printer->Print(vars_, "$output_typename$_pbc *cdata = ($output_typename$_pbc *)closure_data;\n");
  //
  // if our return type has repeated fields, we'll need a counter to loop over the array of pointers
  //
  if (MessageContainsRepeatedFields (printer, method.output_type()))
  {
    printer->Print("int i = 0;\n");
  }
  //
  // copy data from response message (result) to the closure data structure
  //
  GenerateMessageCopyCode(method.output_type(), "cdata->", "result->", printer, true);
  printer->Outdent();
  printer->Print("}\n\n");

}

void AtlCodeGenerator::GenerateAtlServerImplementation(io::Printer* printer)
{
  //
  // Service initialization
  //
  printer->Print(vars_, "$cname$_Service $lcfullname$_service = $ucfullname$__INIT($lcfullname$_server_);\n\n");

  //
  // go through all rpc methods defined for this service and generate the server function
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
    // Generate the server function
    //
    // get the definition
    GenerateAtlServerDefinition(*method, printer, false);

    // start filling it in
    printer->Print("{\n");
    printer->Indent();
    printer->Print(vars_, "$input_typename$ user_input = $input_typename_upper$__INIT;\n");

    printer->Print("\n");
    printer->Print("if (input == NULL)\n");
    printer->Print("{\n");
    printer->Indent();
    printer->Print("closure(NULL, closure_data);\n");
    printer->Print("return;\n");
    printer->Outdent();
    printer->Print("}\n");

    printer->Print("\n");
    printer->Print("// these are needed in 'Send' function for sending reply back to the client\n");
    printer->Print("service->closure = closure;\n");
    printer->Print("service->closure_data = closure_data;\n");
    printer->Print("\n");

    //
    // Prepare protobuf-c data struct to c for user impl
    //
    printer->Print("// convert input data from protobuf-c to pure user struct\n");
    GenerateMessageCopyCode(method->input_type(), "user_input.", "input->", printer, false);

    //
    // call _impl user function
    //
    printer->Print("\n");
    printer->Print("// call user-defined server implementation\n");
    printer->Print(vars_, "$lcfullname$_impl_$method$(service");
    if (method->input_type()->field_count() > 0)
    {
      printer->Print(", ");
      GenerateImplParameterListFromMessage(printer, method->input_type(), "user_input.", false);
    }
    printer->Print(");\n");
    printer->Print("\n");

    //
    // call closure()
    //
    printer->Print("// clean up\n");
    printer->Print("service->closure = NULL;\n");
    printer->Print("service->closure_data = NULL;\n");
    printer->Print("\n");

    // end of the function
    printer->Outdent();
    printer->Print("}\n\n");

    // we need to generate a closure function for the api to call on return
    // of the rpc call from the server
    //
    GenerateAtlServerSendImplementation(*method, printer);

    // FIXME: This is for a temporary work around to compile client properly
    // while client-side and server-side code is in the same file.
    printer->Print("// user-defined server implementation (place holder)\n");
    printer->Print("__attribute__ ((weak))\n");
    GenerateAtlServerImplDefinition(*method, printer, false);
    printer->Print("{\n");
    printer->Indent();
    printer->Print("return 0;\n");
    printer->Outdent();
    printer->Print("}\n");
    printer->Print("\n");

  }

}

void AtlCodeGenerator::GenerateAtlServerDefinitions(io::Printer* printer, bool forHeader)
{
  printer->Print(vars_, "extern $cname$_Service $lcfullname$_service;\n");

  for (int i = 0; i < descriptor_->method_count(); i++) {
    const MethodDescriptor *method = descriptor_->method(i);
    GenerateAtlServerDefinition(*method, printer, forHeader);
    GenerateAtlServerSendDefinition(*method, printer, forHeader);
  }

  printer->Print("\n");

  for (int i = 0; i < descriptor_->method_count(); i++) {
    const MethodDescriptor *method = descriptor_->method(i);
    GenerateAtlServerImplDefinition(*method, printer, forHeader);
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
                 "void $lcfullname$_server_$method$($cname$_Service *service,\n"
                 "     $padddddddddddddddddddddddd$ const $input_typename$_pbc *input,\n"
                 "     $padddddddddddddddddddddddd$ $output_typename$_pbc_Closure closure,\n"
                 "     $padddddddddddddddddddddddd$ void *closure_data)");
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

  printer->Print(vars_, "int $lcfullname$_impl_$method$(const void *service");
  if (method.input_type()->field_count() > 0)
  {
    printer->Print(", ");
    GenerateParameterListFromMessage(printer, method.input_type(), false);
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

  GenerateAtlServerSendDefinition(method, printer, false);

  printer->Print("{\n");
  printer->Indent();

  printer->Print(vars_, "$output_typename$_pbc_Closure closure = ((const $cname$_Service *)service)->closure;\n");
  printer->Print(vars_, "void *closure_data = ((const $cname$_Service *)service)->closure_data;\n");
  printer->Print(vars_, "$output_typename$_pbc result = $output_typename_upper$_PBC__INIT;\n");
  printer->Print("\n");

  //
  // copy data from response message (result) to the closure data structure
  //
  GenerateMessageCopyCode(method.output_type(), "result.", "", printer, true, true);

  printer->Print("\n");
  printer->Print(vars_, "closure(&result, closure_data);\n");

  printer->Print("\n");
  GenerateCleanupMessageMemoryCode(method.output_type(), "result.", printer);

  printer->Outdent();
  printer->Print("}\n\n");
}

void AtlCodeGenerator::GenerateAtlServerSendDefinition(const MethodDescriptor &method, io::Printer* printer, bool forHeader)
{
  string lcname = CamelToLower(method.name());
  vars_["method"] = lcname;

  printer->Print(vars_, "void $lcfullname$_server_$method$Send(const void *service");
  if (method.output_type()->field_count() > 0)
  {
    printer->Print(", ");
    GenerateParameterListFromMessage(printer, method.output_type(), false);
  }
  printer->Print(")");
  if (forHeader)
  {
    printer->Print(";");
  }
  printer->Print("\n");
}


string AtlCodeGenerator::GetAtlClosureFunctionName(const MethodDescriptor &method)
{
  string closure_name = "handle_" + FullNameToLower(method.full_name()) + "_response";
  return closure_name;
}

void AtlCodeGenerator::GenerateSendMessageCopyCode(const Descriptor *message, const string message_name, io::Printer *printer)
{
  //
  // walk through the fields in "message" and output code that copies the fields from an
  // identical message "message_name" to the equivalent fields in "message"
  //
  vars_["message_name"] = message_name;
  for (int i = 0; i < message->field_count(); i++) {
    const FieldDescriptor *field = message->field(i);
    vars_["field_name"] = FieldName(field);
    printer->Print(vars_, "$message_name$.$field_name$ = $field_name$;\n");
    if (field->is_optional() &&
        !((field->type() == FieldDescriptor::TYPE_MESSAGE) || (field->type() == FieldDescriptor::TYPE_STRING)))
    {
      printer->Print(vars_, "$message_name$.has_$field_name$ = 1;\n");
    }
  }
}

void AtlCodeGenerator::GenerateMessageCopyCode(const Descriptor *message, const string lhm, const string rhm, io::Printer *printer, bool allocate_memory, bool send)
{

  //
  // walk through the message structure and create code that copies from the rhm to the lhm.
  // for structures we need to deep copy - we can't just copy pointers
  //
  for (int i = 0; i < message->field_count(); i++)
  {
    const FieldDescriptor *field = message->field(i);
    string field_name = FieldName(field);

    vars_["left_field_name"] = lhm + field_name;
    vars_["right_field_name"] = rhm + field_name;

    if (field->is_repeated())
    {
      //
      // if a field is repeated it will appear in 'C' as a 2 dimensional array
      // ie an array of pointers to pointers of the type
      // we will need to loop the array and free each field
      //
      vars_["left_field_count"] = lhm + "n_" + field_name;
      vars_["right_field_count"] = rhm + "n_" + field_name;
      vars_["message_name"] = FullNameToC(field->message_type()->full_name());

      printer->Print(vars_, "$left_field_count$ = $right_field_count$;\n");
      if (allocate_memory)
      {
        printer->Print(vars_, "$left_field_name$ = malloc($left_field_count$ * sizeof($message_name$));\n");
      }
      printer->Print(vars_, "for (i = 0; i < $left_field_count$; i++)\n");
      printer->Print("{\n");
      printer->Indent();

      //
      // now copy over each field in the structures in the repeated array
      //
      GenerateMessageCopyCode(field->message_type(), lhm + field_name + "[i]->", rhm + field_name + "[i]->", printer, allocate_memory, send);
      printer->Outdent();
      printer->Print("}\n");

    }
    else if (field->type() == FieldDescriptor::TYPE_STRING)
    {
      //
      // strings are represented as char* so we need to allocate memory
      // and then do a strcpy
      printer->Print(vars_, "if ($right_field_name$ != NULL)\n");
      printer->Print("{\n");
      printer->Indent();
      if (allocate_memory)
      {
        printer->Print(vars_, "$left_field_name$ = malloc (strlen ($right_field_name$) + 1);\n");
      }
      printer->Print(vars_, "strncpy ($left_field_name$, $right_field_name$, strlen($right_field_name$));\n");
      printer->Outdent();
      printer->Print("}\n");
    }
    else if (field->type() != FieldDescriptor::TYPE_MESSAGE)
    {
      bool indented = false;

      if (field->is_optional() &&
          !((field->type() == FieldDescriptor::TYPE_MESSAGE) || (field->type() == FieldDescriptor::TYPE_STRING)))
      {
        if (send)
        {
          vars_["has_field_name"] = lhm + "has_" + field_name;
          printer->Print(vars_, "$has_field_name$ = 1;\n");
        }
        else
        {
          vars_["has_field_name"] = rhm + "has_" + field_name;
          printer->Print(vars_, "if ($has_field_name$)\n");
          printer->Indent();
          indented = true;
        }
      }
      printer->Print(vars_, "$left_field_name$ = $right_field_name$;\n");

      if (indented)
        printer->Outdent();
    }
    else
    {
      // this is a sub message so we need to follow pointers into the sub structure.
      // first, we need to allocate memory here for this structure
      //
      vars_["message_name"] = FullNameToC(field->message_type()->full_name());
      if (allocate_memory)
      {
        printer->Print(vars_, "$left_field_name$ = malloc (sizeof ($message_name$));\n");
      }
      GenerateMessageCopyCode(field->message_type(), "(" + lhm + field_name + ")->", "(" + rhm + field_name + ")->", printer, allocate_memory, send);
    }
  }
}

void AtlCodeGenerator::GenerateCleanupMessageMemoryCode(const Descriptor *message, const string lhm, io::Printer *printer)
{

  //
  // walk through the message structure and create code that copies from the rhm to the lhm.
  // for structures we need to deep copy - we can't just copy pointers
  //
  for (int i = 0; i < message->field_count(); i++)
  {
    const FieldDescriptor *field = message->field(i);
    string field_name = FieldName(field);
    vars_["left_field_name"] = lhm + field_name;

    if (field->is_repeated())
    {
      //
      // if a field is repeated it will appear in 'C' as a 2 dimensional array
      // ie an array of pointers to pointers of the type
      // we will need to loop the array and memcpy over each field
      //
      vars_["left_field_count"] = lhm + "n_" + field_name;
      vars_["message_name"] = FullNameToC(field->message_type()->full_name());

      printer->Print(vars_, "for (i = 0; i < $left_field_count$; i++)\n");
      printer->Print("{\n");
      printer->Indent();

      //
      // now copy over each field in the structures in the repeated array
      //
      GenerateCleanupMessageMemoryCode(field->message_type(), lhm + field_name + "[i]->", printer);
      printer->Outdent();
      printer->Print("}\n");
      vars_["left_field_name"] = lhm + field_name;
      printer->Print(vars_, "free ($left_field_name$);\n");
    }
    else if (field->type() == FieldDescriptor::TYPE_STRING)
    {
      //
      // strings are represented as char* so we need to delete the memory we allocated
      //
      printer->Print(vars_, "free ($left_field_name$);\n");
    }
    else if (field->type() == FieldDescriptor::TYPE_MESSAGE)
    {
      // this is a sub message so we need to follow pointers into the sub structure.
      // TODO: do we need to allocate memory here?
      //
      GenerateCleanupMessageMemoryCode(field->message_type(), lhm + field_name + "->", printer);
      vars_["left_field_name"] = lhm + field_name;
      printer->Print(vars_, "free ($left_field_name$);\n");
    }
  }
}


void AtlCodeGenerator::GenerateReceiveMessageCopyCode(const Descriptor *message, const string message_name, io::Printer *printer)
{
  //
  // walk through the fields in "message" and output code that copies the fields from an
  // identical message "message_name" to the equivalent fields in "message"
  //
  vars_["message_name"] = message_name;
  for (int i = 0; i < message->field_count(); i++) {
    const FieldDescriptor *field = message->field(i);
    vars_["field_name"] = FieldName(field);
    printer->Print(vars_, "*$field_name$ = $message_name$.$field_name$;\n");
  }
}

void AtlCodeGenerator::GenerateInit(io::Printer* printer)
{
  printer->Print(vars_,
		 "void $lcfullname$__init ($cname$_Service *service,\n"
		 "     $lcfullpadd$        $cname$_ServiceDestroy destroy)\n"
		 "{\n"
		 "  protobuf_c_service_generated_init (&service->base,\n"
		 "                                     &$lcfullname$__descriptor,\n"
		 "                                     (ProtobufCServiceDestroy) destroy);\n"
		 "}\n");
}

struct MethodIndexAndName { unsigned i; const char *name; };
static int
compare_method_index_and_name_by_name (const void *a, const void *b)
{
  const MethodIndexAndName *ma = (const MethodIndexAndName *) a;
  const MethodIndexAndName *mb = (const MethodIndexAndName *) b;
  return strcmp (ma->name, mb->name);
}

void AtlCodeGenerator::GenerateServiceDescriptor(io::Printer* printer)
{
  int n_methods = descriptor_->method_count();
  MethodIndexAndName *mi_array = new MethodIndexAndName[n_methods];
  
  vars_["n_methods"] = SimpleItoa(n_methods);
  printer->Print(vars_, "static const ProtobufCMethodDescriptor $lcfullname$__method_descriptors[$n_methods$] =\n"
                       "{\n");
  for (int i = 0; i < n_methods; i++) {
    const MethodDescriptor *method = descriptor_->method(i);
    vars_["method"] = method->name();
    vars_["input_descriptor"] = "&" + FullNameToLower(method->input_type()->full_name()) + "__descriptor";
    vars_["output_descriptor"] = "&" + FullNameToLower(method->output_type()->full_name()) + "__descriptor";
    printer->Print(vars_,
             "  { \"$method$\", $input_descriptor$, $output_descriptor$ },\n");
    mi_array[i].i = i;
    mi_array[i].name = method->name().c_str();
  }
  printer->Print(vars_, "};\n");

  qsort ((void*)mi_array, n_methods, sizeof (MethodIndexAndName),
         compare_method_index_and_name_by_name);
  printer->Print(vars_, "const unsigned $lcfullname$__method_indices_by_name[] = {\n");
  for (int i = 0; i < n_methods; i++) {
    vars_["i"] = SimpleItoa(mi_array[i].i);
    vars_["name"] = mi_array[i].name;
    vars_["comma"] = (i + 1 < n_methods) ? "," : " ";
    printer->Print(vars_, "  $i$$comma$        /* $name$ */\n");
  }
  printer->Print(vars_, "};\n");

  printer->Print(vars_, "const ProtobufCServiceDescriptor $lcfullname$__descriptor =\n"
                       "{\n"
		       "  PROTOBUF_C_SERVICE_DESCRIPTOR_MAGIC,\n"
		       "  \"$fullname$\",\n"
		       "  \"$name$\",\n"
		       "  \"$cname$\",\n"
		       "  \"$package$\",\n"
		       "  $n_methods$,\n"
		       "  $lcfullname$__method_descriptors,\n"
		       "  $lcfullname$__method_indices_by_name\n"
		       "};\n");
}

void AtlCodeGenerator::GenerateCallersImplementations(io::Printer* printer)
{
  for (int i = 0; i < descriptor_->method_count(); i++) {
    const MethodDescriptor *method = descriptor_->method(i);
    string lcname = CamelToLower(method->name());
    string lcfullname = FullNameToLower(descriptor_->full_name());
    vars_["method"] = lcname;
    vars_["metpad"] = ConvertToSpaces(lcname);
    vars_["input_typename"] = FullNameToC(method->input_type()->full_name());
    vars_["output_typename"] = FullNameToC(method->output_type()->full_name());
    vars_["padddddddddddddddddd"] = ConvertToSpaces(lcfullname + "__" + lcname);
    vars_["index"] = SimpleItoa(i);
     
    printer->Print(vars_,
                   "void $lcfullname$__$method$(ProtobufCService *service,\n"
                   "     $padddddddddddddddddd$ const $input_typename$ *input,\n"
                   "     $padddddddddddddddddd$ $output_typename$_Closure closure,\n"
                   "     $padddddddddddddddddd$ void *closure_data)\n"
		   "{\n"
		   "  PROTOBUF_C_ASSERT (service->descriptor == &$lcfullname$__descriptor);\n"
		   "  service->invoke(service, $index$, (const ProtobufCMessage *) input, (ProtobufCClosure) closure, closure_data);\n"
		   "}\n");
  }
}


}  // namespace c
}  // namespace compiler
}  // namespace protobuf
}  // namespace google
