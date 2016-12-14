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

#ifdef ATL_CHANGE
#include <protoc-cmsg/c_service.h>
#include <protoc-cmsg/c_helpers.h>
#else
#include <protoc-c/c_service.h>
#include <protoc-c/c_helpers.h>
#endif /* ATL_CHANGE */
#include <google/protobuf/io/printer.h>

namespace google {
namespace protobuf {
namespace compiler {
namespace c {

ServiceGenerator::ServiceGenerator(const ServiceDescriptor* descriptor,
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

ServiceGenerator::~ServiceGenerator() {}

// Header stuff.
void ServiceGenerator::GenerateMainHFile(io::Printer* printer)
{
  GenerateVfuncs(printer);
  GenerateInitMacros(printer);
  GenerateCallersDeclarations(printer);
}
void ServiceGenerator::GenerateVfuncs(io::Printer* printer)
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
#ifdef ATL_CHANGE
                   "  int32_t (*$method$)($cname$_Service *service,\n"
#else
                   "  void (*$method$)($cname$_Service *service,\n"
#endif /* ATL_CHANGE */
                   "         $metpad$  const $input_typename$ *input,\n"
                   "         $metpad$  $output_typename$_Closure closure,\n"
                   "         $metpad$  void *closure_data);\n");
  }
#ifdef ATL_CHANGE
  printer->Print(vars_,
                 "  void *closure;\n"
                 "  void *closure_data;\n");
#endif /* ATL_CHANGE */
  printer->Print(vars_,
		 "};\n");
  printer->Print(vars_,
		 "typedef void (*$cname$_ServiceDestroy)($cname$_Service *);\n"
#ifdef ATL_CHANGE
		 "void $lcfullname$_init ($cname$_Service *service,\n"
#else
		 "void $lcfullname$__init ($cname$_Service *service,\n"
#endif /* ATL_CHANGE */
		 "     $lcfullpadd$        $cname$_ServiceDestroy destroy);\n");
}
void ServiceGenerator::GenerateInitMacros(io::Printer* printer)
{
  printer->Print(vars_,
#ifdef ATL_CHANGE
		 "#define $ucfullname$_BASE_INIT \\\n"
		 "    { &$lcfullname$_descriptor, protobuf_c_service_invoke_internal, NULL }\n"
		 "#define $ucfullname$_INIT(function_prefix_) \\\n"
		 "    { $ucfullname$_BASE_INIT");
#else
		 "#define $ucfullname$__BASE_INIT \\\n"
		 "    { &$lcfullname$__descriptor, protobuf_c_service_invoke_internal, NULL }\n"
		 "#define $ucfullname$__INIT(function_prefix__) \\\n"
		 "    { $ucfullname$__BASE_INIT");
#endif /* ATL_CHANGE */
  for (int i = 0; i < descriptor_->method_count(); i++) {
    const MethodDescriptor *method = descriptor_->method(i);
    string lcname = CamelToLower(method->name());
    vars_["method"] = lcname;
    vars_["metpad"] = ConvertToSpaces(lcname);
    printer->Print(vars_,
#ifdef ATL_CHANGE
                   ",\\\n      function_prefix_ ## $method$");
#else
                   ",\\\n      function_prefix__ ## $method$");
#endif /* ATL_CHANGE */
  }
  printer->Print(vars_,
		 "  }\n");
}
void ServiceGenerator::GenerateCallersDeclarations(io::Printer* printer)
{
  for (int i = 0; i < descriptor_->method_count(); i++) {
    const MethodDescriptor *method = descriptor_->method(i);
    string lcname = CamelToLower(method->name());
    string lcfullname = FullNameToLower(descriptor_->full_name());
    vars_["method"] = lcname;
    vars_["metpad"] = ConvertToSpaces(lcname);
    vars_["input_typename"] = FullNameToC(method->input_type()->full_name());
    vars_["output_typename"] = FullNameToC(method->output_type()->full_name());
#ifdef ATL_CHANGE
    vars_["padddddddddddddddddd"] = ConvertToSpaces(lcfullname + "_" + lcname);
#else
    vars_["padddddddddddddddddd"] = ConvertToSpaces(lcfullname + "__" + lcname);
#endif /* ATL_CHANGE */
    printer->Print(vars_,
#ifdef ATL_CHANGE
                   "int32_t $lcfullname$_$method$(ProtobufCService *service,\n"
                   "        $padddddddddddddddddd$ const $input_typename$ *input,\n"
                   "        $padddddddddddddddddd$ $output_typename$_Closure closure,\n"
                   "        $padddddddddddddddddd$ void *closure_data);\n");
#else
                   "void $lcfullname$__$method$(ProtobufCService *service,\n"
                   "     $padddddddddddddddddd$ const $input_typename$ *input,\n"
                   "     $padddddddddddddddddd$ $output_typename$_Closure closure,\n"
                   "     $padddddddddddddddddd$ void *closure_data);\n");
#endif /* ATL_CHANGE */
  }
}

