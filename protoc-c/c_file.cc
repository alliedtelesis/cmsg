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

// Copyright (c) 2008-2014, Dave Benson and the protobuf-c authors.
// All rights reserved.
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
#include <protoc-c/c_atl_generator.h>
#endif /* ATL_CHANGE */
#include <protoc-c/c_file.h>
#include <protoc-c/c_enum.h>
#include <protoc-c/c_service.h>
#include <protoc-c/c_extension.h>
#include <protoc-c/c_helpers.h>
#include <protoc-c/c_message.h>
#include <google/protobuf/io/printer.h>
#include <google/protobuf/descriptor.pb.h>

#ifdef ATL_CHANGE
#include <protobuf-c/protobuf-c.h>
#else
#include "protobuf-c.h"
#endif /* ATL_CHANGE */

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
#ifdef ATL_CHANGE
    atl_code_generators_(
      new scoped_ptr<AtlCodeGenerator>[file->service_count()]),
#endif /* ATL_CHANGE */
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

#ifdef ATL_CHANGE
  for (int i = 0; i < file->service_count(); i++) {
    atl_code_generators_[i].reset(
      new AtlCodeGenerator(file->service(i), dllexport_decl));
  }
#endif /* ATL_CHANGE */
  for (int i = 0; i < file->extension_count(); i++) {
    extension_generators_[i].reset(
      new ExtensionGenerator(file->extension(i), dllexport_decl));
  }

  SplitStringUsing(file_->package(), ".", &package_parts_);
}

FileGenerator::~FileGenerator() {}

void FileGenerator::GenerateHeader(io::Printer* printer) {
  string filename_identifier = FilenameIdentifier(file_->name());

  int min_header_version = 1000000;
#if defined(HAVE_PROTO3)
  if (file_->syntax() == FileDescriptor::SYNTAX_PROTO3) {
    min_header_version = 1003000;
  }
#endif

  // Generate top of header.
  printer->Print(
    "/* Generated by the protocol buffer compiler.  DO NOT EDIT! */\n"
    "/* Generated from: $filename$ */\n"
    "\n"
#ifdef ATL_CHANGE
    "#ifndef PROTOBUF_C_$filename_identifier$_INCLUDED\n"
    "#define PROTOBUF_C_$filename_identifier$_INCLUDED\n"
#else
    "#ifndef PROTOBUF_C_$filename_identifier$__INCLUDED\n"
    "#define PROTOBUF_C_$filename_identifier$__INCLUDED\n"
#endif /* ATL_CHANGE */
    "\n"
    "#include <protobuf-c/protobuf-c.h>\n"
    "\n"
    "PROTOBUF_C__BEGIN_DECLS\n"
    "\n",
    "filename", file_->name(),
    "filename_identifier", filename_identifier);

  // Verify the protobuf-c library header version is compatible with the
  // protoc-c version before going any further.
  printer->Print(
    "#if PROTOBUF_C_VERSION_NUMBER < $min_header_version$\n"
    "# error This file was generated by a newer version of protoc-c which is "
    "incompatible with your libprotobuf-c headers. Please update your headers.\n"
    "#elif $protoc_version$ < PROTOBUF_C_MIN_COMPILER_VERSION\n"
    "# error This file was generated by an older version of protoc-c which is "
    "incompatible with your libprotobuf-c headers. Please regenerate this file "
    "with a newer version of protoc-c.\n"
    "#endif\n"
    "\n",
    "min_header_version", SimpleItoa(min_header_version),
    "protoc_version", SimpleItoa(PROTOBUF_C_VERSION_NUMBER));
#ifdef ATL_CHANGE
  // Add some includes for the ATL generated code
  printer->Print("#include <string.h>\n");
  printer->Print("#include <stdio.h>\n");
  printer->Print("#include <stdlib.h>\n");
  printer->Print("#include <stdbool.h>\n");
#endif /* ATL_CHANGE */

  for (int i = 0; i < file_->dependency_count(); i++) {
#ifdef ATL_CHANGE
    if ((StripProto(file_->dependency(i)->name()) != "http") &&
        (StripProto(file_->dependency(i)->name()) != "validation"))
    {
        printer->Print(
          "#include \"$dependency$.pb-c.h\"\n",
          "dependency", StripProto(file_->dependency(i)->name()));
    }
#else
    printer->Print(
      "#include \"$dependency$.pb-c.h\"\n",
      "dependency", StripProto(file_->dependency(i)->name()));
#endif /* ATL_CHANGE */
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
    "PROTOBUF_C__END_DECLS\n"
#ifdef ATL_CHANGE
    "\n\n#endif  /* PROTOBUF_$filename_identifier$_INCLUDED */\n",
#else
    "\n\n#endif  /* PROTOBUF_C_$filename_identifier$__INCLUDED */\n",
#endif /* ATL_CHANGE */
    "filename_identifier", filename_identifier);
}

