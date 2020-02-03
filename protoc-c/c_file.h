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

#ifndef GOOGLE_PROTOBUF_COMPILER_C_FILE_H__
#define GOOGLE_PROTOBUF_COMPILER_C_FILE_H__

#include <string>
#include <vector>
#ifdef ATL_CHANGE
#include <fstream>
#endif /* ATL_CHANGE */
#include <google/protobuf/stubs/common.h>

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

class FileGenerator {
 public:
  explicit FileGenerator(const FileDescriptor* file);
  ~FileGenerator();

#ifdef ATL_CHANGE
  void GenerateAtlTypesHeader(io::Printer* printer);
  void GenerateAtlApiHeader(io::Printer* printer);
  void GenerateAtlApiSource(io::Printer* printer);
  void GenerateAtlImplHeader(io::Printer* printer);
  void GenerateAtlImplSource(io::Printer* printer);
  void GenerateAtlImplStubs(io::Printer* printer);
  void GenerateAtlHttpProxySource(io::Printer* printer);
  void GenerateAtlHttpProxyHeader(io::Printer* printer);
  void GenerateAtlValidationSource(io::Printer* printer);
  void GenerateAtlValidationHeader(io::Printer* printer);
#endif /* ATL_CHANGE */

 private:
  const FileDescriptor* file_;

  scoped_array<scoped_ptr<MessageGenerator> > message_generators_;
  scoped_array<scoped_ptr<EnumGenerator> > enum_generators_;
  scoped_array<scoped_ptr<ServiceGenerator> > service_generators_;
#ifdef ATL_CHANGE
  scoped_array<scoped_ptr<AtlCodeGenerator> > atl_code_generators_;
#endif /* ATL_CHANGE */

  // E.g. if the package is foo.bar, package_parts_ is {"foo", "bar"}.
  vector<string> package_parts_;

  GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(FileGenerator);
};

}  // namespace c
}  // namespace compiler
}  // namespace protobuf

}  // namespace google
#endif  // GOOGLE_PROTOBUF_COMPILER_C_FILE_H__
