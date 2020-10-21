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

#ifndef GOOGLE_PROTOBUF_COMPILER_C_ATL_CODE_GENERATOR_H__
#define GOOGLE_PROTOBUF_COMPILER_C_ATL_CODE_GENERATOR_H__

#include <map>
#include <string>
#include <google/protobuf/descriptor.h>
#include "google/api/http.pb.h"
#include "google/api/annotations.pb.h"

namespace google {
namespace protobuf {
  namespace io {
    class Printer;             // printer.h
  }
}

namespace protobuf {
namespace compiler {
namespace c {

class AtlCodeGenerator {
 public:
  explicit AtlCodeGenerator(const ServiceDescriptor* descriptor);
  ~AtlCodeGenerator();

  // Header stuff.
  void GenerateDescriptorDeclarations(io::Printer* printer);
  void GenerateClientHeaderFile(io::Printer* printer);
  void GenerateServerHeaderFile(io::Printer* printer);

  // Source file stuff.
  void GenerateClientCFile(io::Printer* printer);
  void GenerateServerCFile(io::Printer* printer);

  // Http proxy stuff
  void GenerateHttpProxyArrayEntries(io::Printer* printer);
  void GenerateHttpProxyArrayEntriesPerMethod(const MethodDescriptor &method, io::Printer* printer);
  void GenerateHttpProxyArrayEntry(const ::google::api::HttpRule &http_rule, io::Printer* printer);
  void GenerateHttpProxyArrayFunctions(io::Printer* printer);
  void GenerateHttpProxyArrayFunctionDefs(io::Printer* printer);

  // helper function for the conversion of AW+ to use cmsg
  void GenerateAtlServerImplStubs(io::Printer* printer);

 private:

  void GenerateAtlApiDefinitions(io::Printer* printer, bool forHeader);
  void GenerateAtlApiDefinition(const MethodDescriptor &method, io::Printer* printer, bool forHeader);
  void GenerateAtlApiImplementation(io::Printer* printer);

  void GenerateAtlServerDefinitions(io::Printer* printer, bool forHeader);
  void GenerateAtlServerDefinition(const MethodDescriptor &method, io::Printer* printer, bool forHeader);
  void GenerateAtlServerCFileDefinitions(io::Printer* printer);
  void GenerateAtlServerImplementation(io::Printer* printer);

  void GenerateAtlServerImplDefinition(const MethodDescriptor &method, io::Printer* printer, bool forHeader);
  void GenerateAtlServerImplStub(const MethodDescriptor &method, io::Printer* printer);
  void GenerateAtlServerSendDefinition(const MethodDescriptor &method, io::Printer* printer);

  void GenerateServiceSupportCheck (const ServiceDescriptor* descriptor_, io::Printer* printer, bool forHeader);

  // useful functions
  string GetAtlClosureFunctionName(const MethodDescriptor &method);

  const ServiceDescriptor* descriptor_;
  std::map<string, string> vars_;

  GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(AtlCodeGenerator);
};

}  // namespace c
}  // namespace compiler
}  // namespace protobuf

}  // namespace google
#endif  // GOOGLE_PROTOBUF_COMPILER_C_ATL_CODE_GENERATOR_H__
