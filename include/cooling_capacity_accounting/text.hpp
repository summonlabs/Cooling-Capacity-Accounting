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

#ifndef COOLING_CAPACITY_ACCOUNTING_TEXT_HPP
#define COOLING_CAPACITY_ACCOUNTING_TEXT_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cooling_capacity_accounting/errors.hpp"
#include "cooling_capacity_accounting/limits.hpp"

namespace cooling_capacity_accounting {

/// Strict UTF-8 validation: rejects overlong encodings, UTF-16 surrogates,
/// code points above U+10FFFF and embedded NUL bytes.
[[nodiscard]] bool is_valid_utf8(std::string_view text) noexcept;

/// A bounded, validated identifier: 1..64 characters from
/// [A-Za-z0-9._:-], case sensitive, no NUL and no whitespace.
class Identifier {
 public:
  Identifier() = default;

  [[nodiscard]] static Result<Identifier> parse(std::string_view text);
  /// Builds an identifier from text the caller has already validated.
  [[nodiscard]] static Identifier from_validated(std::string text) noexcept;

  [[nodiscard]] bool empty() const noexcept { return text_.empty(); }
  [[nodiscard]] const std::string& str() const noexcept { return text_; }
  [[nodiscard]] std::string_view view() const noexcept { return text_; }

  friend bool operator==(const Identifier& lhs, const Identifier& rhs) noexcept {
    return lhs.text_ == rhs.text_;
  }
  friend bool operator!=(const Identifier& lhs, const Identifier& rhs) noexcept {
    return !(lhs == rhs);
  }
  friend bool operator<(const Identifier& lhs, const Identifier& rhs) noexcept {
    return lhs.text_ < rhs.text_;
  }

 private:
  std::string text_;
};

/// Hash for Identifier, so identifiers can key an unordered container without
/// ever making container iteration order observable.
struct IdentifierHash {
  [[nodiscard]] std::size_t operator()(const Identifier& id) const noexcept;
};

/// Free text: valid UTF-8, at most kMaxTextLength bytes, no NUL and no control
/// characters other than tab.
class BoundedText {
 public:
  BoundedText() = default;

  [[nodiscard]] static Result<BoundedText> parse(std::string_view text);
  [[nodiscard]] static Result<BoundedText> parse(std::string_view text,
                                                 std::size_t max_length);
  [[nodiscard]] static BoundedText from_validated(std::string text) noexcept;

  [[nodiscard]] bool empty() const noexcept { return text_.empty(); }
  [[nodiscard]] const std::string& str() const noexcept { return text_; }
  [[nodiscard]] std::string_view view() const noexcept { return text_; }

  friend bool operator==(const BoundedText& lhs, const BoundedText& rhs) noexcept {
    return lhs.text_ == rhs.text_;
  }
  friend bool operator!=(const BoundedText& lhs, const BoundedText& rhs) noexcept {
    return !(lhs == rhs);
  }
  friend bool operator<(const BoundedText& lhs, const BoundedText& rhs) noexcept {
    return lhs.text_ < rhs.text_;
  }

 private:
  std::string text_;
};

/// An opaque reference to a document owned elsewhere: a vendor datasheet, a
/// commissioning report, a BMS export. This library never opens it.
class DocumentRef {
 public:
  DocumentRef() = default;

  [[nodiscard]] static Result<DocumentRef> parse(std::string_view text);
  /// Builds a reference from text the caller has already validated.
  [[nodiscard]] static DocumentRef from_validated(std::string text) noexcept;

  [[nodiscard]] bool empty() const noexcept { return text_.empty(); }
  [[nodiscard]] const std::string& str() const noexcept { return text_; }
  [[nodiscard]] std::string_view view() const noexcept { return text_; }

  friend bool operator==(const DocumentRef& lhs, const DocumentRef& rhs) noexcept {
    return lhs.text_ == rhs.text_;
  }
  friend bool operator!=(const DocumentRef& lhs, const DocumentRef& rhs) noexcept {
    return !(lhs == rhs);
  }
  friend bool operator<(const DocumentRef& lhs, const DocumentRef& rhs) noexcept {
    return lhs.text_ < rhs.text_;
  }

 private:
  std::string text_;
};

/// True when text is a syntactically valid identifier.
[[nodiscard]] bool is_valid_identifier(std::string_view text) noexcept;

/// Strict signed 64-bit decimal: optional '-', no '+', no leading zeros other
/// than "0" itself, no surrounding whitespace, no separators.
[[nodiscard]] Result<std::int64_t> parse_int64(std::string_view text);
/// Strict unsigned 64-bit decimal with the same rules.
[[nodiscard]] Result<std::uint64_t> parse_uint64(std::string_view text);
/// Strict unsigned 32-bit decimal with the same rules.
[[nodiscard]] Result<std::uint32_t> parse_uint32(std::string_view text);

/// Canonical decimal rendering of a signed 64-bit value.
[[nodiscard]] std::string to_decimal(std::int64_t value);
/// Canonical decimal rendering of an unsigned 64-bit value.
[[nodiscard]] std::string to_decimal(std::uint64_t value);

/// ASCII-only lower casing; never touches bytes above 0x7F.
[[nodiscard]] std::string to_lower_ascii(std::string_view text);
/// True when the byte is an ASCII letter or digit.
[[nodiscard]] bool is_ascii_alphanumeric(char byte) noexcept;
/// True when the byte is an ASCII decimal digit.
[[nodiscard]] bool is_ascii_digit(char byte) noexcept;

/// Splits a comma-separated list into its fields. Empty fields are preserved so
/// that a malformed list is rejected by the caller rather than silently
/// shrinking.
[[nodiscard]] std::vector<std::string> split_list(std::string_view text);

/// Replaces every character outside [0x20, 0x7E] with '?', so that a report can
/// never emit a terminal control sequence.
[[nodiscard]] std::string to_printable_ascii(std::string_view text);

}  // namespace cooling_capacity_accounting

#endif  // COOLING_CAPACITY_ACCOUNTING_TEXT_HPP
