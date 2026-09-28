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

#include "cooling_capacity_accounting/ids.hpp"

#include <string>

namespace cooling_capacity_accounting {
namespace {

constexpr char kHexDigits[] = "0123456789abcdef";

[[nodiscard]] int hex_value(char byte) noexcept {
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
}

}  // namespace

WideId WideId::from_digest(const Digest& digest) noexcept {
  WideId id;
  const auto& bytes = digest.bytes();
  for (std::size_t index = 0; index < 8; ++index) {
    id.high_ = (id.high_ << 8U) | bytes[index];
    id.low_ = (id.low_ << 8U) | bytes[index + 8];
  }
  return id;
}

Result<WideId> WideId::parse(std::string_view hex32) {
  if (hex32.size() != 32) {
    return Error::of(ErrorCode::InvalidIdentifier,
                     "a wide identifier is exactly 32 hexadecimal characters")
        .with_subject(std::string(hex32));
  }
  WideId id;
  for (std::size_t index = 0; index < 32; ++index) {
    const int value = hex_value(hex32[index]);
    if (value < 0) {
      return Error::of(ErrorCode::InvalidIdentifier,
                       "a wide identifier is hexadecimal only")
          .with_subject(std::string(hex32));
    }
    if (index < 16) {
      id.high_ = (id.high_ << 4U) | static_cast<std::uint64_t>(value);
    } else {
      id.low_ = (id.low_ << 4U) | static_cast<std::uint64_t>(value);
    }
  }
  return id;
}

Result<WideId> WideId::from_material(std::string_view material) {
  const Digest digest = Digest::of_text(material);
  return WideId::from_digest(digest);
}

std::string WideId::to_string() const {
  std::string text;
  text.reserve(32);
  for (int shift = 60; shift >= 0; shift -= 4) {
    text.push_back(kHexDigits[(high_ >> static_cast<unsigned>(shift)) & 0xFU]);
  }
  for (int shift = 60; shift >= 0; shift -= 4) {
    text.push_back(kHexDigits[(low_ >> static_cast<unsigned>(shift)) & 0xFU]);
  }
  return text;
}

GenerationOrder compare_generations(const GenerationBundle& lhs,
                                    const GenerationBundle& rhs) noexcept {
  if (lhs == rhs) {
    return GenerationOrder::Equal;
  }
  if (lhs.epoch != rhs.epoch) {
    // Two bundles from different control-plane epochs are not ordered at all:
    // neither is "older", and no request may be fenced on the comparison.
    return GenerationOrder::Incomparable;
  }
  if (lhs.topology != rhs.topology) {
    return lhs.topology < rhs.topology ? GenerationOrder::Older : GenerationOrder::Newer;
  }
  if (lhs.policy != rhs.policy) {
    return lhs.policy < rhs.policy ? GenerationOrder::Older : GenerationOrder::Newer;
  }
  if (lhs.evidence != rhs.evidence) {
    return lhs.evidence < rhs.evidence ? GenerationOrder::Older : GenerationOrder::Newer;
  }
  return lhs.revision < rhs.revision ? GenerationOrder::Older : GenerationOrder::Newer;
}

std::string_view generation_order_name(GenerationOrder order) noexcept {
  switch (order) {
    case GenerationOrder::Equal:
      return "Equal";
    case GenerationOrder::Older:
      return "Older";
    case GenerationOrder::Newer:
      return "Newer";
    case GenerationOrder::Incomparable:
      return "Incomparable";
  }
  return "Incomparable";
}

}  // namespace cooling_capacity_accounting
