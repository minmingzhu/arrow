// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

// String functions

extern "C" {

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "./types.h"

FORCE_INLINE
int32 octet_length_utf8(const utf8 input, int32 length) { return length; }

FORCE_INLINE
int32 bit_length_utf8(const utf8 input, int32 length) { return length * 8; }

FORCE_INLINE
int32 octet_length_binary(const binary input, int32 length) { return length; }

FORCE_INLINE
int32 bit_length_binary(const binary input, int32 length) { return length * 8; }

FORCE_INLINE
int32 mem_compare(const char* left, int32 left_len, const char* right, int32 right_len) {
  int min = left_len;
  if (right_len < min) {
    min = right_len;
  }

  int cmp_ret = memcmp(left, right, min);
  if (cmp_ret != 0) {
    return cmp_ret;
  } else {
    return left_len - right_len;
  }
}

// Expand inner macro for all varlen types.
#define VAR_LEN_OP_TYPES(INNER, NAME, OP) \
  INNER(NAME, utf8, OP)                   \
  INNER(NAME, binary, OP)

// Relational binary fns : left, right params are same, return is bool.
#define BINARY_RELATIONAL(NAME, TYPE, OP)                                        \
  FORCE_INLINE                                                                   \
  bool NAME##_##TYPE##_##TYPE(const TYPE left, int32 left_len, const TYPE right, \
                              int32 right_len) {                                 \
    return mem_compare(left, left_len, right, right_len) OP 0;                   \
  }

VAR_LEN_OP_TYPES(BINARY_RELATIONAL, equal, ==)
VAR_LEN_OP_TYPES(BINARY_RELATIONAL, not_equal, !=)
VAR_LEN_OP_TYPES(BINARY_RELATIONAL, less_than, <)
VAR_LEN_OP_TYPES(BINARY_RELATIONAL, less_than_or_equal_to, <=)
VAR_LEN_OP_TYPES(BINARY_RELATIONAL, greater_than, >)
VAR_LEN_OP_TYPES(BINARY_RELATIONAL, greater_than_or_equal_to, >=)

// Expand inner macro for all varlen types.
#define VAR_LEN_TYPES(INNER, NAME) \
  INNER(NAME, utf8)                \
  INNER(NAME, binary)

FORCE_INLINE
bool starts_with_utf8_utf8(const char* data, int32 data_len, const char* prefix,
                           int32 prefix_len) {
  return ((data_len >= prefix_len) && (memcmp(data, prefix, prefix_len) == 0));
}

FORCE_INLINE
bool ends_with_utf8_utf8(const char* data, int32 data_len, const char* suffix,
                         int32 suffix_len) {
  return ((data_len >= suffix_len) &&
          (memcmp(data + data_len - suffix_len, suffix, suffix_len) == 0));
}

FORCE_INLINE
int32 utf8_char_length(char c) {
  if (c >= 0) {  // 1-byte char
    return 1;
  } else if ((c & 0xE0) == 0xC0) {  // 2-byte char
    return 2;
  } else if ((c & 0xF0) == 0xE0) {  // 3-byte char
    return 3;
  } else if ((c & 0xF8) == 0xF0) {  // 4-byte char
    return 4;
  }
  // invalid char
  return 0;
}

FORCE_INLINE
void set_error_for_invalid_utf(int64_t execution_context, char val) {
  char const* fmt = "unexpected byte \\%02hhx encountered while decoding utf8 string";
  int size = static_cast<int>(strlen(fmt)) + 64;
  char* error = reinterpret_cast<char*>(malloc(size));
  snprintf(error, size, fmt, (unsigned char)val);
  gdv_fn_context_set_error_msg(execution_context, error);
  free(error);
}

// Count the number of utf8 characters
// return 0 for invalid/incomplete input byte sequences
FORCE_INLINE
int32 utf8_length(int64 context, const char* data, int32 data_len) {
  int char_len = 0;
  int count = 0;
  for (int i = 0; i < data_len; i += char_len) {
    char_len = utf8_char_length(data[i]);
    if (char_len == 0 || i + char_len > data_len) {  // invalid byte or incomplete glyph
      set_error_for_invalid_utf(context, data[i]);
      return 0;
    }
    for (int j = 1; j < char_len; ++j) {
      if ((data[i + j] & 0xC0) != 0x80) {  // bytes following head-byte of glyph
        set_error_for_invalid_utf(context, data[i + j]);
        return 0;
      }
    }
    ++count;
  }
  return count;
}

