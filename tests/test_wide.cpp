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

// Proof obligations for the portable 128-bit helpers: exactness against
// independently computed products, refusal at the exact overflow boundary, and
// floor-division reconstruction. No test in this file uses floating point or a
// compiler-specific 128-bit type.

#include <cstdint>
#include <limits>
#include <string>

#include "cooling_capacity_accounting/cooling_capacity_accounting.hpp"

#include "fixtures.hpp"
#include "test_harness.hpp"

namespace {

using cooling_capacity_accounting::checked_mul_add;
using cooling_capacity_accounting::checked_mul_div;
using cooling_capacity_accounting::ErrorCode;
using cooling_capacity_accounting::Result;
using cooling_capacity_accounting::wide_add_u64;
using cooling_capacity_accounting::wide_div_u64;
using cooling_capacity_accounting::wide_is_zero;
using cooling_capacity_accounting::wide_mod_u64;
using cooling_capacity_accounting::wide_mul_u64;
using cooling_capacity_accounting::wide_multiply;
using cooling_capacity_accounting::WideUInt128;

constexpr std::uint64_t kU64Max = std::numeric_limits<std::uint64_t>::max();
constexpr std::uint64_t kTwoTo32 = 0x1'0000'0000ULL;
constexpr std::uint64_t kTwoTo63 = 0x8000'0000'0000'0000ULL;

[[nodiscard]] WideUInt128 wide_of(std::uint64_t high, std::uint64_t low) noexcept {
  WideUInt128 value;
  value.high = high;
  value.low = low;
  return value;
}

/// 2^64 mod modulus, computed with 64 doublings of a value below the modulus.
/// modulus must be below 2^32 so the doubling never overflows 64 bits.
[[nodiscard]] std::uint64_t two_pow_64_mod(std::uint64_t modulus) {
  std::uint64_t value = 1U % modulus;
  for (unsigned bit = 0; bit < 64U; ++bit) {
    value = (value * 2U) % modulus;
  }
  return value;
}

/// value mod modulus using only 64-bit intermediates: an independent oracle for
/// a 128-bit product, so a wrong high or low limb cannot cancel out here.
[[nodiscard]] std::uint64_t wide_mod_small(WideUInt128 value, std::uint64_t modulus) {
  const std::uint64_t high = value.high % modulus;
  const std::uint64_t low = value.low % modulus;
  return ((high * two_pow_64_mod(modulus)) % modulus + low) % modulus;
}

struct MulExpectation {
  bool overflow = false;
  WideUInt128 value;
};

/// Independent (high:low) * factor oracle built only from wide_multiply, which
/// the modular tests above verify limb by limb:
///   (high:low) * factor == (high * factor) << 64 + (low * factor).
[[nodiscard]] MulExpectation multiply_expectation(WideUInt128 value,
                                                  std::uint64_t factor) {
  const WideUInt128 high_product = wide_multiply(value.high, factor);
  const WideUInt128 low_product = wide_multiply(value.low, factor);
  MulExpectation expectation;
  if (high_product.high != 0) {
    expectation.overflow = true;  // the shifted term does not fit 128 bits
    return expectation;
  }
  if (high_product.low > kU64Max - low_product.high) {
    expectation.overflow = true;
  }
  expectation.value = wide_of(high_product.low + low_product.high, low_product.low);
  return expectation;
}

}  // namespace

