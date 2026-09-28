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

#ifndef COOLING_CAPACITY_ACCOUNTING_SRC_BYTE_IO_HPP
#define COOLING_CAPACITY_ACCOUNTING_SRC_BYTE_IO_HPP

// Internal, fixed-width, big-endian byte codec shared by the canonical record
// encoding and the durable store format. Every read is bounds checked before it
// produces a value, and every length is validated against the caller's bound
// before it is used as an allocation size.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "cooling_capacity_accounting/errors.hpp"
#include "cooling_capacity_accounting/limits.hpp"
#include "cooling_capacity_accounting/text.hpp"

namespace cooling_capacity_accounting {
namespace internal {

class ByteWriter {
 public:
  void write_u8(std::uint8_t value) { bytes_.push_back(value); }

  void write_u16(std::uint16_t value) {
    write_u8(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    write_u8(static_cast<std::uint8_t>(value & 0xFFU));
  }

  void write_u32(std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
      write_u8(static_cast<std::uint8_t>((value >> static_cast<unsigned>(shift)) & 0xFFU));
    }
  }

  void write_u64(std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
      write_u8(static_cast<std::uint8_t>((value >> static_cast<unsigned>(shift)) & 0xFFU));
    }
  }

  void write_i64(std::int64_t value) { write_u64(static_cast<std::uint64_t>(value)); }

  void write_bool(bool value) { write_u8(value ? 1U : 0U); }

  void write_raw(const void* data, std::size_t size) {
    const auto* first = static_cast<const std::uint8_t*>(data);
    bytes_.insert(bytes_.end(), first, first + size);
  }

  void write_text(std::string_view text) {
    write_u32(static_cast<std::uint32_t>(text.size()));
    write_raw(text.data(), text.size());
  }

  void write_optional_text(bool present, std::string_view text) {
    write_bool(present);
    if (present) {
      write_text(text);
    }
  }

  void overwrite_u64(std::size_t offset, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
      bytes_[offset + index] = static_cast<std::uint8_t>(
          (value >> ((7U - index) * 8U)) & 0xFFU);
    }
  }

  [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept {
    return bytes_;
  }
  [[nodiscard]] std::vector<std::uint8_t>& bytes() noexcept { return bytes_; }
  [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }

 private:
  std::vector<std::uint8_t> bytes_;
};

class ByteReader {
 public:
  ByteReader(const void* data, std::size_t size)
      : data_(static_cast<const std::uint8_t*>(data)), size_(size) {}

  [[nodiscard]] std::size_t remaining() const noexcept { return size_ - offset_; }
  [[nodiscard]] bool empty() const noexcept { return offset_ == size_; }

  [[nodiscard]] Result<std::uint8_t> read_u8() {
    if (remaining() < 1U) {
      return truncated();
    }
    return data_[offset_++];
  }

