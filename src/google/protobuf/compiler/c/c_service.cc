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

#include <google/protobuf/compiler/c/c_service.h>
#include <google/protobuf/compiler/c/c_helpers.h>
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
    string lcname = CamelToLower(method->name()) + "_pbc";
    vars_["method"] = lcname;
    vars_["metpad"] = ConvertToSpaces(lcname);
    vars_["input_typename"] = FullNameToC(method->input_type()->full_name()) + "_pbc";
    vars_["output_typename"] = FullNameToC(method->output_type()->full_name()) + "_pbc";
    printer->Print(vars_,
                   "  int32_t (*$method$)($cname$_Service *service,\n"
                   "         $metpad$  const $input_typename$ *input,\n"
                   "         $metpad$  $output_typename$_Closure closure,\n"
                   "         $metpad$  void *closure_data);\n");
  }
  printer->Print(vars_,
                 "  void *closure;\n"
                 "  void *closure_data;\n");
  printer->Print(vars_,
		 "};\n");
  printer->Print(vars_,
		 "typedef void (*$cname$_ServiceDestroy)($cname$_Service *);\n"
		 "void $lcfullname$_init ($cname$_Service *service,\n"
		 "     $lcfullpadd$        $cname$_ServiceDestroy destroy);\n");
}
void ServiceGenerator::GenerateInitMacros(io::Printer* printer)
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
void ServiceGenerator::GenerateCallersDeclarations(io::Printer* printer)
{
  for (int i = 0; i < descriptor_->method_count(); i++) {
    const MethodDescriptor *method = descriptor_->method(i);
    string lcname = CamelToLower(method->name()) + "_pbc";
    string lcfullname = FullNameToLower(descriptor_->full_name());
    vars_["method"] = lcname;
    vars_["metpad"] = ConvertToSpaces(lcname);
    vars_["input_typename"] = FullNameToC(method->input_type()->full_name()) + "_pbc";
    vars_["output_typename"] = FullNameToC(method->output_type()->full_name()) + "_pbc";
    vars_["padddddddddddddddddd"] = ConvertToSpaces(lcfullname + "_" + lcname);
    printer->Print(vars_,
                   "int32_t $lcfullname$_$method$(ProtobufCService *service,\n"
                   "        $padddddddddddddddddd$ const $input_typename$ *input,\n"
                   "        $padddddddddddddddddd$ $output_typename$_Closure closure,\n"
                   "        $padddddddddddddddddd$ void *closure_data);\n");
  }
}

void ServiceGenerator::GenerateDescriptorDeclarations(io::Printer* printer)
{
  printer->Print(vars_, "extern const ProtobufCServiceDescriptor $lcfullname$_descriptor;\n");
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

void ServiceGenerator::GenerateServiceDescriptor(io::Printer* printer)
{
  int n_methods = descriptor_->method_count();
  MethodIndexAndName *mi_array = new MethodIndexAndName[n_methods];
  string *method_name = new string[n_methods];
  
  vars_["n_methods"] = SimpleItoa(n_methods);
  printer->Print(vars_, "static const ProtobufCMethodDescriptor $lcfullname$_method_descriptors[$n_methods$] =\n"
                       "{\n");
  for (int i = 0; i < n_methods; i++) {
    const MethodDescriptor *method = descriptor_->method(i);
    vars_["method"] = method->name() + "_pbc";
    vars_["input_descriptor"] = "&" + FullNameToLower(method->input_type()->full_name()) + "_descriptor";
    vars_["output_descriptor"] = "&" + FullNameToLower(method->output_type()->full_name()) + "_descriptor";
    printer->Print(vars_,
             "  { \"$method$\", $input_descriptor$, $output_descriptor$ },\n");
    mi_array[i].i = i;
    method_name[i] = method->name() + "_pbc";
    mi_array[i].name = method_name[i].c_str();
  }
  printer->Print(vars_, "};\n");

  qsort ((void*)mi_array, n_methods, sizeof (MethodIndexAndName),
         compare_method_index_and_name_by_name);
  printer->Print(vars_, "const unsigned $lcfullname$_method_indices_by_name[] = {\n");
  for (int i = 0; i < n_methods; i++) {
    vars_["i"] = SimpleItoa(mi_array[i].i);
    vars_["method"] = mi_array[i].name;
    vars_["comma"] = (i + 1 < n_methods) ? "," : " ";
    printer->Print(vars_, "  $i$$comma$        /* $method$ */\n");
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

  delete[] mi_array;
  delete[] method_name;
}

void ServiceGenerator::GenerateCallersImplementations(io::Printer* printer)
{
  for (int i = 0; i < descriptor_->method_count(); i++) {
    const MethodDescriptor *method = descriptor_->method(i);
    string lcname = CamelToLower(method->name()) + "_pbc";
    string lcfullname = FullNameToLower(descriptor_->full_name());
    vars_["method"] = lcname;
    vars_["metpad"] = ConvertToSpaces(lcname);
    vars_["input_typename"] = FullNameToC(method->input_type()->full_name()) + "_pbc";
    vars_["output_typename"] = FullNameToC(method->output_type()->full_name()) + "_pbc";
    vars_["padddddddddddddddddd"] = ConvertToSpaces(lcfullname + "_" + lcname);
    vars_["index"] = SimpleItoa(i);
     
    printer->Print(vars_,
                   "int32_t $lcfullname$_$method$(ProtobufCService *service,\n"
                   "        $padddddddddddddddddd$ const $input_typename$ *input,\n"
                   "        $padddddddddddddddddd$ $output_typename$_Closure closure,\n"
                   "        $padddddddddddddddddd$ void *closure_data)\n"
		   "{\n"
		   "  PROTOBUF_C_ASSERT (service->descriptor == &$lcfullname$_descriptor);\n"
		   "  return service->invoke(service, $index$, (const ProtobufCMessage *) input, (ProtobufCClosure) closure, closure_data);\n"
		   "}\n");
  }
}


}  // namespace c
}  // namespace compiler
}  // namespace protobuf
}  // namespace google
