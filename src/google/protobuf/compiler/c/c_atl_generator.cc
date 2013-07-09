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
void AtlCodeGenerator::GenerateMainHFile(io::Printer* printer, bool api)
{
  // first check if we need to generate any structs from the input and/or output messages
    // note: need to do this before the api is generated as the api will take these
    // structs as parameters.
    //printer->Print("\n/* Start of Struct definitions \n");
    //GenerateAtlStructDefinitions(printer);
    //printer->Print("\nEnd of Struct definitions */\n");

  if (api)
  {
    // next, generate the api declaration
    printer->Print("\n/* Start of API definition */\n\n");
    GenerateAtlApiDefinitions(printer, true);
    printer->Print("\n/* End of API definition */\n");
  }
  else
  {
    // next, generate the server declaration
    printer->Print("\n/* Start of Server definition */\n\n");
    GenerateAtlServerDefinitions(printer, true);
    printer->Print("\n/* End of Server definition */\n");
  }

    // finally dump out the message structures to help with debug
    //printer->Print("\n/* Start of Message description \n");
    //DumpMessageDefinitions(printer);
    //printer->Print("\nEnd of Message description */\n");

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
		 "void $lcfullname$_init ($cname$_Service *service,\n"
		 "     $lcfullpadd$        $cname$_ServiceDestroy destroy);\n");
}
void AtlCodeGenerator::GenerateInitMacros(io::Printer* printer)
{
  printer->Print(vars_,
		 "#define $ucfullname$_BASE_INIT \\\n"
		 "    { &$lcfullname$_descriptor, protobuf_c_service_invoke_internal, NULL }\n"
		 "#define $ucfullname$_INIT(function_prefix_) \\\n"
		 "    { $ucfullname$_BASE_INIT");
  for (int i = 0; i < descriptor_->method_count(); i++) {
    const MethodDescriptor *method = descriptor_->method(i);
    string lcname = CamelToLower(method->name());
    vars_["method"] = lcname;
    vars_["metpad"] = ConvertToSpaces(lcname);
    printer->Print(vars_,
                   ",\\\n      function_prefix_ ## $method$");
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
    vars_["padddddddddddddddddd"] = ConvertToSpaces(lcfullname + "_" + lcname);
    printer->Print(vars_,
                   "void $lcfullname$_$method$(ProtobufCService *service,\n"
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

  printer->Print(vars_, "int $lcfullname$_api_$method$(cmsg_client *_client");

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

    for (int i = 0; i < message->field_count(); i++) {
      const FieldDescriptor *field = message->field(i);

      vars_["field_name"] = (output ? "result_" : "") + FieldName(field);
      vars_["n_field_name"] = (output ? "result_n_" : "n_") + FieldName(field);

      // need special handling for enums and messages as the type name we print isn't one
      // of the primitive types, but the type name given in the proto file
      if ((field->type() == FieldDescriptor::TYPE_MESSAGE) ||
          (field->type() == FieldDescriptor::TYPE_ENUM))
      {
        if (field->is_repeated())
        {
          // if field is repeated, need to add a count/size parameter
          printer->Print(vars_, "size_t ");
          if (output)
          {
            printer->Print("*");
          }
          printer->Print(vars_, "$n_field_name$, ");
        }
        if (field->type() == FieldDescriptor::TYPE_MESSAGE)
        {
          vars_["message_name"] = FullNameToC(field->message_type()->full_name());
        }
        else if (field->type() == FieldDescriptor::TYPE_ENUM)
        {
          vars_["message_name"] = FullNameToC(field->enum_type()->full_name());
        }
        printer->Print(vars_, "$message_name$ *");
        if (output || field->is_repeated())
        {
          printer->Print("*");
        }
        printer->Print(vars_, "$field_name$");
      }
      else if (field->is_repeated())
      {
        // if field is repeated, need to add a count/size parameter
        printer->Print(vars_, "size_t ");
        if (output)
        {
          printer->Print("*");
        }
        printer->Print(vars_, "$n_field_name$, ");
        //
        // this will appear as a **
        //
        printer->Print("$field_type$", "field_type", TypeToString(field->type()));
        if (field->type() == FieldDescriptor::TYPE_STRING)
        {
          printer->Print("*"); //repeated chars are always double pointers so add 1 to the char *
        }
        else if (field->type() == FieldDescriptor::TYPE_BYTES)
        {
          printer->Print(" **");  // same as message with ProtobufCBinaryData
        }
        else if (output)
        {
          printer->Print(" **"); //repeated output fields are always double pointers
        }
        else
        {
          printer->Print(" *"); //repeated input fields are always single pointers
        }

        printer->Print(vars_, "$field_name$");
      }
      else if (field->type() == FieldDescriptor::TYPE_BYTES)
      {
        // use "field_name_len" and "field" for "bytes" type
        if (output)
        {
          printer->Print(vars_, "size_t *$field_name$_len, uint8_t **$field_name$");
        }
        else
        {
          printer->Print(vars_, "size_t $field_name$_len, uint8_t *$field_name$");
        }
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

void AtlCodeGenerator::GenerateImplParameterListFromMessage(io::Printer* printer, const Descriptor *message, const string prefix)
{
    for (int i = 0; i < message->field_count(); i++) {
      const FieldDescriptor *field = message->field(i);

      vars_["field_name"] = prefix + FieldName(field);
      vars_["n_field_name"] = prefix + "n_" + FieldName(field);

      if (field->is_repeated())
      {
        // if field is repeated, need to add a count/size parameter
        printer->Print(vars_, "$n_field_name$, ");
      }
      else if (field->type() == FieldDescriptor::TYPE_BYTES)
      {
        // use "field_name_len" and "field" for "bytes" type
        printer->Print(vars_, "$field_name$_len, ");
      }

      printer->Print(vars_, "$field_name$");

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
    	description = "ProtobufCBinaryData";
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
  printer->Print(vars_, "extern const ProtobufCServiceDescriptor $lcfullname$_descriptor;\n");
}


// Source file stuff.
void AtlCodeGenerator::GenerateCFile(io::Printer* printer, bool api)
{
  if (api)
  {
    printer->Print("\n/* Start of API Implementation */\n\n");
    GenerateAtlApiImplementation(printer);
    printer->Print("\n/* End of API Implementation */\n");
  }
  else
  {
    printer->Print("\n/* Start of local server definitions */\n\n");
    GenerateAtlServerCFileDefinitions(printer);
    printer->Print("\n/* End of local server definitions */\n\n");

    printer->Print("\n/* Start of Server Implementation */\n\n");
    GenerateAtlServerImplementation(printer);
    printer->Print("\n/* End of Server Implementation */\n");
  }
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
    // of the rpc call from the server just when we have a response with fields
    //
    if(method->output_type()->field_count() > 0)
    {
      GenerateAtlApiClosureFunction(*method, printer);
    }
    //
    // next generate the api function
    //
    // get the definition
    GenerateAtlApiDefinition(*method, printer, false);

    //start filling it in
    printer->Print("{\n");
    printer->Indent();
    //
    // don't create response when response has no fields
    //
    if(method->output_type()->field_count() > 0)
    {
      printer->Print(vars_, "$output_typename$_pbc _msgR;\n");
    }
    printer->Print(vars_, "$input_typename$_pbc _msgS = $input_typename_upper$_PBC_INIT;\n");
    printer->Print(vars_, "ProtobufCService *_service = (ProtobufCService *)_client;\n");

    //
    // copy the input parameters into the outgoing message
    //
    printer->Print("\n");
    printer->Print("/* Copy input variables to pbc send message */\n");
    GenerateMessageCopyCode(method->input_type(), "_msgS.", "", printer, true, true, true, false);
    printer->Print("\n");

    //
    // now send!
    //
    printer->Print("/* Send! */\n");
    vars_["closure_name"] = GetAtlClosureFunctionName(*method);
    vars_["lcfullname"] = FullNameToLower(descriptor_->full_name());
    vars_["method_lcname"] = CamelToLower(method->name());

    //
    // don't pass response callback and msg when response is empty
    //
    if(method->output_type()->field_count() > 0)
    {
      printer->Print(vars_, "$lcfullname$_$method_lcname$ (_service, &_msgS, $closure_name$, &_msgR);\n\n");
    }
    else
    {
      printer->Print(vars_, "$lcfullname$_$method_lcname$ (_service, &_msgS, NULL, NULL);\n\n");
    }

    //
    // to be tidy, cleanup the sent message memory
    //
    printer->Print("/* Free send message memory */\n");
    GenerateCleanupMessageMemoryCode(method->input_type(), "_msgS.", printer);
    printer->Print("\n");

    //
    // copy the return values
    //
    //GenerateReceiveMessageCopyCode(method->output_type(), "_msgR",  printer);
    printer->Print("/* Copy received message fields to output variables */\n");
    GenerateMessageCopyCode(method->output_type(), "result_", "_msgR.", printer, false, false, false, true);
    printer->Print("\n");

    //
    // now the return values are copied we need to cleanup our temporary memory used to
    // transfer values in the closure function
    //
    printer->Print("/* Free temporary receive message memory */\n");
    GenerateCleanupMessageMemoryCode(method->output_type(), "_msgR.", printer);
    printer->Print("\n");

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
  GenerateMessageCopyCode(method.output_type(), "cdata->", "result->", printer, true, false, true, false);
  printer->Outdent();
  printer->Print("}\n\n");

}

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
    if (method->input_type()->field_count() > 0)
    {
      printer->Print(vars_, "$input_typename$ user_input = $input_typename_upper$_INIT;\n");
    }


    printer->Print("\n");
    printer->Print("if (input == NULL)\n");
    printer->Print("{\n");
    printer->Indent();
    printer->Print("_closure(NULL, _closure_data);\n");
    printer->Print("return;\n");
    printer->Outdent();
    printer->Print("}\n");

    printer->Print("\n");
    printer->Print("// these are needed in 'Send' function for sending reply back to the client\n");
    printer->Print("_service->closure = _closure;\n");
    printer->Print("_service->closure_data = _closure_data;\n");
    printer->Print("\n");

    //
    // Prepare protobuf-c data struct to c for user impl
    //
    printer->Print("// convert input data from protobuf-c to pure user struct\n");
    GenerateMessageCopyCode(method->input_type(), "user_input.", "input->", printer, true, false, false, false);

    //
    // call _impl user function
    //
    printer->Print("\n");
    printer->Print("// call user-defined server implementation\n");
    printer->Print(vars_, "$lcfullname$_impl_$method$(_service");
    if (method->input_type()->field_count() > 0)
    {
      printer->Print(", ");
      GenerateImplParameterListFromMessage(printer, method->input_type(), "user_input.");
    }
    printer->Print(");\n");
    printer->Print("\n");

    //
    // call closure()
    //
    printer->Print("// clean up\n");
    printer->Print("_service->closure = NULL;\n");
    printer->Print("_service->closure_data = NULL;\n");
    printer->Print("\n");

    GenerateCleanupMessageMemoryCode(method->input_type(), "user_input.", printer);
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
                 "void $lcfullname$_server_$method$($cname$_Service *_service,\n"
                 "     $padddddddddddddddddddddddd$ const $input_typename$_pbc *input,\n"
                 "     $padddddddddddddddddddddddd$ $output_typename$_pbc_Closure _closure,\n"
                 "     $padddddddddddddddddddddddd$ void *_closure_data)");
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

  printer->Print(vars_, "int $lcfullname$_impl_$method$(const void *_service");
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

  printer->Print(vars_, "$output_typename$_pbc_Closure _closure = ((const $cname$_Service *)_service)->closure;\n");
  printer->Print(vars_, "void *_closure_data = ((const $cname$_Service *)_service)->closure_data;\n");
  printer->Print(vars_, "$output_typename$_pbc _result = $output_typename_upper$_PBC_INIT;\n");
  printer->Print("\n");

  //
  // copy data from response message (_result) to the closure data structure
  //
  GenerateMessageCopyCode(method.output_type(), "_result.", "", printer, true, true, true, false);

  printer->Print("\n");
  printer->Print(vars_, "_closure(&_result, _closure_data);\n");

  printer->Print("\n");
  GenerateCleanupMessageMemoryCode(method.output_type(), "_result.", printer);

  printer->Outdent();
  printer->Print("}\n\n");
}

void AtlCodeGenerator::GenerateAtlServerSendDefinition(const MethodDescriptor &method, io::Printer* printer, bool forHeader)
{
  string lcname = CamelToLower(method.name());
  vars_["method"] = lcname;

  printer->Print(vars_, "void $lcfullname$_server_$method$Send(const void *_service");
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

void AtlCodeGenerator::GenerateMessageCopyCode(const Descriptor *message, const string lhm, const string rhm, io::Printer *printer, bool allocate_memory, bool send, bool to_pbc, bool result_ref, int depth)
{
  //
  // walk through the message structure and create code that copies from the rhm to the lhm.
  // for structures we need to deep copy - we can't just copy pointers
  //
  for (int i = 0; i < message->field_count(); i++)
  {
    const FieldDescriptor *field = message->field(i);
    string field_name = FieldName(field);

    vars_["field_name"] = field->name();
    vars_["left_field_name"] = lhm + field_name;
    vars_["right_field_name"] = rhm + field_name;
    // Use "*" to copy result value of primitive types for _api function call
    vars_["result_ref"] = result_ref ? "*" : "";


    if (field->is_repeated())
    {
      //
      // if a field is repeated it will appear in 'C' as a 2 dimensional array
      // ie an array of pointers to pointers of the type
      // we will need to loop the array and free each field
      //
      printer->Print(vars_, "if ($right_field_name$ != NULL)\n");
      printer->Print("{\n");
      printer->Indent();

      vars_["i"] = "_i" + ((depth > 0) ? SimpleItoa(depth) : "");
      printer->Print(vars_, "int $i$ = 0;\n");

      vars_["left_field_count"] = lhm + "n_" + field_name;
      vars_["right_field_count"] = rhm + "n_" + field_name;
      if (field->type() == FieldDescriptor::TYPE_MESSAGE)
      {
        vars_["message_name"] = FullNameToC(field->message_type()->full_name());
        if (to_pbc)
        {
          vars_["message_name"] = vars_["message_name"] + "_pbc";
        }
      }
      else
      {
        vars_["message_name"] = TypeToString(field->type());
      }

      printer->Print(vars_, "$result_ref$$left_field_count$ = $right_field_count$;\n");
      if (allocate_memory)
      {
        if (field->type() == FieldDescriptor::TYPE_STRING)
        {
          printer->Print(vars_, "$left_field_name$ = malloc ($result_ref$$left_field_count$ * sizeof($message_name$));\n");
        }
        else
        {
          printer->Print(vars_, "$left_field_name$ = malloc ($result_ref$$left_field_count$ * sizeof($message_name$ *));\n");
        }
      }
      printer->Print(vars_, "for ($i$ = 0; $i$ < $result_ref$$left_field_count$; $i$++) // repeated \"$field_name$\"\n");
      printer->Print("{\n");
      printer->Indent();

      //
      // now copy over each field in the structures in the repeated array
      //
      if (field->type() == FieldDescriptor::TYPE_MESSAGE)
      {
        if (allocate_memory)
        {
          printer->Print(vars_, "$left_field_name$[$i$] = malloc (sizeof($message_name$));\n");
        }

        // if this is a pbc struct, we need to init it before use
        if (to_pbc)
        {
          vars_["lcclassname"] = FullNameToLower(field->message_type()->full_name());
          printer->Print(vars_, "$lcclassname$_init($left_field_name$[$i$]);\n");
        }
        GenerateMessageCopyCode(field->message_type(),
                                lhm + field_name + "[" + vars_["i"] + "]->",
                                rhm + field_name + "[" + vars_["i"] + "]->",
                                printer, allocate_memory, send, to_pbc, false, depth + 1);

      }
      else
      {
        if (field->type() == FieldDescriptor::TYPE_STRING)
        {
          if (allocate_memory)
          {
            printer->Print(vars_, "$left_field_name$[$i$] = malloc (strlen ($right_field_name$[$i$]) + 1);\n");
          }
          printer->Print(vars_, "strcpy ($left_field_name$[$i$], $right_field_name$[$i$]);\n");
        }
        else if (field->type() == FieldDescriptor::TYPE_BYTES)
        {
          if (allocate_memory)
          {
            printer->Print(vars_, "($left_field_name$)[$i$]->data = malloc ($right_field_name$[$i$]->len * sizeof(uint8_t));\n");
          }
          printer->Print(vars_, "memcpy (($left_field_name$)[$i$]->data, $right_field_name$[$i$]->data, $right_field_name$[$i$]->len);\n");
          printer->Print(vars_, "($left_field_name$)[$i$]->len = $right_field_name$[$i$]->len;\n");
        }
        else
        {
          printer->Print(vars_, "$result_ref$$left_field_name$[$i$] = $right_field_name$[$i$];\n");
        }
      }
      printer->Outdent();
      printer->Print("}\n"); //for loop
      printer->Outdent();
      printer->Print("}\n"); //if right_field_name != NULL

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
      printer->Print(vars_, "strcpy ($result_ref$$left_field_name$, $right_field_name$);\n");
      printer->Outdent();
      printer->Print("}\n");
    }
    else if (field->type() == FieldDescriptor::TYPE_BYTES)
    {
      printer->Print(vars_, "if ($right_field_name$ != NULL)\n");
      printer->Print("{\n");
      printer->Indent();
      if (allocate_memory)
      {
        printer->Print(vars_, "$left_field_name$ = malloc ($right_field_name$_len * sizeof(uint8_t));\n");
      }
      printer->Print(vars_, "memcpy ($left_field_name$, $right_field_name$, $right_field_name$_len);\n");
      printer->Print(vars_, "$result_ref$$left_field_name$_len = $right_field_name$_len;\n");
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
      printer->Print(vars_, "$result_ref$$left_field_name$ = $right_field_name$;\n");

      if (indented)
        printer->Outdent();
    }
    else
    {
      // this is a sub message so we need to follow pointers into the sub structure.
      // first, we need to allocate memory here for this structure
      //
      if (allocate_memory)
      {
        if (send || to_pbc)
        {
          // send messages are always _pbc structures, so make sure we allocate memory for
          // a _pbc and not an ATL struct (_pbc are bigger than ATL structs)
          vars_["message_name"] = FullNameToC(field->message_type()->full_name()) + "_pbc";
          printer->Print(vars_, "$left_field_name$ = malloc (sizeof ($message_name$));\n");
          // then if we are sending the struct we must init it
          if (send)
          {
            vars_["lcclassname"] = FullNameToLower(field->message_type()->full_name());
            printer->Print(vars_, "$lcclassname$_init($left_field_name$);\n");
          }
        }
        else
        {
          vars_["message_name"] = FullNameToC(field->message_type()->full_name());
          // no need to initialise the struct as the user has to provide values for all
          // the fields anyway so just the malloc should be fine.
          // note: we are not planning on sending this struct so lack of a pbc descriptor
          // (provided by the INIT) shouldn't be a problem
          printer->Print(vars_, "$left_field_name$ = malloc (sizeof ($message_name$));\n");
        }

      }

      GenerateMessageCopyCode(field->message_type(), "(" + vars_["result_ref"] + lhm + field_name + ")->", "(" + rhm + field_name + ")->", printer, allocate_memory, send, to_pbc, false, depth);
    }
  }
}

void AtlCodeGenerator::GenerateCleanupMessageMemoryCode(const Descriptor *message, const string lhm, io::Printer *printer, int depth)
{

  //
  // walk through the message structure and create code that copies from the rhm to the lhm.
  // for structures we need to deep copy - we can't just copy pointers
  //
  for (int i = 0; i < message->field_count(); i++)
  {
    const FieldDescriptor *field = message->field(i);
    string field_name = FieldName(field);

    vars_["field_name"] = field->name();
    vars_["left_field_name"] = lhm + field_name;

    if (field->is_repeated())
    {
      //
      // if a field is repeated it will appear in 'C' as a 2 dimensional array
      // ie an array of pointers to pointers of the type
      // we will need to loop the array and memcpy over each field
      //
      vars_["left_field_count"] = lhm + "n_" + field_name;
      if (field->type() == FieldDescriptor::TYPE_MESSAGE)
      {
        vars_["message_name"] = FullNameToC(field->message_type()->full_name());
      }
      else
      {
        vars_["message_name"] = TypeToString(field->type());
      }

      //
      // now free each field in the structures in the repeated array or the char *s in the char**
      //
      if (field->type() == FieldDescriptor::TYPE_MESSAGE ||
          field->type() == FieldDescriptor::TYPE_STRING ||
          field->type() == FieldDescriptor::TYPE_BYTES)
      {
        printer->Print(vars_, "if ($left_field_name$ != NULL)\n");
        printer->Print("{\n");
        printer->Indent();
        vars_["i"] = "_i" + ((depth > 0) ? SimpleItoa(depth) : "");
        printer->Print(vars_, "int $i$ = 0;\n");
        printer->Print(vars_, "for ($i$ = 0; $i$ < $left_field_count$; $i$++) // sub-message \"$field_name$\"\n");
        printer->Print("{\n");
        printer->Indent();

        if (field->type() == FieldDescriptor::TYPE_MESSAGE)
        {
          if (MessageContainsSubMessages(printer, field->message_type()))
          {
            GenerateCleanupMessageMemoryCode(field->message_type(), "(" + lhm + field_name + ")[" + vars_["i"] + "]->", printer, depth + 1);
            vars_["left_field_name"] = lhm + field_name;
          }
        }
        vars_["i"] = "_i" + ((depth > 0) ? SimpleItoa(depth) : "");
        printer->Print(vars_, "free (($left_field_name$)[$i$]);\n");

        printer->Outdent();
        printer->Print("}\n");
        printer->Outdent();
        printer->Print("}\n");
      }
      //
      // finally cleanup the array
      //
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
    else if (field->type() == FieldDescriptor::TYPE_BYTES)
    {
      printer->Print(vars_, "free ($left_field_name$);\n");
    }
    else if (field->type() == FieldDescriptor::TYPE_MESSAGE)
    {
      // this is a sub message so we need to follow pointers into the sub structure.
      //
      GenerateCleanupMessageMemoryCode(field->message_type(), lhm + field_name + "->", printer, depth);
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
		 "void $lcfullname$_init ($cname$_Service *service,\n"
		 "     $lcfullpadd$        $cname$_ServiceDestroy destroy)\n"
		 "{\n"
		 "  protobuf_c_service_generated_init (&service->base,\n"
		 "                                     &$lcfullname$_descriptor,\n"
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
  printer->Print(vars_, "static const ProtobufCMethodDescriptor $lcfullname$_method_descriptors[$n_methods$] =\n"
                       "{\n");
  for (int i = 0; i < n_methods; i++) {
    const MethodDescriptor *method = descriptor_->method(i);
    vars_["method"] = method->name();
    vars_["input_descriptor"] = "&" + FullNameToLower(method->input_type()->full_name()) + "_descriptor";
    vars_["output_descriptor"] = "&" + FullNameToLower(method->output_type()->full_name()) + "_descriptor";
    printer->Print(vars_,
             "  { \"$method$\", $input_descriptor$, $output_descriptor$ },\n");
    mi_array[i].i = i;
    mi_array[i].name = method->name().c_str();
  }
  printer->Print(vars_, "};\n");

  qsort ((void*)mi_array, n_methods, sizeof (MethodIndexAndName),
         compare_method_index_and_name_by_name);
  printer->Print(vars_, "const unsigned $lcfullname$_method_indices_by_name[] = {\n");
  for (int i = 0; i < n_methods; i++) {
    vars_["i"] = SimpleItoa(mi_array[i].i);
    vars_["name"] = mi_array[i].name;
    vars_["comma"] = (i + 1 < n_methods) ? "," : " ";
    printer->Print(vars_, "  $i$$comma$        /* $name$ */\n");
  }
  printer->Print(vars_, "};\n");

  printer->Print(vars_, "const ProtobufCServiceDescriptor $lcfullname$_descriptor =\n"
                       "{\n"
		       "  PROTOBUF_C_SERVICE_DESCRIPTOR_MAGIC,\n"
		       "  \"$fullname$\",\n"
		       "  \"$name$\",\n"
		       "  \"$cname$\",\n"
		       "  \"$package$\",\n"
		       "  $n_methods$,\n"
		       "  $lcfullname$_method_descriptors,\n"
		       "  $lcfullname$_method_indices_by_name\n"
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
    vars_["padddddddddddddddddd"] = ConvertToSpaces(lcfullname + "_" + lcname);
    vars_["index"] = SimpleItoa(i);
     
    printer->Print(vars_,
                   "void $lcfullname$_$method$(ProtobufCService *service,\n"
                   "     $padddddddddddddddddd$ const $input_typename$ *input,\n"
                   "     $padddddddddddddddddd$ $output_typename$_Closure closure,\n"
                   "     $padddddddddddddddddd$ void *closure_data)\n"
		   "{\n"
		   "  PROTOBUF_C_ASSERT (service->descriptor == &$lcfullname$_descriptor);\n"
		   "  service->invoke(service, $index$, (const ProtobufCMessage *) input, (ProtobufCClosure) closure, closure_data);\n"
		   "}\n");
  }
}


}  // namespace c
}  // namespace compiler
}  // namespace protobuf
}  // namespace google
