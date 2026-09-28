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

#include "cooling_capacity_accounting/digest.hpp"

#include <cstring>
#include <string>

#include "cooling_capacity_accounting/text.hpp"

namespace cooling_capacity_accounting {
namespace {

constexpr std::uint32_t kRoundConstants[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
    0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
    0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
    0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

constexpr char kHexDigits[] = "0123456789abcdef";

[[nodiscard]] constexpr std::uint32_t rotate_right(std::uint32_t value,
                                                   unsigned count) noexcept {
  return (value >> count) | (value << (32U - count));
}

}  // namespace

Digest Digest::of_bytes(const void* data, std::size_t size) {
  Sha256 hasher;
  hasher.update(data, size);
  return hasher.finish();
}

Digest Digest::from_bytes(const std::array<std::uint8_t, kSize>& bytes) noexcept {
  Digest digest;
  digest.bytes_ = bytes;
  return digest;
}

Digest Digest::of_text(std::string_view text) {
  return of_bytes(text.data(), text.size());
}

Result<Digest> Digest::parse_hex(std::string_view hex) {
  if (hex.size() != kSize * 2U) {
    return Error::of(ErrorCode::InvalidDigestText,
                     "a digest is exactly 64 hexadecimal characters")
        .with_detail("length " + to_decimal(static_cast<std::uint64_t>(hex.size())));
  }
  Digest digest;
  for (std::size_t index = 0; index < kSize; ++index) {
    const char high = hex[index * 2U];
    const char low = hex[index * 2U + 1U];
    const auto nibble = [](char byte) -> int {
      if (byte >= '0' && byte <= '9') {
        return byte - '0';
      }
      if (byte >= 'a' && byte <= 'f') {
        return byte - 'a' + 10;
      }
      if (byte >= 'A' && byte <= 'F') {
        return byte - 'A' + 10;
      }
      return -1;
    };
    const int high_value = nibble(high);
    const int low_value = nibble(low);
    if (high_value < 0 || low_value < 0) {
      return Error::of(ErrorCode::InvalidDigestText, "a digest is hexadecimal only")
          .with_subject(std::string(hex));
    }
    digest.bytes_[index] =
        static_cast<std::uint8_t>((high_value << 4) | low_value);
  }
  return digest;
}

std::string Digest::to_hex() const {
  std::string text;
  text.reserve(kSize * 2U);
  for (const std::uint8_t byte : bytes_) {
    text.push_back(kHexDigits[(byte >> 4U) & 0x0FU]);
    text.push_back(kHexDigits[byte & 0x0FU]);
  }
  return text;
}

bool Digest::is_zero() const noexcept {
  for (const std::uint8_t byte : bytes_) {
    if (byte != 0) {
      return false;
    }
  }
  return true;
}

void Sha256::reset() noexcept {
  state_ = {0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
            0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
  buffer_.fill(0);
  buffered_ = 0;
  total_bytes_ = 0;
}

void Sha256::compress(const std::uint8_t* block) noexcept {
  std::uint32_t schedule[64];
  for (std::size_t index = 0; index < 16; ++index) {
    schedule[index] = (static_cast<std::uint32_t>(block[index * 4U]) << 24U) |
                      (static_cast<std::uint32_t>(block[index * 4U + 1U]) << 16U) |
                      (static_cast<std::uint32_t>(block[index * 4U + 2U]) << 8U) |
                      static_cast<std::uint32_t>(block[index * 4U + 3U]);
  }
  for (std::size_t index = 16; index < 64; ++index) {
    const std::uint32_t s0 = rotate_right(schedule[index - 15], 7) ^
                             rotate_right(schedule[index - 15], 18) ^
                             (schedule[index - 15] >> 3U);
    const std::uint32_t s1 = rotate_right(schedule[index - 2], 17) ^
                             rotate_right(schedule[index - 2], 19) ^
                             (schedule[index - 2] >> 10U);
    schedule[index] = schedule[index - 16] + s0 + schedule[index - 7] + s1;
  }

  std::uint32_t a = state_[0];
  std::uint32_t b = state_[1];
  std::uint32_t c = state_[2];
  std::uint32_t d = state_[3];
  std::uint32_t e = state_[4];
  std::uint32_t f = state_[5];
  std::uint32_t g = state_[6];
  std::uint32_t h = state_[7];

  for (std::size_t index = 0; index < 64; ++index) {
    const std::uint32_t sigma1 =
        rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
    const std::uint32_t choice = (e & f) ^ ((~e) & g);
    const std::uint32_t temp1 =
        h + sigma1 + choice + kRoundConstants[index] + schedule[index];
    const std::uint32_t sigma0 =
        rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
    const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    const std::uint32_t temp2 = sigma0 + majority;
    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }

  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
  state_[4] += e;
  state_[5] += f;
  state_[6] += g;
  state_[7] += h;
}

void Sha256::update(const void* data, std::size_t size) noexcept {
  const auto* bytes = static_cast<const std::uint8_t*>(data);
  total_bytes_ += size;
  std::size_t offset = 0;
  if (buffered_ != 0) {
    const std::size_t needed = 64U - buffered_;
    const std::size_t taken = size < needed ? size : needed;
    std::memcpy(buffer_.data() + buffered_, bytes, taken);
    buffered_ += taken;
    offset += taken;
    if (buffered_ == 64U) {
      compress(buffer_.data());
      buffered_ = 0;
    }
  }
  while (size - offset >= 64U) {
    compress(bytes + offset);
    offset += 64U;
  }
  if (offset < size) {
    const std::size_t remaining = size - offset;
    std::memcpy(buffer_.data(), bytes + offset, remaining);
    buffered_ = remaining;
  }
}

Digest Sha256::finish() noexcept {
  const std::uint64_t total_bits = total_bytes_ * 8U;
  std::uint8_t padding[128];
  std::memset(padding, 0, sizeof(padding));
  padding[0] = 0x80U;
  std::size_t padding_length = (buffered_ < 56U) ? (56U - buffered_)
                                                 : (120U - buffered_);
  for (std::size_t index = 0; index < 8; ++index) {
    padding[padding_length + index] =
        static_cast<std::uint8_t>((total_bits >> ((7U - index) * 8U)) & 0xFFU);
  }
  update(padding, padding_length + 8U);

  Digest digest;
  for (std::size_t index = 0; index < 8; ++index) {
    digest.bytes_[index * 4U] =
        static_cast<std::uint8_t>((state_[index] >> 24U) & 0xFFU);
    digest.bytes_[index * 4U + 1U] =
        static_cast<std::uint8_t>((state_[index] >> 16U) & 0xFFU);
    digest.bytes_[index * 4U + 2U] =
        static_cast<std::uint8_t>((state_[index] >> 8U) & 0xFFU);
    digest.bytes_[index * 4U + 3U] = static_cast<std::uint8_t>(state_[index] & 0xFFU);
  }
  return digest;
}

void DigestBuilder::add_bytes(const void* data, std::size_t size) {
  std::uint8_t header[8];
  const auto length = static_cast<std::uint64_t>(size);
  for (std::size_t index = 0; index < 8; ++index) {
    header[index] = static_cast<std::uint8_t>((length >> ((7U - index) * 8U)) & 0xFFU);
  }
  hasher_.update(header, sizeof(header));
  hasher_.update(data, size);
}

void DigestBuilder::add_text(std::string_view text) {
  add_bytes(text.data(), text.size());
}

void DigestBuilder::add_u8(std::uint8_t value) {
  add_bytes(&value, 1);
}

void DigestBuilder::add_u32(std::uint32_t value) {
  std::uint8_t bytes[4];
  for (std::size_t index = 0; index < 4; ++index) {
    bytes[index] = static_cast<std::uint8_t>((value >> ((3U - index) * 8U)) & 0xFFU);
  }
  add_bytes(bytes, sizeof(bytes));
}

void DigestBuilder::add_u64(std::uint64_t value) {
  std::uint8_t bytes[8];
  for (std::size_t index = 0; index < 8; ++index) {
    bytes[index] = static_cast<std::uint8_t>((value >> ((7U - index) * 8U)) & 0xFFU);
  }
  add_bytes(bytes, sizeof(bytes));
}

void DigestBuilder::add_i64(std::int64_t value) {
  add_u64(static_cast<std::uint64_t>(value));
}

void DigestBuilder::add_bool(bool value) {
  add_u8(value ? 1U : 0U);
}

void DigestBuilder::add_field(std::string_view tag, std::string_view value) {
  add_text(tag);
  add_text(value);
}

void DigestBuilder::add_field_u64(std::string_view tag, std::uint64_t value) {
  add_text(tag);
  add_u64(value);
}

void DigestBuilder::add_field_i64(std::string_view tag, std::int64_t value) {
  add_text(tag);
  add_i64(value);
}

void DigestBuilder::add_section(std::string_view name) {
  add_text("\x1fsection");
  add_text(name);
}

Digest DigestBuilder::finish() const {
  Sha256 copy = hasher_;
  return copy.finish();
}

}  // namespace cooling_capacity_accounting