void FileGenerator::GenerateSource(io::Printer* printer) {
  printer->Print(
    "/* Generated by the protocol buffer compiler.  DO NOT EDIT! */\n"
    "/* Generated from: $filename$ */\n"
    "\n"
    "/* Do not generate deprecated warnings for self */\n"
    "#ifndef PROTOBUF_C__NO_DEPRECATED\n"
    "#define PROTOBUF_C__NO_DEPRECATED\n"
    "#endif\n"
    "\n"
    "#include \"$basename$.pb-c.h\"\n",
    "filename", file_->name(),
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
#ifdef ATL_CHANGE
void FileGenerator::GenerateAtlTypesHeader(io::Printer* printer) {
  string filename_identifier = StripProto(file_->name());
  string header_define = MakeHeaderDefineFromFilename("PROTOBUF_C_TYPES_", filename_identifier);

  // Generate top of header.
  printer->Print(
    "/* Generated by the protocol buffer compiler.  DO NOT EDIT! */\n"
    "\n"
    "#ifndef $header_define$\n"
    "#define $header_define$\n"
    "#include <protobuf-c/protobuf-c.h>\n"
    "#include <cmsg/cmsg.h>\n"
    "\n"
    "PROTOBUF_C__BEGIN_DECLS\n"
    "\n",
    "header_define", header_define);

  // Include dependent types header files
  for (int i = 0; i < file_->dependency_count(); i++) {
#ifdef ATL_CHANGE
    if ((StripProto(file_->dependency(i)->name()) != "http") &&
        (StripProto(file_->dependency(i)->name()) != "validation"))
      {
        printer->Print(
          "#include \"$dependency$.h\"\n",
          "dependency", GetAtlTypesFilename(file_->dependency(i)->name()));
      }
#else
    printer->Print(
      "#include \"$dependency$.h\"\n",
      "dependency", GetAtlTypesFilename(file_->dependency(i)->name()));
#endif /* ATL_CHANGE */
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
  printer->Print(
    "\n"
    "PROTOBUF_C__END_DECLS\n"
    "\n\n#endif  /* $header_define$ */\n",
    "header_define", header_define);
}

void FileGenerator::GenerateAtlApiHeader(io::Printer* printer) {
  string filename_identifier = StripProto(file_->name());
  string header_define = MakeHeaderDefineFromFilename("PROTOBUF_C_API_", filename_identifier);

  // Generate top of header.
  printer->Print(
    "/* Generated by the protocol buffer compiler.  DO NOT EDIT! */\n"
    "\n"
    "#ifndef $header_define$\n"
    "#define $header_define$\n"
    "\n"
    "/* include the atl types header to get pbc header, cmsg.h etc */\n"
    "#include \"$types$.h\"\n"
    "PROTOBUF_C__BEGIN_DECLS\n"
    "\n",
    "header_define", header_define,
    "types", GetAtlTypesFilename(file_->name()));

  //
  // add some includes for the ATL generated code
  //
  printer->Print("#include <string.h>\n");
  printer->Print("#include <stdlib.h>\n");
  printer->Print("/* include the cmsg_client definition for the api function */\n");
  printer->Print("#include <cmsg/cmsg_client.h>\n");

  printer->Print("\n");

  printer->Print("\n/* --- atl generated code --- */\n\n");
  for (int i = 0; i < file_->service_count(); i++) {
    atl_code_generators_[i]->GenerateDescriptorDeclarations(printer);
  }

  // Generate atl api definitions.
  printer->Print("\n");
  for (int i = 0; i < file_->service_count(); i++) {
    atl_code_generators_[i]->GenerateClientHeaderFile(printer);
  }


  printer->Print(
    "\n"
    "PROTOBUF_C__END_DECLS\n"
    "\n\n#endif  /* $header_define$ */\n",
    "header_define", header_define);
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

  // include the cmsg error header so the api can output errors
  printer->Print("#include <cmsg/cmsg_error.h>\n");

  for (int i = 0; i < file_->service_count(); i++) {
    atl_code_generators_[i]->GenerateClientCFile(printer);
  }

}

void FileGenerator::GenerateAtlImplHeader(io::Printer* printer) {
  string filename_identifier = StripProto(file_->name());
  string header_define = MakeHeaderDefineFromFilename("PROTOBUF_C_IMPL_", filename_identifier);

  // Generate top of header.
  printer->Print(
    "/* Generated by the protocol buffer compiler.  DO NOT EDIT! */\n"
    "\n"
    "#ifndef $header_define$\n"
    "#define $header_define$\n"
    "\n"
    "/* include the atl types header to get pbc header, cmsg.h etc */\n"
    "#include \"$types$.h\"\n"
    "PROTOBUF_C__BEGIN_DECLS\n"
    "\n",
    "header_define", header_define,
    "types", GetAtlTypesFilename(file_->name()));

  //
  // add some includes for the ATL generated code
  //
  printer->Print("#include <string.h>\n");
  printer->Print("#include <stdlib.h>\n");
  // users of the impl will need the server definitions
  printer->Print("#include <cmsg/cmsg_server.h>\n");


  printer->Print("\n");

  // Generate atl api definitions.
  printer->Print("\n/* --- atl generated code --- */\n\n");
  for (int i = 0; i < file_->service_count(); i++) {
    atl_code_generators_[i]->GenerateServerHeaderFile(printer);
  }

  printer->Print(
    "\n"
    "PROTOBUF_C__END_DECLS\n"
    "\n\n#endif  /* $header_define$ */\n",
    "header_define", header_define);
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
    atl_code_generators_[i]->GenerateServerCFile(printer);
  }

}

void FileGenerator::GenerateAtlImplStubs(io::Printer* printer) {
  printer->Print(
    "/* Generated by the cmsg compiler! */\n"
    "\n"
    "/* Do not build this file. It is generated to assist developers creating new\n"
    " * CMSG servers.  The stub functions can be copied into the server implementation\n"
    " * to get things building and provide a base for implementation.\n"
    " */\n"
    "\n"
    "\n");

  for (int i = 0; i < file_->service_count(); i++) {
      atl_code_generators_[i]->GenerateAtlServerImplStubs(printer);
    }
}

void FileGenerator::GenerateAtlHttpProxySource(io::Printer* printer) {
  string basename = StripProto(file_->name());

  printer->Print(
    "/* Generated by the protocol buffer compiler.  DO NOT EDIT! */\n"
    "\n"
    "/* Do not generate deprecated warnings for self */\n"
    "#ifndef PROTOBUF_C_NO_DEPRECATED\n"
    "#define PROTOBUF_C_NO_DEPRECATED\n"
    "#endif\n"
    "\n"
    "#include \"$basename$_proxy_def.h\"\n",
    "basename", basename);

  printer->Print("#include \"$api_filename$.h\"\n",
                 "api_filename", GetAtlApiFilename(file_->name()));

  // Don't bother generating code if the file has no services
  if (file_->service_count() == 0)
  {
      return;
  }

  printer->Print("\n");

  // generate the cmsg proxy array
  printer->Print("static cmsg_service_info service_info_entries[] = {\n");
  for (int i = 0; i < file_->service_count(); i++) {
    atl_code_generators_[i]->GenerateHttpProxyArrayEntries(printer);
  }
  printer->Print("};\n\n");

  // generate the cmsg proxy array size
  printer->Print("static const int num_service_info_entries = (sizeof (service_info_entries) /\n");
  printer->Print("                                             sizeof (service_info_entries[0]));\n\n");

  // generate the cmsg proxy array functions
  atl_code_generators_[0]->GenerateHttpProxyArrayFunctions (printer);
}

void FileGenerator::GenerateAtlHttpProxyHeader(io::Printer* printer) {
    string filename_identifier = StripProto(file_->name());
    string header_define = MakeHeaderDefineFromFilename("PROTOBUF_C_PROXY_", filename_identifier);

    // Generate top of header.
    printer->Print(
      "/* Generated by the protocol buffer compiler.  DO NOT EDIT! */\n"
      "\n"
      "#ifndef $header_define$\n"
      "#define $header_define$\n"
      "\n"
      "#include <cmsg-proxy/cmsg_proxy.h>\n"
      "\n",
      "header_define", header_define);

    // Only generate function definitions if the file has services
    if (file_->service_count() != 0)
    {
        atl_code_generators_[0]->GenerateHttpProxyArrayFunctionDefs (printer);
    }

    printer->Print(
      "\n"
      "#endif  /* $header_define$ */\n",
      "header_define", header_define);
}
#endif /* ATL_CHANGE */

}  // namespace c
}  // namespace compiler
}  // namespace protobuf
}  // namespace google