// Get the byte position corresponding to a character position for a non-empty utf8
// sequence
FORCE_INLINE
int32 utf8_byte_pos(int64 context, const char* str, int32 str_len, int32 char_pos) {
  int char_len = 0;
  int byte_index = 0;
  for (int32 char_index = 0; char_index < char_pos && byte_index < str_len;
       char_index++) {
    char_len = utf8_char_length(str[byte_index]);
    if (char_len == 0 ||
        byte_index + char_len > str_len) {  // invalid byte or incomplete glyph
      set_error_for_invalid_utf(context, str[byte_index]);
      return -1;
    }
    byte_index += char_len;
  }
  if (byte_index >= str_len) {
    return -1;
  }
  return byte_index;
}

#define UTF8_LENGTH(NAME, TYPE)                               \
  FORCE_INLINE                                                \
  int32 NAME##_##TYPE(int64 context, TYPE in, int32 in_len) { \
    return utf8_length(context, in, in_len);                  \
  }

UTF8_LENGTH(char_length, utf8)
UTF8_LENGTH(length, utf8)
UTF8_LENGTH(lengthUtf8, binary)

// Convert a utf8 sequence to upper case.
// TODO : This handles only ascii characters.
FORCE_INLINE
const char* upper_utf8(int64 context, const char* data, int32 data_len,
                       int32_t* out_len) {
  if (data_len == 0) {
    *out_len = 0;
    return "";
  }

  char* ret = reinterpret_cast<char*>(gdv_fn_context_arena_malloc(context, data_len));
  if (ret == nullptr) {
    gdv_fn_context_set_error_msg(context, "Could not allocate memory for output string");
    *out_len = 0;
    return "";
  }
  for (int32 i = 0; i < data_len; ++i) {
    char cur = data[i];

    // 'A- - 'Z' : 0x41 - 0x5a
    // 'a' - 'z' : 0x61 - 0x7a
    if (cur >= 0x61 && cur <= 0x7a) {
      cur = static_cast<char>(cur - 0x20);
    }
    ret[i] = cur;
  }
  *out_len = data_len;
  return ret;
}

// Convert a utf8 sequence to lower case.
// TODO : This handles only ascii characters.
FORCE_INLINE
const char* lower_utf8(int64 context, const char* data, int32 data_len,
                       int32_t* out_len) {
  if (data_len == 0) {
    *out_len = 0;
    return "";
  }

  char* ret = reinterpret_cast<char*>(gdv_fn_context_arena_malloc(context, data_len));
  if (ret == nullptr) {
    gdv_fn_context_set_error_msg(context, "Could not allocate memory for output string");
    *out_len = 0;
    return "";
  }
  for (int32 i = 0; i < data_len; ++i) {
    char cur = data[i];

    // 'A' - 'Z' : 0x41 - 0x5a
    // 'a' - 'z' : 0x61 - 0x7a
    if (cur >= 0x41 && cur <= 0x5a) {
      cur = static_cast<char>(cur + 0x20);
    }
    ret[i] = cur;
  }
  *out_len = data_len;
  return ret;
}

// Reverse a utf8 sequence
FORCE_INLINE
const char* reverse_utf8(int64 context, const char* data, int32 data_len,
                         int32_t* out_len) {
  if (data_len == 0) {
    *out_len = 0;
    return "";
  }

  char* ret = reinterpret_cast<char*>(gdv_fn_context_arena_malloc(context, data_len));
  if (ret == nullptr) {
    gdv_fn_context_set_error_msg(context, "Could not allocate memory for output string");
    *out_len = 0;
    return "";
  }

  int32 char_len;
  for (int32 i = 0; i < data_len; i += char_len) {
    char_len = utf8_char_length(data[i]);

    if (char_len == 0 || i + char_len > data_len) {  // invalid byte or incomplete glyph
      set_error_for_invalid_utf(context, data[i]);
      *out_len = 0;
      return "";
    }

    for (int32 j = 0; j < char_len; ++j) {
      if (j > 0 && (data[i + j] & 0xC0) != 0x80) {  // bytes following head-byte of glyph
        set_error_for_invalid_utf(context, data[i + j]);
        *out_len = 0;
        return "";
      }
      ret[data_len - i - char_len + j] = data[i + j];
    }
  }
  *out_len = data_len;
  return ret;
}

// Truncates the string to given length
FORCE_INLINE
char* castVARCHAR_utf8_int64(int64 context, const char* data, int32 data_len,
                             int64_t out_len, int32_t* out_length) {
  // TODO: handle allocation failures
  int32_t len = data_len <= static_cast<int32_t>(out_len) || out_len == 0
                    ? data_len
                    : static_cast<int32_t>(out_len);
  char* ret = reinterpret_cast<char*>(gdv_fn_context_arena_malloc(context, len));
  memcpy(ret, data, len);
  *out_length = len;
  return ret;
}

