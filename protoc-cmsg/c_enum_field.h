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

#ifndef GOOGLE_PROTOBUF_COMPILER_C_ENUM_FIELD_H__
#define GOOGLE_PROTOBUF_COMPILER_C_ENUM_FIELD_H__

#include <map>
#include <string>
#ifdef ATL_CHANGE
#include <protoc-cmsg/c_field.h>
#else
#include <google/protobuf/compiler/c/c_field.h>
#endif /* ATL_CHANGE */

namespace google {
namespace protobuf {
namespace compiler {
namespace c {

class EnumFieldGenerator : public FieldGenerator {
 public:
  explicit EnumFieldGenerator(const FieldDescriptor* descriptor);
  ~EnumFieldGenerator();

  // implements FieldGenerator ---------------------------------------
  void GenerateStructMembers(io::Printer* printer) const;
  void GenerateDescriptorInitializer(io::Printer* printer) const;
  string GetDefaultValue(void) const;
  void GenerateStaticInit(io::Printer* printer) const;

 private:
  map<string, string> variables_;

  GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(EnumFieldGenerator);
};


}  // namespace c
}  // namespace compiler
}  // namespace protobuf

}  // namespace google
#endif  // GOOGLE_PROTOBUF_COMPILER_C_ENUM_FIELD_H__
