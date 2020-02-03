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

#ifndef GOOGLE_PROTOBUF_COMPILER_C_HELPERS_CMSG_H__
#define GOOGLE_PROTOBUF_COMPILER_C_HELPERS_CMSG_H__

#include <string>
#include <vector>
#include <sstream>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/io/printer.h>

namespace google {
namespace protobuf {
namespace compiler {
namespace cmsg {

void SplitStringUsing(const string &str, const char *delim, std::vector<string> *out);
string CEscape(const string& src);
string StringReplace(const string& s, const string& oldsub, const string& newsub, bool replace_all);

inline bool HasSuffixString(const string& str, const string& suffix) { return str.size() >= suffix.size() && str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0; }
inline string StripSuffixString(const string& str, const string& suffix) { if (HasSuffixString(str, suffix)) { return str.substr(0, str.size() - suffix.size()); } else { return str; } }
char* FastHexToBuffer(int i, char* buffer);


// Get the (unqualified) name that should be used for this field in C code.
// The name is coerced to lower-case to emulate proto1 behavior.  People
// should be using lowercase-with-underscores style for proto field names
// anyway, so normally this just returns field->name().
string FieldName(const FieldDescriptor* field);

// Get macro string for deprecated field
string FieldDeprecated(const FieldDescriptor* field);

// convert a CamelCase class name into an all uppercase affair
// with underscores separating words, e.g. MyClass becomes MY_CLASS.
string CamelToUpper(const string &class_name);
string CamelToLower(const string &class_name);

// lowercased, underscored name to camel case
string ToCamel(const string &name);

// lowercase the string
string ToLower(const string &class_name);
string ToUpper(const string &class_name);

// full_name() to lowercase with underscores
string FullNameToLower(const string &full_name);
string FullNameToUpper(const string &full_name);

// full_name() to c-typename (with underscores for packages, otherwise camel case)
string FullNameToC(const string &class_name);

// make a string of spaces as long as input
string ConvertToSpaces(const string &input);

// Strips ".proto" or ".protodevel" from the end of a filename.
string StripProto(const string& filename);

string GetAtlFilename(const string &protoname, const string &filetype);
string GetAtlTypesFilename(const string &protoname);
string GetAtlApiFilename(const string &protoname);
string GetAtlImplFilename(const string &protoname);
string GetAtlValidationFilename(const string &protoname);
string GetAtlGlobalFilename(const string &protoname);
string GetPackageName(const string &full_name);
string GetPackageNameUpper(const string &full_name);
string MakeHeaderDefineFromFilename(const string &prefix, const string &filename);

}  // namespace cmsg
}  // namespace compiler
}  // namespace protobuf

}  // namespace google
#endif  // GOOGLE_PROTOBUF_COMPILER_C_HELPERS_CMSG_H__