#define IS_NULL(NAME, TYPE) \
  FORCE_INLINE              \
  bool NAME##_##TYPE(TYPE in, int32 len, boolean is_valid) { return !is_valid; }

VAR_LEN_TYPES(IS_NULL, isnull)

#undef IS_NULL

#define IS_NOT_NULL(NAME, TYPE) \
  FORCE_INLINE                  \
  bool NAME##_##TYPE(TYPE in, int32 len, boolean is_valid) { return is_valid; }

VAR_LEN_TYPES(IS_NOT_NULL, isnotnull)

#undef IS_NOT_NULL

/*
 We follow Oracle semantics for offset:
 - If position is positive, then the first glyph in the substring is determined by
 counting that many glyphs forward from the beginning of the input. (i.e., for position ==
 1 the first glyph in the substring will be identical to the first glyph in the input)

 - If position is negative, then the first glyph in the substring is determined by
 counting that many glyphs backward from the end of the input. (i.e., for position == -1
 the first glyph in the substring will be identical to the last glyph in the input)

 - If position is 0 then it is treated as 1.
 */
FORCE_INLINE
const char* substr_utf8_int64_int64(int64 context, const char* input, int32 in_data_len,
                                    int64 position, int64 substring_length,
                                    int32* out_data_len) {
  if (substring_length <= 0 || input == nullptr || in_data_len <= 0) {
    *out_data_len = 0;
    return "";
  }

  int64 in_glyphs_count = static_cast<int64>(utf8_length(context, input, in_data_len));

  // in_glyphs_count is zero if input has invalid glyphs
  if (in_glyphs_count == 0) {
    *out_data_len = 0;
    return "";
  }

  int64 from_glyph;  // from_glyph==0 indicates the first glyph of the input
  if (position > 0) {
    from_glyph = position - 1;
  } else if (position < 0) {
    from_glyph = in_glyphs_count + position;
  } else {
    from_glyph = 0;
  }

  if (from_glyph < 0 || from_glyph >= in_glyphs_count) {
    *out_data_len = 0;
    return "";
  }

  int64 out_glyphs_count = substring_length;
  if (substring_length > in_glyphs_count - from_glyph) {
    out_glyphs_count = in_glyphs_count - from_glyph;
  }

  int64 in_data_len64 = static_cast<int64>(in_data_len);
  int64 start_pos = 0;
  int64 end_pos = in_data_len64;

  int64 current_glyph = 0;
  int64 pos = 0;
  while (pos < in_data_len64) {
    if (current_glyph == from_glyph) {
      start_pos = pos;
    }
    pos += static_cast<int64>(utf8_char_length(input[pos]));
    if (current_glyph - from_glyph + 1 == out_glyphs_count) {
      end_pos = pos;
    }
    current_glyph++;
  }

  if (end_pos > in_data_len64 || end_pos > INT_MAX) {
    end_pos = in_data_len64;
  }

  *out_data_len = static_cast<int32>(end_pos - start_pos);
  char* ret =
      reinterpret_cast<char*>(gdv_fn_context_arena_malloc(context, *out_data_len));
  if (ret == nullptr) {
    gdv_fn_context_set_error_msg(context, "Could not allocate memory for output string");
    *out_data_len = 0;
    return "";
  }
  memcpy(ret, input + start_pos, *out_data_len);
  return ret;
}

FORCE_INLINE
const char* substr_utf8_int64(int64 context, const char* input, int32 in_len,
                              int64 offset64, int32* out_len) {
  return substr_utf8_int64_int64(context, input, in_len, offset64, in_len, out_len);
}

FORCE_INLINE
const char* concat_utf8_utf8(int64 context, const char* left, int32 left_len,
                             bool left_validity, const char* right, int32 right_len,
                             bool right_validity, int32* out_len) {
  if (!left_validity) {
    left_len = 0;
  }
  if (!right_validity) {
    right_len = 0;
  }
  return concatOperator_utf8_utf8(context, left, left_len, right, right_len, out_len);
}

