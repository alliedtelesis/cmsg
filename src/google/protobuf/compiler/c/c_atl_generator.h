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
  // See generator.cc for the meaning of dllexport_decl.
  explicit AtlCodeGenerator(const ServiceDescriptor* descriptor,
                            const string& dllexport_decl);
  ~AtlCodeGenerator();

  // Header stuff.
  void GenerateMainHFile(io::Printer* printer);
  void GenerateVfuncs(io::Printer* printer);
  void GenerateInitMacros(io::Printer* printer);
  void GenerateDescriptorDeclarations(io::Printer* printer);
  void GenerateCallersDeclarations(io::Printer* printer);

  // Source file stuff.
  void GenerateCFile(io::Printer* printer);
  void GenerateServiceDescriptor(io::Printer* printer);
  void GenerateInit(io::Printer* printer);
  void GenerateCallersImplementations(io::Printer* printer);

 private:
  void GenerateAtlHeader(io::Printer* printer);
  void GenerateAtlStructDefinitions(io::Printer* printer);
  void GenerateStructDefinitionsFromMessage(io::Printer* printer, const Descriptor *message, bool firstMessage);
  void DumpMessageDefinitions(io::Printer* printer);
  void GenerateAtlApiDefinitions(io::Printer* printer, bool forHeader);
  void GenerateAtlApiDefinition(const MethodDescriptor &method, io::Printer* printer, bool forHeader);
  void GenerateParameterListFromMessage(io::Printer* printer, const Descriptor *message, bool output);

  void GenerateAtlApiImplementation(io::Printer* printer);
  void GenerateAtlApiClosureFunction(const MethodDescriptor &method, io::Printer* printer);
  string GetAtlClosureFunctionName(const MethodDescriptor &method);
  void GenerateMessageCopyCode(const Descriptor *message, const string lhm, const string rhm, io::Printer *printer, bool allocate_memory);
  void GenerateSendMessageCopyCode(const Descriptor *message, const string message_name, io::Printer *printer);
  void GenerateReceiveMessageCopyCode(const Descriptor *message, const string message_name, io::Printer *printer);
  void GenerateCleanupMessageMemoryCode(const Descriptor *message, const string lhm, io::Printer *printer);


  void PrintMessageFields(io::Printer* printer, const Descriptor *message);
  string TypeToString(FieldDescriptor::Type type);
  bool MessageContainsSubMessages(io::Printer* printer, const Descriptor *message);
  bool MessageContainsRepeatedFields(io::Printer* printer, const Descriptor *message);

  const ServiceDescriptor* descriptor_;
  map<string, string> vars_;

  GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(AtlCodeGenerator);
};

}  // namespace c
}  // namespace compiler
}  // namespace protobuf

}  // namespace google
#endif  // GOOGLE_PROTOBUF_COMPILER_C_ATL_CODE_GENERATOR_H__
