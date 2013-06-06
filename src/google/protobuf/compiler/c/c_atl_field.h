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

#ifndef GOOGLE_PROTOBUF_COMPILER_C_ATL_FIELD_H__
#define GOOGLE_PROTOBUF_COMPILER_C_ATL_FIELD_H__

#include <google/protobuf/stubs/common.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/compiler/c/c_field.h>

namespace google {
namespace protobuf {
  namespace io {
    class Printer;             // printer.h
  }
}

namespace protobuf {
namespace compiler {
namespace c {

class AtlFieldGenerator : public FieldGenerator {
 public:
  explicit AtlFieldGenerator(const FieldDescriptor *descriptor) : FieldGenerator(descriptor) {}
  ~AtlFieldGenerator();

  // Generate definitions to be included in the structure.
  void GenerateStructMembers(io::Printer* printer) const = 0;

  // Generate a static initializer for this field.
  void GenerateDescriptorInitializer(io::Printer* printer) const = 0;

  void GenerateDefaultValueDeclarations(io::Printer* printer) const { }
  void GenerateDefaultValueImplementations(io::Printer* printer) const { }
  string GetDefaultValue() const = 0;

  // Generate members to initialize this field from a static initializer
  void GenerateStaticInit(io::Printer* printer) const = 0;


 protected:
  void GenerateDescriptorInitializerGeneric(io::Printer* printer,
                                            bool optional_uses_has,
                                            const string &type_macro,
                                            const string &descriptor_addr) const;
  const FieldDescriptor *descriptor_;

 private:
  GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(AtlFieldGenerator);
};

// Convenience class which constructs AtlFieldGenerators for a Descriptor.
class AtlFieldGeneratorMap {
 public:
  explicit AtlFieldGeneratorMap(const Descriptor* descriptor);
  ~AtlFieldGeneratorMap();

  const AtlFieldGenerator& get(const FieldDescriptor* field) const;

 private:
  const Descriptor* descriptor_;
  scoped_array<scoped_ptr<AtlFieldGenerator> > field_generators_;

  static AtlFieldGenerator* MakeGenerator(const FieldDescriptor* field);

  GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(AtlFieldGeneratorMap);
};

}  // namespace c
}  // namespace compiler
}  // namespace protobuf

}  // namespace google
#endif  // GOOGLE_PROTOBUF_COMPILER_C_ATL_FIELD_H__
