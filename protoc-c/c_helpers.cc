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

#include <vector>
#include <set>
#include <stdio.h>		// for snprintf
#include <float.h>

#include <protoc-c/c_helpers.h>
#include <google/protobuf/stubs/common.h>

namespace google {
namespace protobuf {
namespace compiler {
namespace c {

#if defined(_MSC_VER)
// FIXME: In the case where the generated string is longer than the buffer,
// _snprint() returns a negative value, where snprintf() returns the number
// of characters that *would* have been stored, had there been room.
// That is fundamental, as it allows snprintf() to be used to find the size
// necessary for the buffer, simply by calling it with the size of the buffer
// passed in as zero.
// Note: at the present moment return value isn't used in the code.
#define snprintf _snprintf
#pragma warning(disable:4800)
#pragma warning(disable:4996)
#endif

string DotsToUnderscores(const string& name) {
  return StringReplace(name, ".", "_", true);
}

string DotsToColons(const string& name) {
  return StringReplace(name, ".", "::", true);
}

string CamelToUpper(const string &name) {
  bool was_upper = true;		// suppress initial _
  string rv = "";
  int len = name.length();
  for (int i = 0; i < len; i++) {
    bool is_upper = isupper(name[i]);
    if (is_upper) {
      if (!was_upper)
	rv += '_';
      rv += name[i];
    } else {
      rv += toupper(name[i]);
    }
    was_upper = is_upper;
  }
  return rv;
}
string CamelToLower(const string &name) {
  bool was_upper = true;		// suppress initial _
  string rv = "";
  int len = name.length();
  for (int i = 0; i < len; i++) {
    bool is_upper = isupper(name[i]);
    if (is_upper) {
      if (!was_upper)
	rv += '_';
      rv += tolower(name[i]);
    } else {
      rv += name[i];
    }
    was_upper = is_upper;
  }
  return rv;
}


string ToUpper(const string &name) {
  string rv = "";
  int len = name.length();
  for (int i = 0; i < len; i++) {
    rv += toupper(name[i]);
  }
  return rv;
}
string ToLower(const string &name) {
  string rv = "";
  int len = name.length();
  for (int i = 0; i < len; i++) {
    rv += tolower(name[i]);
  }
  return rv;
}
string ToCamel(const string &name) {
  string rv = "";
  int len = name.length();
  bool next_is_upper = true;
  for (int i = 0; i < len; i++) {
    if (name[i] == '_') {
      next_is_upper = true;
    } else if (next_is_upper) {
      rv += toupper (name[i]);
      next_is_upper = false;
    } else {
      rv += name[i];
    }
  }
  return rv;
}

string FullNameToLower(const string &full_name) {
  vector<string> pieces;
  SplitStringUsing(full_name, ".", &pieces);
  string rv = "";
  for (unsigned i = 0; i < pieces.size(); i++) {
    if (pieces[i] == "") continue;
    if (rv != "") rv += "__";
    rv += CamelToLower(pieces[i]);
  }
  return rv;
}
string FullNameToUpper(const string &full_name) {
  vector<string> pieces;
  SplitStringUsing(full_name, ".", &pieces);
  string rv = "";
  for (unsigned i = 0; i < pieces.size(); i++) {
    if (pieces[i] == "") continue;
    if (rv != "") rv += "__";
    rv += CamelToUpper(pieces[i]);
  }
  return rv;
}
string FullNameToC(const string &full_name) {
  vector<string> pieces;
  SplitStringUsing(full_name, ".", &pieces);
  string rv = "";
  for (unsigned i = 0; i < pieces.size(); i++) {
    if (pieces[i] == "") continue;
    if (rv != "") rv += "__";
    rv += ToCamel(pieces[i]);
  }
  return rv;
}

string ConvertToSpaces(const string &input) {
  return string(input.size(), ' ');
}

string CEscape(const string& src);

const char* const kKeywordList[] = {
  "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool", "break", "case",
  "catch", "char", "class", "compl", "const", "const_cast", "continue",
  "default", "delete", "do", "double", "dynamic_cast", "else", "enum",
  "explicit", "extern", "false", "float", "for", "friend", "goto", "if",
  "inline", "int", "long", "mutable", "namespace", "new", "not", "not_eq",
  "operator", "or", "or_eq", "private", "protected", "public", "register",
  "reinterpret_cast", "return", "short", "signed", "sizeof", "static",
  "static_cast", "struct", "switch", "template", "this", "throw", "true", "try",
  "typedef", "typeid", "typename", "union", "unsigned", "using", "virtual",
  "void", "volatile", "wchar_t", "while", "xor", "xor_eq"
};

std::set<string> MakeKeywordsMap() {
  std::set<string> result;
  for (int i = 0; i < GOOGLE_ARRAYSIZE(kKeywordList); i++) {
    result.insert(kKeywordList[i]);
  }
  return result;
}

std::set<string> kKeywords = MakeKeywordsMap();

string FieldName(const FieldDescriptor* field) {
  string result = ToLower(field->name());
  if (kKeywords.count(result) > 0) {
    result.append("_");
  }
  return result;
}

string FieldDeprecated(const FieldDescriptor* field) {
  if (field->options().deprecated()) {
    return " PROTOBUF_C__DEPRECATED";
  }
  return "";
}

string StripProto(const string& filename) {
  if (HasSuffixString(filename, ".protodevel")) {
    return StripSuffixString(filename, ".protodevel");
  } else {
    return StripSuffixString(filename, ".proto");
  }
}

// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXx
// XXXXXXXXX  this stuff is copied from strutils.cc !!!!   XXXXXXXXXXXXXXXXXXXXXXXXXXXXx
// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXx
// ----------------------------------------------------------------------
// StringReplace()
//    Replace the "old" pattern with the "new" pattern in a string,
//    and append the result to "res".  If replace_all is false,
//    it only replaces the first instance of "old."
// ----------------------------------------------------------------------

void StringReplace(const string& s, const string& oldsub,
                   const string& newsub, bool replace_all,
                   string* res) {
  if (oldsub.empty()) {
    res->append(s);  // if empty, append the given string.
    return;
  }

  string::size_type start_pos = 0;
  string::size_type pos;
  do {
    pos = s.find(oldsub, start_pos);
    if (pos == string::npos) {
      break;
    }
    res->append(s, start_pos, pos - start_pos);
    res->append(newsub);
    start_pos = pos + oldsub.size();  // start searching again after the "old"
  } while (replace_all);
  res->append(s, start_pos, s.length() - start_pos);
}


// ----------------------------------------------------------------------
// StringReplace()
//    Give me a string and two patterns "old" and "new", and I replace
//    the first instance of "old" in the string with "new", if it
//    exists.  If "global" is true; call this repeatedly until it
//    fails.  RETURN a new string, regardless of whether the replacement
//    happened or not.
// ----------------------------------------------------------------------

string StringReplace(const string& s, const string& oldsub,
                     const string& newsub, bool replace_all) {
  string ret;
  StringReplace(s, oldsub, newsub, replace_all, &ret);
  return ret;
}

// ----------------------------------------------------------------------
// SplitStringUsing()
//    Split a string using a character delimiter. Append the components
//    to 'result'.
//
// Note: For multi-character delimiters, this routine will split on *ANY* of
// the characters in the string, not the entire string as a single delimiter.
// ----------------------------------------------------------------------
template <typename ITR>
static inline
void SplitStringToIteratorUsing(const string& full,
                                const char* delim,
                                ITR& result) {
  // Optimize the common case where delim is a single character.
  if (delim[0] != '\0' && delim[1] == '\0') {
    char c = delim[0];
    const char* p = full.data();
    const char* end = p + full.size();
    while (p != end) {
      if (*p == c) {
        ++p;
      } else {
        const char* start = p;
        while (++p != end && *p != c);
        *result++ = string(start, p - start);
      }
    }
    return;
  }

  string::size_type begin_index, end_index;
  begin_index = full.find_first_not_of(delim);
  while (begin_index != string::npos) {
    end_index = full.find_first_of(delim, begin_index);
    if (end_index == string::npos) {
      *result++ = full.substr(begin_index);
      return;
    }
    *result++ = full.substr(begin_index, (end_index - begin_index));
    begin_index = full.find_first_not_of(delim, end_index);
  }
}

void SplitStringUsing(const string& full,
                      const char* delim,
                      vector<string>* result) {
  std::back_insert_iterator< vector<string> > it(*result);
  SplitStringToIteratorUsing(full, delim, it);
}

char* FastHexToBuffer(int i, char* buffer)
{
  snprintf(buffer, 16, "%x", i);
  return buffer;
}


static int CEscapeInternal(const char* src, int src_len, char* dest,
                           int dest_len, bool use_hex) {
  const char* src_end = src + src_len;
  int used = 0;
  bool last_hex_escape = false; // true if last output char was \xNN

  for (; src < src_end; src++) {
    if (dest_len - used < 2)   // Need space for two letter escape
      return -1;

    bool is_hex_escape = false;
    switch (*src) {
      case '\n': dest[used++] = '\\'; dest[used++] = 'n';  break;
      case '\r': dest[used++] = '\\'; dest[used++] = 'r';  break;
      case '\t': dest[used++] = '\\'; dest[used++] = 't';  break;
      case '\"': dest[used++] = '\\'; dest[used++] = '\"'; break;
      case '\'': dest[used++] = '\\'; dest[used++] = '\''; break;
      case '\\': dest[used++] = '\\'; dest[used++] = '\\'; break;
      default:
        // Note that if we emit \xNN and the src character after that is a hex
        // digit then that digit must be escaped too to prevent it being
        // interpreted as part of the character code by C.
        if (!isprint(*src) || (last_hex_escape && isxdigit(*src))) {
          if (dest_len - used < 4) // need space for 4 letter escape
            return -1;
          sprintf(dest + used, (use_hex ? "\\x%02x" : "\\%03o"),
                  static_cast<uint8>(*src));
          is_hex_escape = use_hex;
          used += 4;
        } else {
          dest[used++] = *src; break;
        }
    }
    last_hex_escape = is_hex_escape;
  }

  if (dest_len - used < 1)   // make sure that there is room for \0
    return -1;

  dest[used] = '\0';   // doesn't count towards return value though
  return used;
}
string CEscape(const string& src) {
  const int dest_length = src.size() * 4 + 1; // Maximum possible expansion
  scoped_array<char> dest(new char[dest_length]);
  const int len = CEscapeInternal(src.data(), src.size(),
                                  dest.get(), dest_length, false);
  GOOGLE_DCHECK_GE(len, 0);
  return string(dest.get(), len);
}

}  // namespace c
}  // namespace compiler
}  // namespace protobuf
}  // namespace google
