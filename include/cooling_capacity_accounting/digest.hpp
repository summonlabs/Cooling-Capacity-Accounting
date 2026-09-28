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

#ifndef COOLING_CAPACITY_ACCOUNTING_DIGEST_HPP
#define COOLING_CAPACITY_ACCOUNTING_DIGEST_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "cooling_capacity_accounting/errors.hpp"

namespace cooling_capacity_accounting {

/// A SHA-256 digest (FIPS 180-4). It is the integrity and identity primitive of
/// every canonical record, published generation and persisted file.
class Digest {
 public:
  static constexpr std::size_t kSize = 32;

  Digest() = default;

  [[nodiscard]] static Digest of_bytes(const void* data, std::size_t size);
  [[nodiscard]] static Digest from_bytes(
      const std::array<std::uint8_t, kSize>& bytes) noexcept;
  [[nodiscard]] static Digest of_text(std::string_view text);
  [[nodiscard]] static Digest zero() noexcept { return Digest{}; }
  /// Parses exactly 64 hexadecimal characters.
  [[nodiscard]] static Result<Digest> parse_hex(std::string_view hex);

  [[nodiscard]] const std::array<std::uint8_t, kSize>& bytes() const noexcept {
    return bytes_;
  }
  [[nodiscard]] std::string to_hex() const;
  [[nodiscard]] bool is_zero() const noexcept;

  friend bool operator==(const Digest& lhs, const Digest& rhs) noexcept {
    return lhs.bytes_ == rhs.bytes_;
  }
  friend bool operator!=(const Digest& lhs, const Digest& rhs) noexcept {
    return !(lhs == rhs);
  }
  friend bool operator<(const Digest& lhs, const Digest& rhs) noexcept {
    return lhs.bytes_ < rhs.bytes_;
  }

 private:
  friend class Sha256;

  std::array<std::uint8_t, kSize> bytes_{};
};

/// Streaming SHA-256.
class Sha256 {
 public:
  Sha256() { reset(); }

  void reset() noexcept;
  void update(const void* data, std::size_t size) noexcept;
  void update(std::string_view text) noexcept { update(text.data(), text.size()); }
  /// Finalises the digest. The hasher must be reset before it is reused.
  [[nodiscard]] Digest finish() noexcept;

 private:
  void compress(const std::uint8_t* block) noexcept;

  std::array<std::uint32_t, 8> state_{};
  std::array<std::uint8_t, 64> buffer_{};
  std::size_t buffered_ = 0;
  std::uint64_t total_bytes_ = 0;
};

/// Length-prefixed field mixing. Every field is written as an 8-byte big-endian
/// length followed by its bytes, so no two distinct field sequences can produce
/// the same digest input.
class DigestBuilder {
 public:
  DigestBuilder() = default;

  void add_bytes(const void* data, std::size_t size);
  void add_text(std::string_view text);
  void add_u8(std::uint8_t value);
  void add_u32(std::uint32_t value);
  void add_u64(std::uint64_t value);
  void add_i64(std::int64_t value);
  void add_bool(bool value);
  /// A named field: the tag is mixed before the value.
  void add_field(std::string_view tag, std::string_view value);
  void add_field_u64(std::string_view tag, std::uint64_t value);
  void add_field_i64(std::string_view tag, std::int64_t value);
  /// Separates sections so that truncating one section changes the digest.
  void add_section(std::string_view name);

  [[nodiscard]] Digest finish() const;

 private:
  Sha256 hasher_;
};

}  // namespace cooling_capacity_accounting

#endif  // COOLING_CAPACITY_ACCOUNTING_DIGEST_HPP