CCA_TEST(wide_multiply_known_products) {
  struct ProductCase {
    std::uint64_t a;
    std::uint64_t b;
    std::uint64_t high;
    std::uint64_t low;
  };
  const ProductCase cases[] = {
      {0U, 0U, 0U, 0U},
      {0U, kU64Max, 0U, 0U},
      {kU64Max, 0U, 0U, 0U},
      {1U, kU64Max, 0U, kU64Max},
      {kU64Max, 1U, 0U, kU64Max},
      // (2^64 - 1)^2 == 2^128 - 2^65 + 1
      {kU64Max, kU64Max, kU64Max - 1U, 1U},
      // 2^32 * 2^32 == 2^64
      {kTwoTo32, kTwoTo32, 1U, 0U},
      // 2^63 * 2 == 2^64
      {kTwoTo63, 2U, 1U, 0U},
      // (2^32 - 1)^2 == 2^64 - 2^33 + 1
      {0xFFFF'FFFFULL, 0xFFFF'FFFFULL, 0U, 0xFFFF'FFFE'0000'0001ULL},
      // (2^32 - 1) * (2^32 + 1) == 2^64 - 1
      {0xFFFF'FFFFULL, kTwoTo32 + 1U, 0U, kU64Max},
      // 2^32 * (2^64 - 1) == 2^96 - 2^32
      {kTwoTo32, kU64Max, 0xFFFF'FFFFULL, 0xFFFF'FFFF'0000'0000ULL},
      // 123456789 * 987654321 == 121932631112635269
      {123'456'789ULL, 987'654'321ULL, 0U, 121'932'631'112'635'269ULL},
  };
  for (const ProductCase& item : cases) {
    const WideUInt128 product = wide_multiply(item.a, item.b);
    CCA_CHECK_EQ(product.high, item.high);
    CCA_CHECK_EQ(product.low, item.low);
    CCA_CHECK_EQ(wide_multiply(item.b, item.a), product);
  }

  CCA_CHECK(wide_is_zero(wide_of(0U, 0U)));
  CCA_CHECK(!wide_is_zero(wide_of(0U, 1U)));
  CCA_CHECK(!wide_is_zero(wide_of(1U, 0U)));
  CCA_CHECK(wide_of(0U, 5U) == wide_multiply(5U, 1U));
  CCA_CHECK(wide_of(0U, 5U) != wide_of(1U, 5U));
  CCA_CHECK(wide_of(0U, 5U) < wide_of(0U, 6U));
  CCA_CHECK(wide_of(0U, kU64Max) < wide_of(1U, 0U));
  CCA_CHECK(wide_of(2U, 0U) < wide_of(3U, 0U));
}

CCA_TEST(wide_multiply_matches_independent_modular_arithmetic) {
  constexpr std::uint64_t kSeed = 0x9E37'79B9'7F4A'7C15ULL;
  constexpr std::uint64_t kModuli[] = {1'000'000'007ULL, 998'244'353ULL,
                                       4'294'967'291ULL};
  cca_test::SeededRandom random(kSeed);
  for (int iteration = 0; iteration < 256; ++iteration) {
    const std::uint64_t a = random.next_u64();
    const std::uint64_t b = random.next_u64();
    const WideUInt128 product = wide_multiply(a, b);
    if (!(product == wide_multiply(b, a))) {
      cca_test::note("failing seed " + std::to_string(kSeed) + " iteration " +
                     std::to_string(iteration));
      CCA_FAIL("wide_multiply is not commutative");
      return;
    }
    for (const std::uint64_t modulus : kModuli) {
      const std::uint64_t expected = ((a % modulus) * (b % modulus)) % modulus;
      if (wide_mod_small(product, modulus) != expected) {
        cca_test::note("failing seed " + std::to_string(kSeed) + " iteration " +
                       std::to_string(iteration) + " modulus " +
                       std::to_string(modulus));
        CCA_FAIL("wide_multiply disagrees with modular arithmetic");
        return;
      }
    }
    // A product of two 32-bit values is exactly the built-in 64-bit product.
    const std::uint64_t small_a = a & 0xFFFF'FFFFULL;
    const std::uint64_t small_b = b & 0xFFFF'FFFFULL;
    const WideUInt128 small = wide_multiply(small_a, small_b);
    if (small.high != 0U || small.low != small_a * small_b) {
      cca_test::note("failing seed " + std::to_string(kSeed) + " iteration " +
                     std::to_string(iteration));
      CCA_FAIL("wide_multiply disagrees with the 64-bit product of 32-bit values");
      return;
    }
  }
}

CCA_TEST(wide_add_u64_exactness_and_carry) {
  CCA_ASSIGN(zero_sum, wide_add_u64(wide_of(0U, 0U), 0U));
  CCA_CHECK(wide_is_zero(zero_sum));

  CCA_ASSIGN(identity_sum, wide_add_u64(wide_of(7U, 9U), 0U));
  CCA_CHECK_EQ(identity_sum.high, 7U);
  CCA_CHECK_EQ(identity_sum.low, 9U);

  // One past the low-limb boundary carries into the high limb.
  CCA_ASSIGN(carried, wide_add_u64(wide_of(0U, kU64Max - 1U), 1U));
  CCA_CHECK_EQ(carried.high, 0U);
  CCA_CHECK_EQ(carried.low, kU64Max);

  CCA_ASSIGN(carried_again, wide_add_u64(carried, 1U));
  CCA_CHECK_EQ(carried_again.high, 1U);
  CCA_CHECK_EQ(carried_again.low, 0U);

  // 0xFFFFFFFFFFFFFFFF_FFFFFFFF + 0 is representable; adding one is not.
  CCA_ASSIGN(saturated, wide_add_u64(wide_of(kU64Max, kU64Max), 0U));
  CCA_CHECK_EQ(saturated.high, kU64Max);
  CCA_CHECK_EQ(saturated.low, kU64Max);
  CCA_CHECK_CODE(wide_add_u64(wide_of(kU64Max, kU64Max), 1U),
                 ErrorCode::NumericOverflow);
  // 2^64 * (2^64 - 1) + (2^64 - 1) == 2^128 - 1, which is representable.
  CCA_ASSIGN(top, wide_add_u64(wide_of(kU64Max, 0U), kU64Max));
  CCA_CHECK_EQ(top.high, kU64Max);
  CCA_CHECK_EQ(top.low, kU64Max);
  CCA_CHECK_CODE(wide_add_u64(top, 1U), ErrorCode::NumericOverflow);

  // A carry that does not reach the top is exact.
  CCA_ASSIGN(no_top_carry, wide_add_u64(wide_of(1U, kU64Max), kU64Max));
  CCA_CHECK_EQ(no_top_carry.high, 2U);
  CCA_CHECK_EQ(no_top_carry.low, kU64Max - 1U);
}

CCA_TEST(wide_mul_u64_exactness_and_overflow) {
  CCA_ASSIGN(by_zero, wide_mul_u64(wide_of(kU64Max, kU64Max), 0U));
  CCA_CHECK(wide_is_zero(by_zero));
  CCA_ASSIGN(zero_by, wide_mul_u64(wide_of(0U, 0U), kU64Max));
  CCA_CHECK(wide_is_zero(zero_by));
  CCA_ASSIGN(by_one, wide_mul_u64(wide_of(3U, 4U), 1U));
  CCA_CHECK_EQ(by_one.high, 3U);
  CCA_CHECK_EQ(by_one.low, 4U);

  CCA_ASSIGN(doubled, wide_mul_u64(wide_of(0U, kTwoTo63), 2U));
  CCA_CHECK_EQ(doubled.high, 1U);
  CCA_CHECK_EQ(doubled.low, 0U);

  // (2^64 - 1) * (2^64 - 1) == 2^128 - 2^65 + 1: the largest representable
  // square, so the boundary itself must succeed.
  CCA_ASSIGN(square, wide_mul_u64(wide_of(0U, kU64Max), kU64Max));
  CCA_CHECK_EQ(square.high, kU64Max - 1U);
  CCA_CHECK_EQ(square.low, 1U);

  // 2^64 * (2^64 - 1) == 2^128 - 2^64: still representable.
  CCA_ASSIGN(shifted, wide_mul_u64(wide_of(1U, 0U), kU64Max));
  CCA_CHECK_EQ(shifted.high, kU64Max);
  CCA_CHECK_EQ(shifted.low, 0U);

  // One past: 2 * 2^64 * (2^64 - 1) does not fit.
  CCA_CHECK_CODE(wide_mul_u64(wide_of(2U, 0U), kU64Max),
                 ErrorCode::NumericOverflow);
  CCA_CHECK_CODE(wide_mul_u64(wide_of(kU64Max, 0U), 2U), ErrorCode::NumericOverflow);
  CCA_CHECK_CODE(wide_mul_u64(wide_of(1U, kU64Max), kU64Max),
                 ErrorCode::NumericOverflow);
}

CCA_TEST(wide_mul_u64_matches_independent_oracle) {
  constexpr std::uint64_t kSeed = 0x00C0'FFEE'1234'5678ULL;
  cca_test::SeededRandom random(kSeed);
  for (int iteration = 0; iteration < 256; ++iteration) {
    const WideUInt128 value = wide_of(random.next_u64(), random.next_u64());
    const std::uint64_t factor = random.next_u64();
    const MulExpectation expected = multiply_expectation(value, factor);
    const Result<WideUInt128> observed = wide_mul_u64(value, factor);
    if (expected.overflow) {
      if (observed.ok()) {
        cca_test::note("failing seed " + std::to_string(kSeed) + " iteration " +
                       std::to_string(iteration));
        CCA_FAIL("wide_mul_u64 accepted a product that does not fit 128 bits");
        return;
      }
      CCA_CHECK_EQ(observed.code(), ErrorCode::NumericOverflow);
    } else if (!observed.ok()) {
      cca_test::note("failing seed " + std::to_string(kSeed) + " iteration " +
                     std::to_string(iteration));
      CCA_FAIL("wide_mul_u64 refused a representable product");
      return;
    } else if (!(observed.value() == expected.value)) {
      cca_test::note("failing seed " + std::to_string(kSeed) + " iteration " +
                     std::to_string(iteration));
      CCA_FAIL("wide_mul_u64 disagrees with the limb oracle");
      return;
    }
  }
}

CCA_TEST(wide_div_u64_and_wide_mod_u64_refuse_a_zero_divisor) {
  CCA_CHECK_CODE(wide_div_u64(wide_of(0U, 1U), 0U), ErrorCode::DivisionByZero);
  CCA_CHECK_CODE(wide_mod_u64(wide_of(0U, 1U), 0U), ErrorCode::DivisionByZero);
  CCA_CHECK_CODE(wide_div_u64(wide_of(kU64Max, kU64Max), 0U),
                 ErrorCode::DivisionByZero);
  CCA_CHECK_CODE(wide_mod_u64(wide_of(kU64Max, kU64Max), 0U),
                 ErrorCode::DivisionByZero);
}

CCA_TEST(wide_div_u64_and_wide_mod_u64_exact_values) {
  CCA_ASSIGN(narrow, wide_div_u64(wide_of(0U, 100U), 7U));
  CCA_CHECK_EQ(narrow, 14U);
  CCA_ASSIGN(narrow_mod, wide_mod_u64(wide_of(0U, 100U), 7U));
  CCA_CHECK_EQ(narrow_mod, 2U);
  CCA_ASSIGN(zero_quotient, wide_div_u64(wide_of(0U, 0U), kU64Max));
  CCA_CHECK_EQ(zero_quotient, 0U);

  // 2^64 / 2 == 2^63, the largest power of two a 64-bit quotient can hold.
  CCA_ASSIGN(power, wide_div_u64(wide_of(1U, 0U), 2U));
  CCA_CHECK_EQ(power, kTwoTo63);
  CCA_ASSIGN(power_mod, wide_mod_u64(wide_of(1U, 0U), 2U));
  CCA_CHECK_EQ(power_mod, 0U);

  // 2^64 / (2^64 - 1) == 1, remainder 1.
  CCA_ASSIGN(one, wide_div_u64(wide_of(1U, 0U), kU64Max));
  CCA_CHECK_EQ(one, 1U);
  CCA_ASSIGN(one_mod, wide_mod_u64(wide_of(1U, 0U), kU64Max));
  CCA_CHECK_EQ(one_mod, 1U);

  // (2^128 - 1) / (2^64 - 1) == 2^64 + 1: one past the 64-bit quotient.
  CCA_CHECK_CODE(wide_div_u64(wide_of(kU64Max, kU64Max), kU64Max),
                 ErrorCode::NumericOverflow);

  // (2^64 - 2) * 2^64 + (2^64 - 1) == (2^64 - 1)^2 + (2^64 - 2)
  CCA_ASSIGN(largest, wide_div_u64(wide_of(kU64Max - 1U, kU64Max), kU64Max));
  CCA_CHECK_EQ(largest, kU64Max);
  CCA_ASSIGN(largest_mod, wide_mod_u64(wide_of(kU64Max - 1U, kU64Max), kU64Max));
  CCA_CHECK_EQ(largest_mod, kU64Max - 1U);
}

CCA_TEST(wide_div_and_mod_reconstruct_a_random_dividend) {
  constexpr std::uint64_t kSeed = 0x5DEE'CE66'D000'0001ULL;
  cca_test::SeededRandom random(kSeed);
  for (int iteration = 0; iteration < 256; ++iteration) {
    const std::uint64_t divisor = static_cast<std::uint64_t>(
        random.next_range(1, std::numeric_limits<std::int64_t>::max()));
    const std::uint64_t quotient = random.next_u64();
    const std::uint64_t remainder = static_cast<std::uint64_t>(
        random.next_range(0, static_cast<std::int64_t>(divisor - 1U)));
    CCA_ASSIGN(dividend,
               wide_add_u64(wide_multiply(quotient, divisor), remainder));
    const Result<std::uint64_t> observed_quotient = wide_div_u64(dividend, divisor);
    const Result<std::uint64_t> observed_remainder = wide_mod_u64(dividend, divisor);
    if (!observed_quotient.ok() || !observed_remainder.ok()) {
      cca_test::note("failing seed " + std::to_string(kSeed) + " iteration " +
                     std::to_string(iteration));
      CCA_FAIL("floor division of a constructed dividend failed");
      return;
    }
    if (observed_quotient.value() != quotient ||
        observed_remainder.value() != remainder) {
      cca_test::note("failing seed " + std::to_string(kSeed) + " iteration " +
                     std::to_string(iteration));
      CCA_FAIL("quotient or remainder does not match the constructed dividend");
      return;
    }
    // The remainder is always below the divisor.
    CCA_CHECK(observed_remainder.value() < divisor);
  }
}

CCA_TEST(checked_mul_div_exact_and_overflowing_cases) {
  CCA_ASSIGN(identity, checked_mul_div(1U, 1U, 1U));
  CCA_CHECK_EQ(identity, 1U);
  CCA_ASSIGN(floored, checked_mul_div(6U, 7U, 3U));
  CCA_CHECK_EQ(floored, 14U);
  CCA_ASSIGN(zero_product, checked_mul_div(0U, kU64Max, kU64Max));
  CCA_CHECK_EQ(zero_product, 0U);
  CCA_ASSIGN(zero_factor, checked_mul_div(kU64Max, 0U, kU64Max));
  CCA_CHECK_EQ(zero_factor, 0U);
  CCA_ASSIGN(greatest, checked_mul_div(kU64Max, kU64Max, kU64Max));
  CCA_CHECK_EQ(greatest, kU64Max);
  CCA_ASSIGN(halved, checked_mul_div(2U, kU64Max, 2U));
  CCA_CHECK_EQ(halved, kU64Max);
  CCA_CHECK_CODE(checked_mul_div(kU64Max, kU64Max, 1U), ErrorCode::NumericOverflow);
  CCA_CHECK_CODE(checked_mul_div(kU64Max, 2U, 1U), ErrorCode::NumericOverflow);
  CCA_CHECK_CODE(checked_mul_div(1U, 1U, 0U), ErrorCode::DivisionByZero);
  CCA_CHECK_CODE(checked_mul_div(0U, 0U, 0U), ErrorCode::DivisionByZero);

  // checked_mul_div is exactly wide_div_u64(wide_multiply(a, b), d).
  CCA_ASSIGN(via_wide, wide_div_u64(wide_multiply(123'456'789ULL, 987'654'321ULL),
                                    1'000'000'007ULL));
  CCA_ASSIGN(direct, checked_mul_div(123'456'789ULL, 987'654'321ULL, 1'000'000'007ULL));
  CCA_CHECK_EQ(direct, via_wide);
}

CCA_TEST(checked_mul_div_matches_the_euclidean_invariant) {
  struct InvariantCase {
    std::uint64_t a;
    std::uint64_t b;
    std::uint64_t divisor;
  };
  const InvariantCase cases[] = {
      {0U, 0U, 1U},
      {0U, kU64Max, kU64Max},
      {1U, kU64Max, kU64Max},
      {2U, kU64Max, kU64Max},
      {kU64Max, 0U, 1U},
      {kU64Max, 1U, kU64Max},
      {kU64Max, kU64Max, kU64Max},
      {1U, 1U, 1U},
      {3U, kU64Max, 7U},
      {7U, 123'456'789ULL, 7U},
      {7U, 123'456'789ULL, 123'456'789ULL},
      {1000U, 1'234'567'890'123'456'789ULL, 97U},
      {123'456'789ULL, 987'654'321ULL, 1'000'000'007ULL},
  };
  for (const InvariantCase& item : cases) {
    const std::uint64_t quotient = item.b / item.divisor;
    const std::uint64_t residue = item.b % item.divisor;
    // The identity is asserted only where every term is representable, which is
    // the exact condition the invariant is stated under.
    CCA_CHECK(item.a == 0U || quotient <= kU64Max / item.a);
    CCA_CHECK(item.a == 0U || residue <= kU64Max / item.a);
    const Result<std::uint64_t> left = checked_mul_div(item.a, item.b, item.divisor);
    const Result<std::uint64_t> tail = checked_mul_div(item.a, residue, item.divisor);
    if (!left.ok() || !tail.ok()) {
      CCA_FAIL("a representable invariant case was refused");
      return;
    }
    const Result<std::uint64_t> right =
        checked_mul_add(item.a, quotient, tail.value());
    if (!right.ok()) {
      CCA_FAIL("the right-hand side of the invariant overflowed");
      return;
    }
    CCA_CHECK_EQ(left.value(), right.value());
  }

  // The same identity over values constructed so that no term overflows.
  constexpr std::uint64_t kSeed = 0xBEEF'0000'1234'5678ULL;
  cca_test::SeededRandom random(kSeed);
  for (int iteration = 0; iteration < 128; ++iteration) {
    const std::uint64_t a =
        static_cast<std::uint64_t>(random.next_range(1, 1'000'000));
    const std::uint64_t divisor =
        static_cast<std::uint64_t>(random.next_range(1, 1'000'000));
    const std::uint64_t quotient =
        static_cast<std::uint64_t>(random.next_range(0, 1'000'000'000'000LL));
    const std::uint64_t residue = static_cast<std::uint64_t>(
        random.next_range(0, static_cast<std::int64_t>(divisor - 1U)));
    CCA_ASSIGN(b, wide_add_u64(wide_multiply(quotient, divisor), residue));
    CCA_CHECK_EQ(b.high, 0U);
    const Result<std::uint64_t> left = checked_mul_div(a, b.low, divisor);
    const Result<std::uint64_t> tail = checked_mul_div(a, residue, divisor);
    if (!left.ok() || !tail.ok()) {
      cca_test::note("failing seed " + std::to_string(kSeed) + " iteration " +
                     std::to_string(iteration));
      CCA_FAIL("a constructed invariant case was refused");
      return;
    }
    const Result<std::uint64_t> right =
        checked_mul_add(a, quotient, tail.value());
    if (!right.ok() || right.value() != left.value()) {
      cca_test::note("failing seed " + std::to_string(kSeed) + " iteration " +
                     std::to_string(iteration));
      CCA_FAIL("the euclidean invariant does not hold");
      return;
    }
  }
}

CCA_TEST(checked_mul_add_exact_and_overflowing_cases) {
  CCA_ASSIGN(zero, checked_mul_add(0U, 0U, 0U));
  CCA_CHECK_EQ(zero, 0U);
  CCA_ASSIGN(small, checked_mul_add(1U, 2U, 3U));
  CCA_CHECK_EQ(small, 5U);
  // A zero factor cancels the largest multiplicand; the addend survives.
  CCA_ASSIGN(zero_factor, checked_mul_add(kU64Max, 0U, 0U));
  CCA_CHECK_EQ(zero_factor, 0U);
  CCA_ASSIGN(zero_factor_with_addend, checked_mul_add(kU64Max, 0U, kU64Max));
  CCA_CHECK_EQ(zero_factor_with_addend, kU64Max);
  CCA_ASSIGN(addend_only, checked_mul_add(0U, kU64Max, kU64Max));
  CCA_CHECK_EQ(addend_only, kU64Max);
  CCA_ASSIGN(by_one, checked_mul_add(kU64Max, 1U, 0U));
  CCA_CHECK_EQ(by_one, kU64Max);
  CCA_ASSIGN(largest_32, checked_mul_add(kTwoTo32, kTwoTo32 - 1U, 0U));
  CCA_CHECK_EQ(largest_32, kU64Max - (kTwoTo32 - 1U));
  CCA_ASSIGN(exact_top, checked_mul_add(kTwoTo63 - 1U, 2U, 0U));
  CCA_CHECK_EQ(exact_top, kU64Max - 1U);

  // One past each boundary.
  CCA_CHECK_CODE(checked_mul_add(kTwoTo63, 2U, 0U), ErrorCode::NumericOverflow);
  CCA_CHECK_CODE(checked_mul_add(kTwoTo32, kTwoTo32, 0U), ErrorCode::NumericOverflow);
  CCA_CHECK_CODE(checked_mul_add(kU64Max, 1U, 1U), ErrorCode::NumericOverflow);
  CCA_CHECK_CODE(checked_mul_add(kU64Max, kU64Max, 0U), ErrorCode::NumericOverflow);
  CCA_CHECK_CODE(checked_mul_add(kTwoTo32, kTwoTo32 - 1U, kU64Max),
                 ErrorCode::NumericOverflow);
  CCA_CHECK_CODE(checked_mul_add(kTwoTo32, kTwoTo32, kU64Max),
                 ErrorCode::NumericOverflow);

  // The factor order never changes the exact result.
  CCA_ASSIGN(ordered, checked_mul_add(7U, 11U, 13U));
  CCA_ASSIGN(swapped, checked_mul_add(11U, 7U, 13U));
  CCA_CHECK_EQ(ordered, swapped);
}

CCA_TEST(checked_mul_add_matches_the_limb_oracle) {
  constexpr std::uint64_t kSeed = 0x0BAD'F00D'DEAD'BEEFULL;
  cca_test::SeededRandom random(kSeed);
  for (int iteration = 0; iteration < 256; ++iteration) {
    const std::uint64_t a = random.next_u64();
    const std::uint64_t b = random.next_u64();
    const std::uint64_t addend = random.next_u64();
    const Result<WideUInt128> exact = wide_add_u64(wide_multiply(a, b), addend);
    const Result<std::uint64_t> observed = checked_mul_add(a, b, addend);
    const bool expected_overflow = !exact.ok() || exact.value().high != 0U;
    if (expected_overflow) {
      if (observed.ok()) {
        cca_test::note("failing seed " + std::to_string(kSeed) + " iteration " +
                       std::to_string(iteration));
        CCA_FAIL("checked_mul_add accepted a product that does not fit 64 bits");
        return;
      }
      CCA_CHECK_EQ(observed.code(), ErrorCode::NumericOverflow);
    } else if (!observed.ok() || observed.value() != exact.value().low) {
      cca_test::note("failing seed " + std::to_string(kSeed) + " iteration " +
                     std::to_string(iteration));
      CCA_FAIL("checked_mul_add disagrees with the exact 128-bit sum");
      return;
    }
  }
}
