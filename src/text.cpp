// Copyright 2026 Summon Software Labs
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cooling_capacity_accounting/text.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace cooling_capacity_accounting {
namespace {

constexpr char kAsciiLowerFirst = 'a';
constexpr char kAsciiLowerLast = 'z';
constexpr char kAsciiUpperFirst = 'A';
constexpr char kAsciiUpperLast = 'Z';
constexpr char kAsciiDigitFirst = '0';
constexpr char kAsciiDigitLast = '9';

[[nodiscard]] bool is_identifier_byte(char byte) noexcept {
  if (byte >= kAsciiLowerFirst && byte <= kAsciiLowerLast) {
    return true;
  }
  if (byte >= kAsciiUpperFirst && byte <= kAsciiUpperLast) {
    return true;
  }
  if (byte >= kAsciiDigitFirst && byte <= kAsciiDigitLast) {
    return true;
  }
  return byte == '.' || byte == '_' || byte == ':' || byte == '-';
}

[[nodiscard]] bool is_reference_byte(char byte) noexcept {
  if (is_identifier_byte(byte)) {
    return true;
  }
  return byte == '/' || byte == '#' || byte == '?' || byte == '=' || byte == '&' ||
         byte == '%' || byte == '+' || byte == '~' || byte == '@' || byte == ',' ||
         byte == ';' || byte == '!';
}

/// Decodes one UTF-8 sequence starting at index. Returns the number of bytes
/// consumed, or 0 when the sequence is invalid.
[[nodiscard]] std::size_t decode_utf8(std::string_view text, std::size_t index,
                                      std::uint32_t& code_point) noexcept {
  const std::size_t size = text.size();
  const auto byte_at = [&text](std::size_t position) {
    return static_cast<std::uint8_t>(text[position]);
  };
  const std::uint8_t first = byte_at(index);
  if (first < 0x80U) {
    code_point = first;
    return 1;
  }
  std::size_t length = 0;
  std::uint32_t value = 0;
  std::uint32_t minimum = 0;
  if ((first & 0xE0U) == 0xC0U) {
    length = 2;
    value = first & 0x1FU;
    minimum = 0x80U;
  } else if ((first & 0xF0U) == 0xE0U) {
    length = 3;
    value = first & 0x0FU;
    minimum = 0x800U;
  } else if ((first & 0xF8U) == 0xF0U) {
    length = 4;
    value = first & 0x07U;
    minimum = 0x10000U;
  } else {
    return 0;
  }
  if (index + length > size) {
    return 0;
  }
  for (std::size_t offset = 1; offset < length; ++offset) {
    const std::uint8_t continuation = byte_at(index + offset);
    if ((continuation & 0xC0U) != 0x80U) {
      return 0;
    }
    value = (value << 6U) | (continuation & 0x3FU);
  }
  if (value < minimum) {
    return 0;  // overlong encoding
  }
  if (value >= 0xD800U && value <= 0xDFFFU) {
    return 0;  // UTF-16 surrogate
  }
  if (value > 0x10FFFFU) {
    return 0;
  }
  code_point = value;
  return length;
}

[[nodiscard]] Result<std::string> parse_decimal_digits(std::string_view text,
                                                       bool allow_sign) {
  if (text.empty()) {
    return Error::of(ErrorCode::InvalidNumber, "empty decimal");
  }
  std::size_t index = 0;
  bool negative = false;
  if (text[0] == '-') {
    if (!allow_sign) {
      return Error::of(ErrorCode::InvalidNumber, "a negative value is not accepted here")
          .with_subject(std::string(text));
    }
    negative = true;
    index = 1;
    if (index == text.size()) {
      return Error::of(ErrorCode::InvalidNumber, "a sign with no digits")
          .with_subject(std::string(text));
    }
  }
  if (text[index] == '+') {
    return Error::of(ErrorCode::InvalidNumber, "an explicit plus sign is not canonical")
        .with_subject(std::string(text));
  }
  for (std::size_t position = index; position < text.size(); ++position) {
    if (!is_ascii_digit(text[position])) {
      return Error::of(ErrorCode::InvalidNumber, "a decimal must contain digits only")
          .with_subject(std::string(text));
    }
  }
  const std::string_view digits = text.substr(index);
  if (digits.size() > 1 && digits[0] == '0') {
    return Error::of(ErrorCode::InvalidNumber, "a leading zero is not canonical")
        .with_subject(std::string(text));
  }
  if (negative) {
    bool all_zero = true;
    for (const char digit : digits) {
      if (digit != '0') {
        all_zero = false;
        break;
      }
    }
    if (all_zero) {
      // "-0" and "0" are the same value but only one of them is canonical.
      return Error::of(ErrorCode::InvalidNumber, "a negative zero is not canonical")
          .with_subject(std::string(text));
    }
  }
  std::string canonical = negative ? "-" + std::string(digits) : std::string(digits);
  return canonical;
}

}  // namespace

bool is_ascii_digit(char byte) noexcept {
  return byte >= kAsciiDigitFirst && byte <= kAsciiDigitLast;
}

bool is_ascii_alphanumeric(char byte) noexcept {
  return is_ascii_digit(byte) ||
         (byte >= kAsciiLowerFirst && byte <= kAsciiLowerLast) ||
         (byte >= kAsciiUpperFirst && byte <= kAsciiUpperLast);
}

bool is_valid_utf8(std::string_view text) noexcept {
  std::size_t index = 0;
  while (index < text.size()) {
    if (text[index] == '\0') {
      return false;
    }
    std::uint32_t code_point = 0;
    const std::size_t consumed = decode_utf8(text, index, code_point);
    if (consumed == 0) {
      return false;
    }
    index += consumed;
  }
  return true;
}

bool is_valid_identifier(std::string_view text) noexcept {
  if (text.empty() || text.size() > kMaxIdentifierLength) {
    return false;
  }
  for (const char byte : text) {
    if (!is_identifier_byte(byte)) {
      return false;
    }
  }
  return true;
}

Result<Identifier> Identifier::parse(std::string_view text) {
  if (text.empty()) {
    return Error::of(ErrorCode::InvalidIdentifier, "an identifier may not be empty");
  }
  if (text.size() > kMaxIdentifierLength) {
    return Error::of(ErrorCode::InvalidIdentifier, "identifier is longer than the limit")
        .with_subject(std::string(text.substr(0, kMaxIdentifierLength)))
        .with_detail("length " + to_decimal(static_cast<std::uint64_t>(text.size())) +
                     " exceeds " +
                     to_decimal(static_cast<std::uint64_t>(kMaxIdentifierLength)));
  }
  for (const char byte : text) {
    if (!is_identifier_byte(byte)) {
      return Error::of(ErrorCode::InvalidIdentifier,
                       "identifiers accept A-Z a-z 0-9 . _ : - only")
          .with_subject(std::string(text));
    }
  }
  return Identifier::from_validated(std::string(text));
}

Identifier Identifier::from_validated(std::string text) noexcept {
  Identifier result;
  result.text_ = std::move(text);
  return result;
}

std::size_t IdentifierHash::operator()(const Identifier& id) const noexcept {
  // FNV-1a, so the hash is identical on every platform the library supports.
  std::uint64_t hash = 1469598103934665603ULL;
  for (const char byte : id.view()) {
    hash ^= static_cast<std::uint8_t>(byte);
    hash *= 1099511628211ULL;
  }
  return static_cast<std::size_t>(hash);
}

Result<BoundedText> BoundedText::parse(std::string_view text) {
  return parse(text, kMaxTextLength);
}

Result<BoundedText> BoundedText::parse(std::string_view text, std::size_t max_length) {
  if (text.size() > max_length) {
    return Error::of(ErrorCode::TooManyItems, "text is longer than the limit")
        .with_detail("length " + to_decimal(static_cast<std::uint64_t>(text.size())) +
                     " exceeds " + to_decimal(static_cast<std::uint64_t>(max_length)));
  }
  if (!is_valid_utf8(text)) {
    return Error::of(ErrorCode::InvalidUtf8, "text is not valid UTF-8");
  }
  for (const char byte : text) {
    const auto value = static_cast<unsigned char>(byte);
    if (value < 0x20U && byte != '\t') {
      return Error::of(ErrorCode::InvalidText, "control characters are not accepted")
          .with_detail("byte value " + to_decimal(static_cast<std::uint64_t>(value)));
    }
    if (value == 0x7FU) {
      return Error::of(ErrorCode::InvalidText, "the delete character is not accepted");
    }
  }
  return BoundedText::from_validated(std::string(text));
}

BoundedText BoundedText::from_validated(std::string text) noexcept {
  BoundedText result;
  result.text_ = std::move(text);
  return result;
}

Result<DocumentRef> DocumentRef::parse(std::string_view text) {
  if (text.empty()) {
    return Error::of(ErrorCode::InvalidText, "a document reference may not be empty");
  }
  if (text.size() > kMaxReferenceLength) {
    return Error::of(ErrorCode::TooManyItems,
                     "a document reference is longer than the limit")
        .with_detail("length " + to_decimal(static_cast<std::uint64_t>(text.size())) +
                     " exceeds " +
                     to_decimal(static_cast<std::uint64_t>(kMaxReferenceLength)));
  }
  for (const char byte : text) {
    if (!is_reference_byte(byte)) {
      return Error::of(ErrorCode::InvalidText,
                       "a document reference accepts identifier characters and / # ? = & % + ~ @ , ; ! only")
          .with_subject(std::string(text));
    }
  }
  DocumentRef result;
  result.text_ = std::string(text);
  return result;
}

DocumentRef DocumentRef::from_validated(std::string text) noexcept {
  DocumentRef result;
  result.text_ = std::move(text);
  return result;
}

Result<std::int64_t> parse_int64(std::string_view text) {
  CCA_TRY_ASSIGN(canonical, parse_decimal_digits(text, true));
  try {
    std::size_t consumed = 0;
    const std::int64_t value = std::stoll(canonical, &consumed);
    if (consumed != canonical.size()) {
      return Error::of(ErrorCode::InvalidNumber, "trailing characters after a decimal")
          .with_subject(std::string(text));
    }
    return value;
  } catch (const std::out_of_range&) {
    return Error::of(ErrorCode::OutOfRange, "decimal does not fit a signed 64-bit value")
        .with_subject(std::string(text));
  } catch (const std::invalid_argument&) {
    return Error::of(ErrorCode::InvalidNumber, "not a decimal")
        .with_subject(std::string(text));
  }
}

Result<std::uint64_t> parse_uint64(std::string_view text) {
  CCA_TRY_ASSIGN(canonical, parse_decimal_digits(text, false));
  try {
    std::size_t consumed = 0;
    const unsigned long long value = std::stoull(canonical, &consumed);
    if (consumed != canonical.size()) {
      return Error::of(ErrorCode::InvalidNumber, "trailing characters after a decimal")
          .with_subject(std::string(text));
    }
    return static_cast<std::uint64_t>(value);
  } catch (const std::out_of_range&) {
    return Error::of(ErrorCode::OutOfRange,
                     "decimal does not fit an unsigned 64-bit value")
        .with_subject(std::string(text));
  } catch (const std::invalid_argument&) {
    return Error::of(ErrorCode::InvalidNumber, "not a decimal")
        .with_subject(std::string(text));
  }
}

Result<std::uint32_t> parse_uint32(std::string_view text) {
  CCA_TRY_ASSIGN(value, parse_uint64(text));
  if (value > 0xFFFFFFFFULL) {
    return Error::of(ErrorCode::OutOfRange,
                     "decimal does not fit an unsigned 32-bit value")
        .with_subject(std::string(text));
  }
  return static_cast<std::uint32_t>(value);
}

std::string to_decimal(std::int64_t value) {
  return std::to_string(value);
}

std::string to_decimal(std::uint64_t value) {
  return std::to_string(value);
}

std::string to_lower_ascii(std::string_view text) {
  std::string result;
  result.reserve(text.size());
  for (const char byte : text) {
    if (byte >= kAsciiUpperFirst && byte <= kAsciiUpperLast) {
      result.push_back(static_cast<char>(byte - kAsciiUpperFirst + kAsciiLowerFirst));
    } else {
      result.push_back(byte);
    }
  }
  return result;
}

std::vector<std::string> split_list(std::string_view text) {
  std::vector<std::string> fields;
  std::string current;
  for (const char byte : text) {
    if (byte == ',') {
      fields.push_back(current);
      current.clear();
      continue;
    }
    current.push_back(byte);
  }
  fields.push_back(current);
  return fields;
}

std::string to_printable_ascii(std::string_view text) {
  std::string result;
  result.reserve(text.size());
  for (const char byte : text) {
    const auto value = static_cast<unsigned char>(byte);
    if (value < 0x20U || value > 0x7EU) {
      result.push_back('?');
    } else {
      result.push_back(byte);
    }
  }
  return result;
}

}  // namespace cooling_capacity_accounting