FORCE_INLINE
const char* concatOperator_utf8_utf8(int64 context, const char* left, int32 left_len,
                                     const char* right, int32 right_len, int32* out_len) {
  *out_len = left_len + right_len;
  if (*out_len <= 0) {
    *out_len = 0;
    return "";
  }
  char* ret = reinterpret_cast<char*>(gdv_fn_context_arena_malloc(context, *out_len));
  if (ret == nullptr) {
    gdv_fn_context_set_error_msg(context, "Could not allocate memory for output string");
    *out_len = 0;
    return "";
  }
  memcpy(ret, left, left_len);
  memcpy(ret + left_len, right, right_len);
  return ret;
}

FORCE_INLINE
const char* convert_fromUTF8_binary(int64 context, const char* bin_in, int32 len,
                                    int32* out_len) {
  *out_len = len;
  char* ret = reinterpret_cast<char*>(gdv_fn_context_arena_malloc(context, *out_len));
  if (ret == nullptr) {
    gdv_fn_context_set_error_msg(context, "Could not allocate memory for output string");
    *out_len = 0;
    return "";
  }
  memcpy(ret, bin_in, *out_len);
  return ret;
}

// Search for a string within another string
FORCE_INLINE
int32 locate_utf8_utf8(int64 context, const char* sub_str, int32 sub_str_len,
                       const char* str, int32 str_len) {
  return locate_utf8_utf8_int32(context, sub_str, sub_str_len, str, str_len, 1);
}

// Search for a string within another string starting at position start-pos (1-indexed)
FORCE_INLINE
int32 locate_utf8_utf8_int32(int64 context, const char* sub_str, int32 sub_str_len,
                             const char* str, int32 str_len, int32 start_pos) {
  if (start_pos < 1) {
    gdv_fn_context_set_error_msg(context, "Start position must be greater than 0");
    return 0;
  }

  if (str_len == 0 || sub_str_len == 0) {
    return 0;
  }

  int32 byte_pos = utf8_byte_pos(context, str, str_len, start_pos - 1);
  if (byte_pos < 0) {
    return 0;
  }
  for (int32 i = byte_pos; i <= str_len - sub_str_len; ++i) {
    if (memcmp(str + i, sub_str, sub_str_len) == 0) {
      return utf8_length(context, str, i) + 1;
    }
  }
  return 0;
}

FORCE_INLINE
const char* replace_with_max_len_utf8_utf8_utf8(int64 context, const char* text,
                                                int32 text_len, const char* from_str,
                                                int32 from_str_len, const char* to_str,
                                                int32 to_str_len, int32 max_length,
                                                int32* out_len) {
  // if from_str is empty or its length exceeds that of original string,
  // return the original string
  if (from_str_len <= 0 || from_str_len > text_len) {
    *out_len = text_len;
    return text;
  }

  bool found = false;
  int32 text_index = 0;
  char* out;
  int32 out_index = 0;
  int32 last_match_index =
      0;  // defer copying string from last_match_index till next match is found

  for (; text_index <= text_len - from_str_len;) {
    if (memcmp(text + text_index, from_str, from_str_len) == 0) {
      if (out_index + text_index - last_match_index + to_str_len > max_length) {
        gdv_fn_context_set_error_msg(context, "Buffer overflow for output string");
        *out_len = 0;
        return "";
      }
      if (!found) {
        // found match for first time
        out = reinterpret_cast<char*>(gdv_fn_context_arena_malloc(context, max_length));
        if (out == nullptr) {
          gdv_fn_context_set_error_msg(context,
                                       "Could not allocate memory for output string");
          *out_len = 0;
          return "";
        }
        found = true;
      }
      // first copy the part deferred till now
      memcpy(out + out_index, text + last_match_index, (text_index - last_match_index));
      out_index += text_index - last_match_index;
      // then copy the target string
      memcpy(out + out_index, to_str, to_str_len);
      out_index += to_str_len;

      text_index += from_str_len;
      last_match_index = text_index;
    } else {
      text_index++;
    }
  }

  if (!found) {
    *out_len = text_len;
    return text;
  }

  if (out_index + text_len - last_match_index > max_length) {
    gdv_fn_context_set_error_msg(context, "Buffer overflow for output string");
    *out_len = 0;
    return "";
  }
  memcpy(out + out_index, text + last_match_index, text_len - last_match_index);
  out_index += text_len - last_match_index;
  *out_len = out_index;
  return out;
}

FORCE_INLINE
const char* replace_utf8_utf8_utf8(int64 context, const char* text, int32 text_len,
                                   const char* from_str, int32 from_str_len,
                                   const char* to_str, int32 to_str_len, int32* out_len) {
  return replace_with_max_len_utf8_utf8_utf8(context, text, text_len, from_str,
                                             from_str_len, to_str, to_str_len, 65535,
                                             out_len);
}

}  // extern "C"
