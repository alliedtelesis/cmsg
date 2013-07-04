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

#include <google/protobuf/compiler/c/c_file.h>
#include <google/protobuf/compiler/c/c_enum.h>
#include <google/protobuf/compiler/c/c_service.h>
#include <google/protobuf/compiler/c/c_atl_generator.h>
#include <google/protobuf/compiler/c/c_atl_message.h>
#include <google/protobuf/compiler/c/c_extension.h>
#include <google/protobuf/compiler/c/c_helpers.h>
#include <google/protobuf/compiler/c/c_message.h>
#include <google/protobuf/io/printer.h>
#include <google/protobuf/descriptor.pb.h>

namespace google {
namespace protobuf {
namespace compiler {
namespace c {

// ===================================================================

FileGenerator::FileGenerator(const FileDescriptor* file,
                             const string& dllexport_decl)
  : file_(file),
    message_generators_(
      new scoped_ptr<MessageGenerator>[file->message_type_count()]),
    enum_generators_(
      new scoped_ptr<EnumGenerator>[file->enum_type_count()]),
    service_generators_(
      new scoped_ptr<ServiceGenerator>[file->service_count()]),
    atl_code_generators_(
      new scoped_ptr<AtlCodeGenerator>[file->service_count()]),
    atl_message_generators_(
      new scoped_ptr<AtlMessageGenerator>[file->message_type_count()]),
    extension_generators_(
      new scoped_ptr<ExtensionGenerator>[file->extension_count()]) {

  for (int i = 0; i < file->message_type_count(); i++) {
    message_generators_[i].reset(
      new MessageGenerator(file->message_type(i), dllexport_decl));
  }

  for (int i = 0; i < file->enum_type_count(); i++) {
    enum_generators_[i].reset(
      new EnumGenerator(file->enum_type(i), dllexport_decl));
  }

  for (int i = 0; i < file->service_count(); i++) {
    service_generators_[i].reset(
      new ServiceGenerator(file->service(i), dllexport_decl));
  }

  for (int i = 0; i < file->service_count(); i++) {
    atl_code_generators_[i].reset(
      new AtlCodeGenerator(file->service(i), dllexport_decl));
  }

  for (int i = 0; i < file->message_type_count(); i++) {
    atl_message_generators_[i].reset(
      new AtlMessageGenerator(file->message_type(i), dllexport_decl));
  }

  for (int i = 0; i < file->extension_count(); i++) {
    extension_generators_[i].reset(
      new ExtensionGenerator(file->extension(i), dllexport_decl));
  }

  SplitStringUsing(file_->package(), ".", &package_parts_);
}

FileGenerator::~FileGenerator() {}

void FileGenerator::GenerateHeader(io::Printer* printer) {
  string filename_identifier = FilenameIdentifier(file_->name());

  // Generate top of header.
  printer->Print(
    "/* Generated by the protocol buffer compiler.  DO NOT EDIT! */\n"
    "\n"
    "#ifndef PROTOBUF_C_$filename_identifier$_INCLUDED\n"
    "#define PROTOBUF_C_$filename_identifier$_INCLUDED\n"
    "\n"
    "#include <google/protobuf-c/protobuf-c.h>\n"
    "#include <google/protobuf-c/protobuf-c-rpc.h>\n"
    "\n"
    "PROTOBUF_C_BEGIN_DECLS\n"
    "\n",
    "filename_identifier", filename_identifier);

#if 0
  // Verify the protobuf library header version is compatible with the protoc
  // version before going any further.
  printer->Print(
    "#if GOOGLE_PROTOBUF_VERSION < $min_header_version$\n"
    "#error This file was generated by a newer version of protoc which is\n"
    "#error incompatible with your Protocol Buffer headers.  Please update\n"
    "#error your headers.\n"
    "#endif\n"
    "#if $protoc_version$ < GOOGLE_PROTOBUF_MIN_PROTOC_VERSION\n"
    "#error This file was generated by an older version of protoc which is\n"
    "#error incompatible with your Protocol Buffer headers.  Please\n"
    "#error regenerate this file with a newer version of protoc.\n"
    "#endif\n"
    "\n",
    "min_header_version",
      SimpleItoa(protobuf::internal::kMinHeaderVersionForProtoc),
    "protoc_version", SimpleItoa(GOOGLE_PROTOBUF_VERSION));
#endif

  //
  // add some includes for the ATL generated code
  //
  printer->Print("#include <string.h>\n");
  printer->Print("#include <stdlib.h>\n");

  for (int i = 0; i < file_->dependency_count(); i++) {
    printer->Print(
      "#include \"$dependency$.pb-c.h\"\n",
      "dependency", StripProto(file_->dependency(i)->name()));
  }

  printer->Print("\n");

  // Generate forward declarations of classes.
  for (int i = 0; i < file_->message_type_count(); i++) {
    message_generators_[i]->GenerateStructTypedef(printer);
  }

  printer->Print("\n");

  // Generate enum definitions.
  printer->Print("\n/* --- enums --- */\n\n");
  for (int i = 0; i < file_->message_type_count(); i++) {
    message_generators_[i]->GenerateEnumDefinitions(printer);
  }
  for (int i = 0; i < file_->enum_type_count(); i++) {
    enum_generators_[i]->GenerateDefinition(printer);
  }

  // Generate class definitions.
  printer->Print("\n/* --- messages --- */\n\n");
  for (int i = 0; i < file_->message_type_count(); i++) {
    message_generators_[i]->GenerateStructDefinition(printer);
  }

  for (int i = 0; i < file_->message_type_count(); i++) {
    message_generators_[i]->GenerateHelperFunctionDeclarations(printer, false);
  }

  printer->Print("/* --- per-message closures --- */\n\n");
  for (int i = 0; i < file_->message_type_count(); i++) {
    message_generators_[i]->GenerateClosureTypedef(printer);
  }

  // Generate service definitions.
  printer->Print("\n/* --- services --- */\n\n");
  for (int i = 0; i < file_->service_count(); i++) {
    service_generators_[i]->GenerateMainHFile(printer);
  }

  // Declare extension identifiers.
  for (int i = 0; i < file_->extension_count(); i++) {
    extension_generators_[i]->GenerateDeclaration(printer);
  }

  printer->Print("\n/* --- descriptors --- */\n\n");
  for (int i = 0; i < file_->enum_type_count(); i++) {
    enum_generators_[i]->GenerateDescriptorDeclarations(printer);
  }
  for (int i = 0; i < file_->message_type_count(); i++) {
    message_generators_[i]->GenerateDescriptorDeclarations(printer);
  }
  for (int i = 0; i < file_->service_count(); i++) {
    service_generators_[i]->GenerateDescriptorDeclarations(printer);
  }

  printer->Print(
    "\n"
    "PROTOBUF_C_END_DECLS\n"
    "\n\n#endif  /* PROTOBUF_$filename_identifier$_INCLUDED */\n",
    "filename_identifier", filename_identifier);
}

void FileGenerator::GenerateSource(io::Printer* printer) {
  printer->Print(
    "/* Generated by the protocol buffer compiler.  DO NOT EDIT! */\n"
    "\n"
    "/* Do not generate deprecated warnings for self */\n"
    "#ifndef PROTOBUF_C_NO_DEPRECATED\n"
    "#define PROTOBUF_C_NO_DEPRECATED\n"
    "#endif\n"
    "\n"
    "#include \"$basename$.pb-c.h\"\n",
    "basename", StripProto(file_->name()));

#if 0
  // For each dependency, write a prototype for that dependency's
  // BuildDescriptors() function.  We don't expose these in the header because
  // they are internal implementation details, and since this is generated code
  // we don't have the usual risks involved with declaring external functions
  // within a .cc file.
  for (int i = 0; i < file_->dependency_count(); i++) {
    const FileDescriptor* dependency = file_->dependency(i);
    // Open the dependency's namespace.
    vector<string> dependency_package_parts;
    SplitStringUsing(dependency->package(), ".", &dependency_package_parts);
    // Declare its BuildDescriptors() function.
    printer->Print(
      "void $function$();",
      "function", GlobalBuildDescriptorsName(dependency->name()));
    // Close the namespace.
    for (int i = 0; i < dependency_package_parts.size(); i++) {
      printer->Print(" }");
    }
    printer->Print("\n");
  }
#endif

  for (int i = 0; i < file_->message_type_count(); i++) {
    message_generators_[i]->GenerateHelperFunctionDefinitions(printer, false);
  }
  for (int i = 0; i < file_->message_type_count(); i++) {
    message_generators_[i]->GenerateMessageDescriptor(printer);
  }
  for (int i = 0; i < file_->enum_type_count(); i++) {
    enum_generators_[i]->GenerateEnumDescriptor(printer);
  }
  for (int i = 0; i < file_->service_count(); i++) {
    service_generators_[i]->GenerateCFile(printer);
  }

}

void FileGenerator::GenerateAtlTypesHeader(io::Printer* printer) {
  string filename_identifier = StripProto(file_->name());

  // Generate top of header.
  printer->Print(
    "/* Generated by the protocol buffer compiler.  DO NOT EDIT! */\n"
    "\n"
    "#ifndef PROTOBUF_C_TYPES_$filename_identifier$_INCLUDED\n"
     "#define PROTOBUF_C_TYPES_$filename_identifier$_INCLUDED\n"
    "#include <google/protobuf-c/protobuf-c.h>\n"
    "#include <google/protobuf-c/protobuf-c-rpc.h>\n"
    "\n"
    "PROTOBUF_C_BEGIN_DECLS\n"
    "\n",
    "filename_identifier", filename_identifier);

  // Include dependent types header files
  for (int i = 0; i < file_->dependency_count(); i++) {
    printer->Print(
      "#include \"$dependency$.h\"\n",
      "dependency", GetAtlTypesFilename(file_->dependency(i)->name()));
  }

  //
  // include the protobuf generated header
  //
  printer->Print("#include \"$pbh$.pb-c.h\"\n", "pbh", filename_identifier);
  printer->Print("\n");

  // Add global header file for this .proto if the file "<proto>_proto_global.h" exists
  string proto_global_h = GetAtlGlobalFilename(file_->name()) + ".h";
  std::ifstream f(proto_global_h.c_str());
  if (f.good()) {
    printer->Print("#include \"$proto_global_h$\"\n", "proto_global_h", proto_global_h);
  }
  else {
    printer->Print("//#include \"$proto_global_h$\"\n", "proto_global_h", proto_global_h);
  }
  f.close();
  printer->Print("\n");

  // Generate atl structure definitions.
  printer->Print("\n/* --- atl generated structures --- */\n\n");
  for (int i = 0; i < file_->message_type_count(); i++) {
    atl_message_generators_[i]->GenerateStructTypedef(printer);
  }

  printer->Print("\n");
  for (int i = 0; i < file_->message_type_count(); i++) {
    atl_message_generators_[i]->GenerateStructDefinition(printer);
  }

  printer->Print(
    "\n"
    "PROTOBUF_C_END_DECLS\n"
    "\n\n#endif  /* PROTOBUF_TYPES_$filename_identifier$_INCLUDED */\n",
    "filename_identifier", filename_identifier);
}

void FileGenerator::GenerateAtlApiHeader(io::Printer* printer) {
  string filename_identifier = StripProto(file_->name());

  // Generate top of header.
  printer->Print(
    "/* Generated by the protocol buffer compiler.  DO NOT EDIT! */\n"
    "\n"
    "#ifndef PROTOBUF_C_API_$filename_identifier$_INCLUDED\n"
     "#define PROTOBUF_C_API_$filename_identifier$_INCLUDED\n"
    "#include <google/protobuf-c/protobuf-c.h>\n"
    "#include <google/protobuf-c/protobuf-c-rpc.h>\n"
    "\n"
    "PROTOBUF_C_BEGIN_DECLS\n"
    "\n",
    "filename_identifier", filename_identifier);

  //
  // add some includes for the ATL generated code
  //
  printer->Print("#include <string.h>\n");
  printer->Print("#include <stdlib.h>\n");
  // include the ATL types header which will also include the pbc header
  printer->Print("#include \"$pbh$.h\"\n", "pbh", GetAtlTypesFilename(file_->name()));
  printer->Print("/* include the cmsg_client definition for the api function */\n");
  printer->Print("#include <google/protobuf-c/protobuf-c-cmsg-client.h>\n");

  printer->Print("\n");

  printer->Print("\n/* --- atl generated code --- */\n\n");
  for (int i = 0; i < file_->service_count(); i++) {
    atl_code_generators_[i]->GenerateDescriptorDeclarations(printer);
  }

  // Generate atl api definitions.
  printer->Print("\n");
  for (int i = 0; i < file_->service_count(); i++) {
    atl_code_generators_[i]->GenerateMainHFile(printer, true);
  }


  printer->Print(
    "\n"
    "PROTOBUF_C_END_DECLS\n"
    "\n\n#endif  /* PROTOBUF_API_$filename_identifier$_INCLUDED */\n",
    "filename_identifier", filename_identifier);
}

void FileGenerator::GenerateAtlApiSource(io::Printer* printer) {
  printer->Print(
    "/* Generated by the protocol buffer compiler.  DO NOT EDIT! */\n"
    "\n"
    "/* Do not generate deprecated warnings for self */\n"
    "#ifndef PROTOBUF_C_NO_DEPRECATED\n"
    "#define PROTOBUF_C_NO_DEPRECATED\n"
    "#endif\n"
    "\n"
    "#include \"$basename$.h\"\n",
    "basename", GetAtlApiFilename(file_->name()));

  for (int i = 0; i < file_->service_count(); i++) {
    atl_code_generators_[i]->GenerateCFile(printer, true);
  }

}

void FileGenerator::GenerateAtlImplHeader(io::Printer* printer) {
  string filename_identifier = StripProto(file_->name());

  // Generate top of header.
  printer->Print(
    "/* Generated by the protocol buffer compiler.  DO NOT EDIT! */\n"
    "\n"
    "#ifndef PROTOBUF_C_IMPL_$filename_identifier$_INCLUDED\n"
     "#define PROTOBUF_C_IMPL_$filename_identifier$_INCLUDED\n"
    "#include <google/protobuf-c/protobuf-c.h>\n"
    "#include <google/protobuf-c/protobuf-c-rpc.h>\n"
    "\n"
    "PROTOBUF_C_BEGIN_DECLS\n"
    "\n",
    "filename_identifier", filename_identifier);

  //
  // add some includes for the ATL generated code
  //
  printer->Print("#include <string.h>\n");
  printer->Print("#include <stdlib.h>\n");
  // include the ATL types header which will also include the pbc header
  printer->Print("#include \"$pbh$.h\"\n", "pbh", GetAtlTypesFilename(file_->name()));

  printer->Print("\n");

  // Generate atl api definitions.
  printer->Print("\n/* --- atl generated code --- */\n\n");
  for (int i = 0; i < file_->service_count(); i++) {
    atl_code_generators_[i]->GenerateMainHFile(printer, false);
  }

  printer->Print(
    "\n"
    "PROTOBUF_C_END_DECLS\n"
    "\n\n#endif  /* PROTOBUF_IMPL_$filename_identifier$_INCLUDED */\n",
    "filename_identifier", filename_identifier);
}

void FileGenerator::GenerateAtlImplSource(io::Printer* printer) {
  printer->Print(
    "/* Generated by the protocol buffer compiler.  DO NOT EDIT! */\n"
    "\n"
    "/* Do not generate deprecated warnings for self */\n"
    "#ifndef PROTOBUF_C_NO_DEPRECATED\n"
    "#define PROTOBUF_C_NO_DEPRECATED\n"
    "#endif\n"
    "\n"
    "#include \"$basename$.h\"\n",
    "basename", GetAtlImplFilename(file_->name()));

  for (int i = 0; i < file_->service_count(); i++) {
    atl_code_generators_[i]->GenerateCFile(printer, false);
  }

}


}  // namespace c
}  // namespace compiler
}  // namespace protobuf
}  // namespace google