  [[nodiscard]] Result<std::uint16_t> read_u16() {
    CCA_TRY_ASSIGN(high, read_u8());
    CCA_TRY_ASSIGN(low, read_u8());
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(high) << 8U) | low);
  }

  [[nodiscard]] Result<std::uint32_t> read_u32() {
    std::uint32_t value = 0;
    for (int index = 0; index < 4; ++index) {
      CCA_TRY_ASSIGN(byte, read_u8());
      value = (value << 8U) | byte;
    }
    return value;
  }

  [[nodiscard]] Result<std::uint64_t> read_u64() {
    std::uint64_t value = 0;
    for (int index = 0; index < 8; ++index) {
      CCA_TRY_ASSIGN(byte, read_u8());
      value = (value << 8U) | byte;
    }
    return value;
  }

  [[nodiscard]] Result<std::int64_t> read_i64() {
    CCA_TRY_ASSIGN(value, read_u64());
    return static_cast<std::int64_t>(value);
  }

  [[nodiscard]] Result<bool> read_bool() {
    CCA_TRY_ASSIGN(value, read_u8());
    if (value > 1U) {
      return Error::of(ErrorCode::MalformedRecord, "a boolean field is neither 0 nor 1")
          .with_detail("value " + to_decimal(static_cast<std::uint64_t>(value)));
    }
    return value == 1U;
  }

  [[nodiscard]] Result<std::vector<std::uint8_t>> read_raw(std::size_t size) {
    if (remaining() < size) {
      return truncated();
    }
    std::vector<std::uint8_t> bytes(data_ + offset_, data_ + offset_ + size);
    offset_ += size;
    return bytes;
  }

  [[nodiscard]] Result<std::string> read_text(std::size_t max_length) {
    CCA_TRY_ASSIGN(length, read_u32());
    if (static_cast<std::size_t>(length) > max_length) {
      return Error::of(ErrorCode::ItemTooLarge,
                       "a length-prefixed field exceeds its bound")
          .with_detail("length " + to_decimal(static_cast<std::uint64_t>(length)) +
                       " exceeds " +
                       to_decimal(static_cast<std::uint64_t>(max_length)));
    }
    if (remaining() < static_cast<std::size_t>(length)) {
      return truncated();
    }
    std::string text(reinterpret_cast<const char*>(data_ + offset_),
                     static_cast<std::size_t>(length));
    offset_ += static_cast<std::size_t>(length);
    return text;
  }

  /// Reads an optional text field. A field marked present must carry at least
  /// one byte, because the encoder never writes an empty present field: a
  /// decoder that accepted one would break the canonical round trip.
  [[nodiscard]] Result<std::string> read_optional_text(std::size_t max_length) {
    CCA_TRY_ASSIGN(present, read_bool());
    if (!present) {
      return std::string();
    }
    CCA_TRY_ASSIGN(text, read_text(max_length));
    if (text.empty()) {
      return Error::of(ErrorCode::MalformedRecord,
                       "an optional text field is marked present but is empty");
    }
    return text;
  }

  /// Reads a record count and checks it against a bound the caller configured
  /// through Limits, so an over-bound count is LimitExceeded rather than the
  /// format-level TooManyItems a compile-time bound would produce.
  [[nodiscard]] Result<std::uint32_t> read_count(std::size_t bound,
                                                 const char* what) {
    CCA_TRY_ASSIGN(count, read_u32());
    if (static_cast<std::size_t>(count) > bound) {
      return Error::of(ErrorCode::LimitExceeded,
                       std::string("the record declares more ") + what +
                           " than the configured bound allows")
          .with_detail("count " + to_decimal(static_cast<std::uint64_t>(count)) +
                       " exceeds " +
                       to_decimal(static_cast<std::uint64_t>(bound)));
    }
    return count;
  }

  [[nodiscard]] Error truncated() const {
    return Error::of(ErrorCode::TruncatedInput,
                     "the input ends before the record is complete")
        .with_detail("offset " + to_decimal(static_cast<std::uint64_t>(offset_)) +
                     " of " + to_decimal(static_cast<std::uint64_t>(size_)));
  }

 private:
  const std::uint8_t* data_;
  std::size_t size_;
  std::size_t offset_ = 0;
};

/// Decodes an enumerated field, refusing a value outside its definition.
template <typename Enum, typename Predicate>
[[nodiscard]] Result<Enum> decode_enum(std::uint32_t raw, Predicate is_defined,
                                       const char* what) {
  const auto value = static_cast<Enum>(raw);
  if (!is_defined(value)) {
    return Error::of(ErrorCode::ImpossibleEnumValue,
                     std::string("the ") + what + " is outside its definition")
        .with_detail("raw value " + to_decimal(static_cast<std::uint64_t>(raw)));
  }
  return value;
}

}  // namespace internal
}  // namespace cooling_capacity_accounting

#endif  // COOLING_CAPACITY_ACCOUNTING_SRC_BYTE_IO_HPP
