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

#ifndef GOOGLE_PROTOBUF_COMPILER_C_FILE_H__
#define GOOGLE_PROTOBUF_COMPILER_C_FILE_H__

#include <string>
#include <vector>
#ifdef ATL_CHANGE
#include <fstream>
#endif /* ATL_CHANGE */
#include <google/protobuf/stubs/common.h>
#ifdef ATL_CHANGE
#include <protoc-cmsg/c_field.h>
#else
#include <google/protobuf/compiler/c/c_field.h>
#endif /* ATL_CHANGE */

namespace google {
namespace protobuf {
  class FileDescriptor;        // descriptor.h
  namespace io {
    class Printer;             // printer.h
  }
}

namespace protobuf {
namespace compiler {
namespace c {

class EnumGenerator;           // enum.h
class MessageGenerator;        // message.h
class ServiceGenerator;        // service.h
#ifdef ATL_CHANGE
class AtlCodeGenerator;        // atl_generator.h
#endif /* ATL_CHANGE */
class ExtensionGenerator;      // extension.h

class FileGenerator {
 public:
  // See generator.cc for the meaning of dllexport_decl.
  explicit FileGenerator(const FileDescriptor* file,
                         const string& dllexport_decl);
  ~FileGenerator();

  void GenerateHeader(io::Printer* printer);
  void GenerateSource(io::Printer* printer);
#ifdef ATL_CHANGE
  void GenerateAtlTypesHeader(io::Printer* printer);
  void GenerateAtlApiHeader(io::Printer* printer);
  void GenerateAtlApiSource(io::Printer* printer);
  void GenerateAtlImplHeader(io::Printer* printer);
  void GenerateAtlImplSource(io::Printer* printer);
  void GenerateAtlImplStubs(io::Printer* printer);
  void GenerateAtlHttpProxyDefinition(io::Printer* printer);
#endif /* ATL_CHANGE */

 private:
  const FileDescriptor* file_;

  scoped_array<scoped_ptr<MessageGenerator> > message_generators_;
  scoped_array<scoped_ptr<EnumGenerator> > enum_generators_;
  scoped_array<scoped_ptr<ServiceGenerator> > service_generators_;
#ifdef ATL_CHANGE
  scoped_array<scoped_ptr<AtlCodeGenerator> > atl_code_generators_;
#endif /* ATL_CHANGE */
  scoped_array<scoped_ptr<ExtensionGenerator> > extension_generators_;

  // E.g. if the package is foo.bar, package_parts_ is {"foo", "bar"}.
  vector<string> package_parts_;

  GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(FileGenerator);
};

}  // namespace c
}  // namespace compiler
}  // namespace protobuf

}  // namespace google
#endif  // GOOGLE_PROTOBUF_COMPILER_C_FILE_H__
