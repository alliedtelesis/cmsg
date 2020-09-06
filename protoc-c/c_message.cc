// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
// http://code.google.com/p/protobuf/
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Author: kenton@google.com (Kenton Varda)
//  Based on original Protocol Buffers design by
//  Sanjay Ghemawat, Jeff Dean, and others.

// Copyright (c) 2008-2013, Dave Benson.  All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Modified to implement C code by Dave Benson.

#include <algorithm>
#include <map>
#include <memory>
#include <protoc-c/c_message.h>
#include <protoc-c/c_enum.h>
#include <protoc-c/c_helpers.h>
#include <protoc-c/c_helpers_cmsg.h>
#include <google/protobuf/io/printer.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/wire_format.h>
#include <google/protobuf/descriptor.pb.h>
#include "validation.pb.h"

namespace google {
namespace protobuf {
namespace compiler {
namespace c {

// ===================================================================

MessageGenerator::MessageGenerator(const Descriptor* descriptor)
  : descriptor_(descriptor),
    nested_generators_(new std::unique_ptr<MessageGenerator>[
      descriptor->nested_type_count()]),
    enum_generators_(new std::unique_ptr<EnumGenerator>[
      descriptor->enum_type_count()]) {

  for (int i = 0; i < descriptor->nested_type_count(); i++) {
    nested_generators_[i].reset(
      new MessageGenerator(descriptor->nested_type(i)));
  }

  for (int i = 0; i < descriptor->enum_type_count(); i++) {
    enum_generators_[i].reset(
      new EnumGenerator(descriptor->enum_type(i)));
  }
}

MessageGenerator::~MessageGenerator() {}

void MessageGenerator::
GenerateStructTypedefDefine(io::Printer* printer) {
    printer->Print("typedef $classname$ $cmsg_classname$;\n",
                 "cmsg_classname", cmsg::FullNameToC(descriptor_->full_name()),
                 "classname", FullNameToC(descriptor_->full_name()));

  for (int i = 0; i < descriptor_->nested_type_count(); i++) {
    nested_generators_[i]->GenerateStructTypedefDefine(printer);
  }
}

void MessageGenerator::
GenerateEnumDefinitionsDefine(io::Printer* printer) {
  for (int i = 0; i < descriptor_->nested_type_count(); i++) {
    nested_generators_[i]->GenerateEnumDefinitionsDefine(printer);
  }

  for (int i = 0; i < descriptor_->enum_type_count(); i++) {
    enum_generators_[i]->GenerateDefinitionDefine(printer);
  }
}


void MessageGenerator::
GenerateStructDefinitionDefine(io::Printer* printer) {
  for (int i = 0; i < descriptor_->nested_type_count(); i++) {
    nested_generators_[i]->GenerateStructDefinitionDefine(printer);
  }

  std::map<string, string> vars;

  //
  vars["ucclassname"] = FullNameToUpper(descriptor_->full_name());
  vars["cmsg_ucclassname"] = cmsg::FullNameToUpper(descriptor_->full_name());

  // Generate the case enums for unions
  for (int i = 0; i < descriptor_->oneof_decl_count(); i++) {
    const OneofDescriptor *oneof = descriptor_->oneof_decl(i);
    vars["ucclassname"] = FullNameToUpper(descriptor_->full_name());
    vars["oneofname"] = FullNameToUpper(oneof->name());
    vars["foneofname"] = FullNameToC(oneof->full_name());
    vars["cmsg_foneofname"] = cmsg::FullNameToC(oneof->full_name());
    vars["cmsg_oneofname"] = cmsg::FullNameToUpper(oneof->name());
    vars["cmsg_ucclassname"] = cmsg::FullNameToUpper(descriptor_->full_name());

    printer->Print(vars, "#define $cmsg_foneofname$Case $foneofname$Case\n");
    printer->Print(vars, "#define $cmsg_ucclassname$_$cmsg_oneofname$_NOT_SET $ucclassname$__$oneofname$__NOT_SET\n");

    for (int j = 0; j < oneof->field_count(); j++) {
      const FieldDescriptor *field = oneof->field(j);
      vars["fieldname"] = FullNameToUpper(field->name());
      vars["cmsg_fieldname"] = cmsg::FullNameToUpper(field->name());
      printer->Print(vars, "#define $cmsg_ucclassname$_$cmsg_oneofname$_$cmsg_fieldname$ $ucclassname$__$oneofname$_$fieldname$\n");
    }
  }

  for (int i = 0; i < descriptor_->field_count(); i++) {
    const FieldDescriptor *field = descriptor_->field(i);
    if (field->has_default_value()) {
      if (field->type() == FieldDescriptor::TYPE_STRING ||
          field->type() == FieldDescriptor::TYPE_BYTES) {
          vars["default_value_data"] = FullNameToLower(field->full_name()) + "__default_value_data";
          vars["cmsg_default_value_data"] = cmsg::FullNameToLower(field->full_name()) + "_default_value_data";
          printer->Print(vars, "#define $cmsg_default_value_data$ $default_value_data$\n");
      }
    }
  }

  printer->Print(vars, "#define $cmsg_ucclassname$_INIT $ucclassname$__INIT\n");
}

static bool _message_has_validation(const Descriptor *message,
                                    std::unordered_set<const Descriptor *> *recursed_descriptors)
{
    const FieldDescriptor *field = NULL;

    /* For each submessage in a message we recursively check if that message
     * needs validation. Therefore it is possible to recurse infinitively
     * if a submessage eventually has a field of the type of its parent. This
     * check here is to ensure that we stop recursion if we have already recursed
     * through the message type we are currently checking. */
    if (recursed_descriptors->find(message) != recursed_descriptors->end())
    {
        return false;
    }

    recursed_descriptors->insert(message);

    for (int i = 0; i < message->field_count(); i++)
    {
        field = message->field(i);
        if (field->options().HasExtension(validation))
        {
            return true;
        }
        if (field->type() == FieldDescriptor::TYPE_MESSAGE)
         {
              const Descriptor *submessage = field->message_type();

              if (_message_has_validation(submessage, recursed_descriptors))
              {
                  return true;
              }
         }
    }

    return false;
}

static bool message_has_validation(const Descriptor *message)
{
    std::unordered_set<const Descriptor *> recursed_descriptors;
    return _message_has_validation (message, &recursed_descriptors);
}

static void
generate_int_validation (const FieldDescriptor *field, io::Printer* printer,
                         const char *function_name, int64_t compare_value)
{
    std::map<string, string> vars;
    FieldValidation validation_defs = field->options().GetExtension(validation);
    std::string int_string;

    if (validation_defs.has_error_message())
    {
        vars["error_message"] = validation_defs.error_message();
    }
    else
    {
        vars["error_message"] = "NULL";
    }

    int_string = std::to_string(compare_value);

    vars["compare_value"] = int_string;
    vars["fieldname"] = field->name();
    vars["function_name"] = function_name;

    printer->Print(vars, "if (!$function_name$ (message->$fieldname$, $compare_value$, \"$fieldname$\",\n");
    printer->Print(vars, "                      \"$error_message$\", err_str, err_str_len))\n");
    printer->Print("{\n");
    printer->Indent();
    printer->Print("return false;\n");
    printer->Outdent();
    printer->Print("}\n");
}

static void
generate_str_validation (const FieldDescriptor *field, io::Printer* printer,
                         const char *function_name)
{
    std::map<string, string> vars;
    FieldValidation validation_defs = field->options().GetExtension(validation);

    if (validation_defs.has_error_message())
    {
        vars["error_message"] = validation_defs.error_message();
    }
    else
    {
        vars["error_message"] = "NULL";
    }

    vars["fieldname"] = field->name();
    vars["function_name"] = function_name;

    printer->Print(vars, "if (message->$fieldname$ && !$function_name$ (message->$fieldname$, \"$fieldname$\",\n");
    printer->Print(vars, "                                              \"$error_message$\", err_str, err_str_len))\n");
    printer->Print("{\n");
    printer->Indent();
    printer->Print("return false;\n");
    printer->Outdent();
    printer->Print("}\n");
}

static void
generate_string_format_validation (const FieldDescriptor *field, io::Printer* printer)
{
    std::map<string, string> vars;
    std::string int_string;
    FieldValidation validation_defs = field->options().GetExtension(validation);
    common_string_format format = validation_defs.string_format();

    vars["fieldname"] = field->name();

    switch (format)
    {
    case IP_ADDRESS:
        generate_str_validation (field, printer, "cmsg_validate_ip_address");
        break;

    case UTC_TIMESTAMP:
        generate_str_validation (field, printer, "cmsg_validate_utc_timestamp");
        break;

    case MAC_ADDRESS:
        generate_str_validation (field, printer, "cmsg_validate_mac_address");
        break;

    default:
        break;
    }
}

static void
generate_field_validation (const FieldDescriptor *field, io::Printer* printer)
{
    FieldValidation validation_defs = field->options().GetExtension(validation);

    if (validation_defs.has_int_ge())
    {
        generate_int_validation (field, printer, "cmsg_validate_int_ge",
                                 validation_defs.int_ge());
    }
    if (validation_defs.has_int_le())
    {
        generate_int_validation (field, printer, "cmsg_validate_int_le",
                                 validation_defs.int_le());
    }
    if (validation_defs.has_string_format())
    {
        generate_string_format_validation (field, printer);
    }
}

static void
generate_fields_validation (const Descriptor *message, io::Printer* printer)
{
    const FieldDescriptor *field = NULL;

    for (int i = 0; i < message->field_count(); i++)
    {
        field = message->field(i);
        if (field->options().HasExtension(validation))
        {
            generate_field_validation (field, printer);
            continue;
        }
        if (field->type() == FieldDescriptor::TYPE_MESSAGE &&
            message_has_validation (field->message_type()))
        {
            std::map<string, string> vars;
            const Descriptor *submessage = field->message_type();
            vars["lcclassname"] = cmsg::FullNameToLower(submessage->full_name());
            vars["fieldname"] = field->name();

            printer->Print(vars, "if (message->$fieldname$ && !$lcclassname$_validate (message->$fieldname$ , err_str, err_str_len))\n");
            printer->Print("{\n");
            printer->Indent();
            printer->Print("return false;\n");
            printer->Outdent();
            printer->Print("}\n");
        }
    }
}

static void
generate_validation_function (const Descriptor *message, io::Printer* printer)
{
    std::map<string, string> vars;

    if (!message_has_validation (message))
    {
        return;
    }

    vars["classname"] = cmsg::FullNameToC(message->full_name());
    vars["lcclassname"] = cmsg::FullNameToLower(message->full_name());
    printer->Print("\n");
    printer->Print(vars, "bool $lcclassname$_validate (const $classname$ *message, char *err_str, uint32_t err_str_len)\n");
    printer->Print("{\n");
    printer->Indent();
    printer->Print(vars, "if (!message)\n");
    printer->Print("{\n");
    printer->Indent();
    printer->Print("return true;\n");
    printer->Outdent();
    printer->Print("}\n");
    generate_fields_validation (message, printer);
    printer->Print("return true;\n");
    printer->Outdent();
    printer->Print("}\n");
    printer->Print("\n");
}

void MessageGenerator::
GenerateHelperFunctionDeclarationsDefine(io::Printer* printer, bool is_submessage)
{
  for (int i = 0; i < descriptor_->nested_type_count(); i++) {
    nested_generators_[i]->GenerateHelperFunctionDeclarationsDefine(printer, true);
  }

  std::map<string, string> vars;
  vars["classname"] = FullNameToC(descriptor_->full_name());
  vars["lcclassname"] = FullNameToLower(descriptor_->full_name());
  vars["cmsg_classname"] = cmsg::FullNameToC(descriptor_->full_name());
  vars["cmsg_lcclassname"] = cmsg::FullNameToLower(descriptor_->full_name());

  printer->Print(vars, "/* $cmsg_classname$ methods */\n");
  printer->Print(vars, "#define $cmsg_lcclassname$_init $lcclassname$__init\n");

  if (!is_submessage) {
    printer->Print(vars,
         "#define $cmsg_lcclassname$_get_packed_size $lcclassname$__get_packed_size\n"
         "#define $cmsg_lcclassname$_pack $lcclassname$__pack\n"
         "#define $cmsg_lcclassname$_pack_to_buffer $lcclassname$__pack_to_buffer\n"
         "#define $cmsg_lcclassname$_unpack $lcclassname$__unpack\n"
         "#define $cmsg_lcclassname$_free_unpacked $lcclassname$__free_unpacked\n"
    );

    // todo - This should probably be generated by the validation related generator.
    if (message_has_validation (descriptor_))
    {
        printer->Print(vars, "bool $lcclassname$_validate (const $classname$ *message, char *err_str, uint32_t err_str_len);\n");
    }
  }
}

void MessageGenerator::
GenerateDescriptorDeclarationsDefines(io::Printer* printer) {
  printer->Print("#define $cmsg_name$_descriptor $name$__descriptor\n",
                 "cmsg_name", cmsg::FullNameToLower(descriptor_->full_name()),
                 "name", FullNameToLower(descriptor_->full_name()));

  for (int i = 0; i < descriptor_->nested_type_count(); i++) {
    nested_generators_[i]->GenerateDescriptorDeclarationsDefines(printer);
  }

  for (int i = 0; i < descriptor_->enum_type_count(); i++) {
    enum_generators_[i]->GenerateDescriptorDeclarationsDefines(printer);
  }
}
void MessageGenerator::GenerateClosureTypedefDefine(io::Printer* printer)
{
  for (int i = 0; i < descriptor_->nested_type_count(); i++) {
    nested_generators_[i]->GenerateClosureTypedefDefine(printer);
  }
  std::map<string, string> vars;
  vars["name"] = FullNameToC(descriptor_->full_name());
  vars["cmsg_name"] = cmsg::FullNameToC(descriptor_->full_name());
  printer->Print(vars, "#define $cmsg_name$_Closure $name$_Closure\n");
}

static int
compare_pfields_by_number (const void *a, const void *b)
{
  const FieldDescriptor *pa = *(const FieldDescriptor **)a;
  const FieldDescriptor *pb = *(const FieldDescriptor **)b;
  if (pa->number() < pb->number()) return -1;
  if (pa->number() > pb->number()) return +1;
  return 0;
}

void MessageGenerator::
GenerateValidationDefinitions(io::Printer* printer, bool is_submessage)
{
  for (int i = 0; i < descriptor_->nested_type_count(); i++) {
    nested_generators_[i]->GenerateValidationDefinitions(printer, true);
  }

  if (!is_submessage) {
    generate_validation_function (descriptor_, printer);
  }
}

void MessageGenerator::
GenerateValidationDeclarations(io::Printer* printer, bool is_submessage)
{
  for (int i = 0; i < descriptor_->nested_type_count(); i++) {
    nested_generators_[i]->GenerateValidationDeclarations(printer, true);
  }

  std::map<string, string> vars;
  vars["classname"] = cmsg::FullNameToC(descriptor_->full_name());
  vars["lcclassname"] = cmsg::FullNameToLower(descriptor_->full_name());

  if (!is_submessage) {
    if (message_has_validation (descriptor_))
    {
        printer->Print(vars, "bool $lcclassname$_validate (const $classname$ *message, char *err_str, uint32_t err_str_len);\n");
    }
  }
}

}  // namespace c
}  // namespace compiler
}  // namespace protobuf
}  // namespace google
