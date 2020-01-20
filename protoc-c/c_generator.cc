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

#include <protoc-c/c_generator.h>

#include <vector>
#include <utility>

#include <protoc-c/c_file.h>
#include <protoc-c/c_helpers.h>
#include <google/protobuf/io/printer.h>
#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/descriptor.pb.h>

#ifdef ATL_CHANGE
#include <protoc-c/c_helpers_cmsg.h>
#endif /* ATL_CHANGE */

namespace google {
namespace protobuf {
namespace compiler {
namespace c {

CGenerator::CGenerator() {}
CGenerator::~CGenerator() {}

bool CGenerator::Generate(const FileDescriptor* file,
                            const string& parameter,
                            OutputDirectory* output_directory,
                            string* error) const {
  string basename = StripProto(file->name());
  basename.append(".pb-c");

  FileGenerator file_generator(file);

#ifdef ATL_CHANGE
  // generate the atl types header file
  string types_basename = cmsg::GetAtlTypesFilename(file->name());

  {
    scoped_ptr<io::ZeroCopyOutputStream> output(
          output_directory->Open(types_basename + ".h"));
    io::Printer printer(output.get(), '$');
    file_generator.GenerateAtlTypesHeader(&printer);
  }

  // generate the atl api header and source files
  string api_basename = cmsg::GetAtlApiFilename(file->name());

  {
    scoped_ptr<io::ZeroCopyOutputStream> output(
          output_directory->Open(api_basename + ".h"));
    io::Printer printer(output.get(), '$');
    file_generator.GenerateAtlApiHeader(&printer);
  }

  // Generate c file.
  {
    scoped_ptr<io::ZeroCopyOutputStream> output(
          output_directory->Open(api_basename + ".c"));
    io::Printer printer(output.get(), '$');
    file_generator.GenerateAtlApiSource(&printer);
  }

  // now generate the atl impl header and source files
  string impl_basename = cmsg::GetAtlImplFilename(file->name());

  {
    scoped_ptr<io::ZeroCopyOutputStream> output(
          output_directory->Open(impl_basename + ".h"));
    io::Printer printer(output.get(), '$');
    file_generator.GenerateAtlImplHeader(&printer);
  }

  // Generate c file.
  {
    scoped_ptr<io::ZeroCopyOutputStream> output(
          output_directory->Open(impl_basename + ".c"));
    io::Printer printer(output.get(), '$');
    file_generator.GenerateAtlImplSource(&printer);
  }

  // Generate impl stubs file.
  {
    scoped_ptr<io::ZeroCopyOutputStream> output(
          output_directory->Open(impl_basename + "_stubs.c"));
    io::Printer printer(output.get(), '$');
    file_generator.GenerateAtlImplStubs(&printer);
  }

  // Generate http proxy source file
  {
    scoped_ptr<io::ZeroCopyOutputStream> output(
          output_directory->Open(StripProto(file->name()) + "_proxy_def.c"));
    io::Printer printer(output.get(), '$');
    file_generator.GenerateAtlHttpProxySource(&printer);
  }

  // Generate http proxy header file
  {
    scoped_ptr<io::ZeroCopyOutputStream> output(
          output_directory->Open(StripProto(file->name()) + "_proxy_def.h"));
    io::Printer printer(output.get(), '$');
    file_generator.GenerateAtlHttpProxyHeader(&printer);
  }

  // Generate validation source file
  {
    scoped_ptr<io::ZeroCopyOutputStream> output(
          output_directory->Open(StripProto(file->name()) + "_validation_auto.c"));
    io::Printer printer(output.get(), '$');
    file_generator.GenerateAtlValidationSource(&printer);
  }

  // Generate validation header file
  {
    scoped_ptr<io::ZeroCopyOutputStream> output(
          output_directory->Open(StripProto(file->name()) + "_validation_auto.h"));
    io::Printer printer(output.get(), '$');
    file_generator.GenerateAtlValidationHeader(&printer);
  }
#endif /* ATL_CHANGE */

  return true;
}

}  // namespace c
}  // namespace compiler
}  // namespace protobuf
}  // namespace google
