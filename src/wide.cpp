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

#include "cooling_capacity_accounting/wide.hpp"

#include <limits>

namespace cooling_capacity_accounting {
namespace {

constexpr std::uint64_t kU64Max = std::numeric_limits<std::uint64_t>::max();

[[nodiscard]] Error overflow_error(const char* operation) {
  return Error::of(ErrorCode::NumericOverflow, operation)
      .with_detail("the exact 128-bit intermediate does not fit the destination");
}

}  // namespace

Result<WideUInt128> wide_add_u64(WideUInt128 value, std::uint64_t addend) noexcept {
  const std::uint64_t low = value.low + addend;
  const std::uint64_t carry = low < value.low ? 1U : 0U;
  if (carry != 0U && value.high == kU64Max) {
    return overflow_error("wide_add_u64");
  }
  return WideUInt128{value.high + carry, low};
}

Result<WideUInt128> wide_mul_u64(WideUInt128 value, std::uint64_t factor) noexcept {
  if (factor != 0U && value.high != 0U && value.high > (kU64Max / factor)) {
    return overflow_error("wide_mul_u64");
  }
  const std::uint64_t high_product = value.high * factor;
  const WideUInt128 low_product = wide_multiply(value.low, factor);
  if (high_product > kU64Max - low_product.high) {
    return overflow_error("wide_mul_u64");
  }
  return WideUInt128{high_product + low_product.high, low_product.low};
}

Result<std::uint64_t> wide_div_u64(WideUInt128 value, std::uint64_t divisor) noexcept {
  if (divisor == 0U) {
    return Error::of(ErrorCode::DivisionByZero, "wide_div_u64");
  }
  if (value.high == 0U) {
    return value.low / divisor;
  }
  if (value.high >= divisor) {
    return overflow_error("wide_div_u64");
  }
  // Long division of (remainder:value.low) by divisor, with remainder < divisor
  // so the quotient fits in 64 bits. The loop is the portable form of a
  // 128-by-64 division: it never overflows because the running remainder is
  // always below the divisor once the subtract has been applied.
  std::uint64_t quotient = 0;
  std::uint64_t remainder = value.high;
  for (int bit = 63; bit >= 0; --bit) {
    const std::uint64_t next = (value.low >> static_cast<unsigned>(bit)) & 1U;
    const bool carried = (remainder >> 63U) != 0U;
    remainder = (remainder << 1U) | next;
    if (carried || remainder >= divisor) {
      remainder -= divisor;
      quotient |= (1ULL << static_cast<unsigned>(bit));
    }
  }
  return quotient;
}

Result<std::uint64_t> wide_mod_u64(WideUInt128 value, std::uint64_t divisor) noexcept {
  if (divisor == 0U) {
    return Error::of(ErrorCode::DivisionByZero, "wide_mod_u64");
  }
  std::uint64_t remainder = value.high % divisor;
  for (int bit = 63; bit >= 0; --bit) {
    const std::uint64_t next = (value.low >> static_cast<unsigned>(bit)) & 1U;
    const bool carried = (remainder >> 63U) != 0U;
    remainder = (remainder << 1U) | next;
    if (carried || remainder >= divisor) {
      remainder -= divisor;
    }
  }
  return remainder;
}

Result<std::uint64_t> checked_mul_div(std::uint64_t a, std::uint64_t b,
                                      std::uint64_t divisor) noexcept {
  return wide_div_u64(wide_multiply(a, b), divisor);
}

Result<std::uint64_t> checked_mul_add(std::uint64_t a, std::uint64_t b,
                                      std::uint64_t addend) noexcept {
  CCA_TRY_ASSIGN(sum, wide_add_u64(wide_multiply(a, b), addend));
  if (sum.high != 0U) {
    return overflow_error("checked_mul_add");
  }
  return sum.low;
}

}  // namespace cooling_capacity_accounting
