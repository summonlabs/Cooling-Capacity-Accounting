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

#ifndef COOLING_CAPACITY_ACCOUNTING_WIDE_HPP
#define COOLING_CAPACITY_ACCOUNTING_WIDE_HPP

#include <cstdint>

#include "cooling_capacity_accounting/errors.hpp"

namespace cooling_capacity_accounting {

/// Portable 128-bit unsigned integer. It exists so that exact fixed-point
/// arithmetic (milliwatt * parts-per-million) is possible without any
/// floating point and without relying on a compiler-specific __int128.
struct WideUInt128 {
  std::uint64_t high = 0;
  std::uint64_t low = 0;
};

[[nodiscard]] constexpr bool wide_is_zero(WideUInt128 value) noexcept {
  return value.high == 0 && value.low == 0;
}

[[nodiscard]] constexpr bool operator==(WideUInt128 lhs, WideUInt128 rhs) noexcept {
  return lhs.high == rhs.high && lhs.low == rhs.low;
}
[[nodiscard]] constexpr bool operator!=(WideUInt128 lhs, WideUInt128 rhs) noexcept {
  return !(lhs == rhs);
}
[[nodiscard]] constexpr bool operator<(WideUInt128 lhs, WideUInt128 rhs) noexcept {
  return lhs.high != rhs.high ? lhs.high < rhs.high : lhs.low < rhs.low;
}

/// Exact 64x64 -> 128 multiplication. Never overflows.
///
/// The product is assembled from four 32-bit limb products. The two cross
/// products are added with an explicit carry, and the carry out of their sum is
/// folded into the high word shifted by 32, which is where a naive
/// implementation silently loses it.
[[nodiscard]] constexpr WideUInt128 wide_multiply(std::uint64_t a,
                                                  std::uint64_t b) noexcept {
  const std::uint64_t a_low = a & 0xFFFFFFFFULL;
  const std::uint64_t a_high = a >> 32U;
  const std::uint64_t b_low = b & 0xFFFFFFFFULL;
  const std::uint64_t b_high = b >> 32U;

  const std::uint64_t low_low = a_low * b_low;
  const std::uint64_t low_high = a_low * b_high;
  const std::uint64_t high_low = a_high * b_low;
  const std::uint64_t high_high = a_high * b_high;

  // low_high + high_low can exceed 64 bits; the carry is part of the result.
  const std::uint64_t cross = low_high + high_low;
  const std::uint64_t cross_carry = cross < low_high ? 1ULL : 0ULL;

  // Folding the middle word into the low word can carry as well.
  const std::uint64_t middle = cross + (low_low >> 32U);
  const std::uint64_t middle_carry = middle < cross ? 1ULL : 0ULL;

  WideUInt128 result;
  result.low = (middle << 32U) | (low_low & 0xFFFFFFFFULL);
  result.high = high_high + (middle >> 32U) +
                ((cross_carry + middle_carry) << 32U);
  return result;
}

/// Exact addition of a 64-bit addend, refusing 128-bit overflow.
[[nodiscard]] Result<WideUInt128> wide_add_u64(WideUInt128 value,
                                               std::uint64_t addend) noexcept;

/// Exact multiplication by a 64-bit factor, refusing 128-bit overflow.
[[nodiscard]] Result<WideUInt128> wide_mul_u64(WideUInt128 value,
                                               std::uint64_t factor) noexcept;

/// floor(value / divisor), refusing a zero divisor and a quotient that does not
/// fit in 64 bits.
[[nodiscard]] Result<std::uint64_t> wide_div_u64(WideUInt128 value,
                                                 std::uint64_t divisor) noexcept;

/// value % divisor.
[[nodiscard]] Result<std::uint64_t> wide_mod_u64(WideUInt128 value,
                                                 std::uint64_t divisor) noexcept;

/// floor((a * b) / divisor) with a fully checked intermediate.
[[nodiscard]] Result<std::uint64_t> checked_mul_div(std::uint64_t a, std::uint64_t b,
                                                    std::uint64_t divisor) noexcept;

/// Exact a * b + addend, refusing 64-bit overflow.
[[nodiscard]] Result<std::uint64_t> checked_mul_add(std::uint64_t a, std::uint64_t b,
                                                    std::uint64_t addend) noexcept;

}  // namespace cooling_capacity_accounting

#endif  // COOLING_CAPACITY_ACCOUNTING_WIDE_HPP