void ServiceGenerator::GenerateDescriptorDeclarations(io::Printer* printer)
{
#ifdef ATL_CHANGE
  printer->Print(vars_, "extern const ProtobufCServiceDescriptor $lcfullname$_descriptor;\n");
#else
  printer->Print(vars_, "extern const ProtobufCServiceDescriptor $lcfullname$__descriptor;\n");
#endif /* ATL_CHANGE */
}


// Source file stuff.
void ServiceGenerator::GenerateCFile(io::Printer* printer)
{
  GenerateServiceDescriptor(printer);
  GenerateCallersImplementations(printer);
  GenerateInit(printer);
}
void ServiceGenerator::GenerateInit(io::Printer* printer)
{
  printer->Print(vars_,
#ifdef ATL_CHANGE
		 "void $lcfullname$_init ($cname$_Service *service,\n"
#else
		 "void $lcfullname$__init ($cname$_Service *service,\n"
#endif /* ATL_CHANGE */
		 "     $lcfullpadd$        $cname$_ServiceDestroy destroy)\n"
		 "{\n"
		 "  protobuf_c_service_generated_init (&service->base,\n"
#ifdef ATL_CHANGE
		 "                                     &$lcfullname$_descriptor,\n"
#else
		 "                                     &$lcfullname$__descriptor,\n"
#endif /* ATL_CHANGE */
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

void ServiceGenerator::GenerateServiceDescriptor(io::Printer* printer)
{
  int n_methods = descriptor_->method_count();
  MethodIndexAndName *mi_array = new MethodIndexAndName[n_methods];
  
  vars_["n_methods"] = SimpleItoa(n_methods);
#ifdef ATL_CHANGE
  printer->Print(vars_, "static const ProtobufCMethodDescriptor $lcfullname$_method_descriptors[$n_methods$] =\n"
#else
  printer->Print(vars_, "static const ProtobufCMethodDescriptor $lcfullname$__method_descriptors[$n_methods$] =\n"
#endif /* ATL_CHANGE */
                       "{\n");
  for (int i = 0; i < n_methods; i++) {
    const MethodDescriptor *method = descriptor_->method(i);
    vars_["method"] = method->name();
#ifdef ATL_CHANGE
    vars_["input_descriptor"] = "&" + FullNameToLower(method->input_type()->full_name()) + "_descriptor";
    vars_["output_descriptor"] = "&" + FullNameToLower(method->output_type()->full_name()) + "_descriptor";
#else
    vars_["input_descriptor"] = "&" + FullNameToLower(method->input_type()->full_name()) + "__descriptor";
    vars_["output_descriptor"] = "&" + FullNameToLower(method->output_type()->full_name()) + "__descriptor";
#endif /* ATL_CHANGE */
    printer->Print(vars_,
             "  { \"$method$\", $input_descriptor$, $output_descriptor$ },\n");
    mi_array[i].i = i;
    mi_array[i].name = method->name().c_str();
  }
  printer->Print(vars_, "};\n");

  qsort ((void*)mi_array, n_methods, sizeof (MethodIndexAndName),
         compare_method_index_and_name_by_name);
#ifdef ATL_CHANGE
  printer->Print(vars_, "const unsigned $lcfullname$_method_indices_by_name[] = {\n");
#else
  printer->Print(vars_, "const unsigned $lcfullname$__method_indices_by_name[] = {\n");
#endif /* ATL_CHANGE */
  for (int i = 0; i < n_methods; i++) {
    vars_["i"] = SimpleItoa(mi_array[i].i);
#ifdef ATL_CHANGE
    vars_["method"] = mi_array[i].name;
#else
    vars_["name"] = mi_array[i].name;
#endif /* ATL_CHANGE */
    vars_["comma"] = (i + 1 < n_methods) ? "," : " ";
#ifdef ATL_CHANGE
    printer->Print(vars_, "  $i$$comma$        /* $method$ */\n");
#else
    printer->Print(vars_, "  $i$$comma$        /* $name$ */\n");
#endif /* ATL_CHANGE */
  }
  printer->Print(vars_, "};\n");

#ifdef ATL_CHANGE
  printer->Print(vars_, "const ProtobufCServiceDescriptor $lcfullname$_descriptor =\n"
#else
  printer->Print(vars_, "const ProtobufCServiceDescriptor $lcfullname$__descriptor =\n"
#endif /* ATL_CHANGE */
                       "{\n"
		       "  PROTOBUF_C_SERVICE_DESCRIPTOR_MAGIC,\n"
		       "  \"$fullname$\",\n"
		       "  \"$name$\",\n"
		       "  \"$cname$\",\n"
		       "  \"$package$\",\n"
		       "  $n_methods$,\n"
#ifdef ATL_CHANGE
		       "  $lcfullname$_method_descriptors,\n"
		       "  $lcfullname$_method_indices_by_name\n"
#else
		       "  $lcfullname$__method_descriptors,\n"
		       "  $lcfullname$__method_indices_by_name\n"
#endif /* ATL_CHANGE */
		       "};\n");
#ifdef ATL_CHANGE
  delete[] mi_array;
#endif /* ATL_CHANGE */
}

void ServiceGenerator::GenerateCallersImplementations(io::Printer* printer)
{
  for (int i = 0; i < descriptor_->method_count(); i++) {
    const MethodDescriptor *method = descriptor_->method(i);
    string lcname = CamelToLower(method->name());
    string lcfullname = FullNameToLower(descriptor_->full_name());
    vars_["method"] = lcname;
    vars_["metpad"] = ConvertToSpaces(lcname);
    vars_["input_typename"] = FullNameToC(method->input_type()->full_name());
    vars_["output_typename"] = FullNameToC(method->output_type()->full_name());
#ifdef ATL_CHANGE
    vars_["padddddddddddddddddd"] = ConvertToSpaces(lcfullname + "_" + lcname);
#else
    vars_["padddddddddddddddddd"] = ConvertToSpaces(lcfullname + "__" + lcname);
#endif /* ATL_CHANGE */
    vars_["index"] = SimpleItoa(i);
     
    printer->Print(vars_,
#ifdef ATL_CHANGE
                   "int32_t $lcfullname$_$method$(ProtobufCService *service,\n"
                   "        $padddddddddddddddddd$ const $input_typename$ *input,\n"
                   "        $padddddddddddddddddd$ $output_typename$_Closure closure,\n"
                   "        $padddddddddddddddddd$ void *closure_data)\n"
#else
                   "void $lcfullname$__$method$(ProtobufCService *service,\n"
                   "     $padddddddddddddddddd$ const $input_typename$ *input,\n"
                   "     $padddddddddddddddddd$ $output_typename$_Closure closure,\n"
                   "     $padddddddddddddddddd$ void *closure_data)\n"
#endif /* ATL_CHANGE */
		   "{\n"
#ifdef ATL_CHANGE
		   "  PROTOBUF_C_ASSERT (service->descriptor == &$lcfullname$_descriptor);\n"
		   "  return service->invoke(service, $index$, (const ProtobufCMessage *) input, (ProtobufCClosure) closure, closure_data);\n"
#else
		   "  PROTOBUF_C_ASSERT (service->descriptor == &$lcfullname$__descriptor);\n"
		   "  service->invoke(service, $index$, (const ProtobufCMessage *) input, (ProtobufCClosure) closure, closure_data);\n"

#endif /* ATL_CHANGE */
		   "}\n");
  }
}


}  // namespace c
}  // namespace compiler
}  // namespace protobuf
}  // namespace google
