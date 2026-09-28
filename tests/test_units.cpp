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

// Proof obligations for the exact quantity types: construction bounds, checked
// arithmetic at the exact ceiling and one past it, underflow refusal, exact
// ppm scaling that conserves the whole quantity, and the signed delta type.

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "cooling_capacity_accounting/cooling_capacity_accounting.hpp"

#include "fixtures.hpp"
#include "test_harness.hpp"

namespace {

using cooling_capacity_accounting::DurationMs;
using cooling_capacity_accounting::ErrorCode;
using cooling_capacity_accounting::Ratio;
using cooling_capacity_accounting::ScaledPower;
using cooling_capacity_accounting::ThermalDelta;
using cooling_capacity_accounting::ThermalPower;
using cooling_capacity_accounting::Timestamp;
using cooling_capacity_accounting::Unit;
using cooling_capacity_accounting::unit_name;
using cooling_capacity_accounting::unit_symbol;

constexpr std::int64_t kCeiling =
    cooling_capacity_accounting::kMaxThermalPowerMilliwatts;
constexpr std::int64_t kI64Max = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t kI64Min = std::numeric_limits<std::int64_t>::min();
constexpr std::uint64_t kU64Max = std::numeric_limits<std::uint64_t>::max();

}  // namespace

CCA_TEST(thermal_power_construction_and_checked_milliwatts) {
  CCA_CHECK(ThermalPower::zero().is_zero());
  CCA_CHECK_EQ(ThermalPower::zero().milliwatts(), 0);
  CCA_CHECK(!ThermalPower::zero().is_ceiling());

  CCA_CHECK(ThermalPower::ceiling().is_ceiling());
  CCA_CHECK_EQ(ThermalPower::ceiling().milliwatts(), kCeiling);
  CCA_CHECK(!ThermalPower::ceiling().is_zero());

  CCA_CHECK_EQ(ThermalPower::of_milliwatts(1).milliwatts(), 1);
  CCA_CHECK_EQ(ThermalPower::of_milliwatts(kCeiling).milliwatts(), kCeiling);
  CCA_CHECK_EQ(ThermalPower::unit(), Unit::Milliwatt);

  CCA_ASSIGN(lowest, ThermalPower::checked_milliwatts(0));
  CCA_CHECK(lowest.is_zero());
  CCA_ASSIGN(highest, ThermalPower::checked_milliwatts(kCeiling));
  CCA_CHECK(highest.is_ceiling());
  CCA_CHECK_CODE(ThermalPower::checked_milliwatts(-1), ErrorCode::NegativeQuantity);
  CCA_CHECK_CODE(ThermalPower::checked_milliwatts(kI64Min),
                 ErrorCode::NegativeQuantity);
  CCA_CHECK_CODE(ThermalPower::checked_milliwatts(kCeiling + 1),
                 ErrorCode::OutOfRange);
  CCA_CHECK_CODE(ThermalPower::checked_milliwatts(kI64Max), ErrorCode::OutOfRange);

  CCA_CHECK(ThermalPower::zero() < ThermalPower::of_milliwatts(1));
  CCA_CHECK(ThermalPower::of_milliwatts(1) < ThermalPower::ceiling());
  CCA_CHECK(ThermalPower::zero() <= ThermalPower::zero());
  CCA_CHECK(ThermalPower::ceiling() > ThermalPower::of_milliwatts(1));
  CCA_CHECK(ThermalPower::ceiling() >= ThermalPower::ceiling());
  CCA_CHECK(ThermalPower::zero() == ThermalPower::zero());
  CCA_CHECK(ThermalPower::zero() != ThermalPower::of_milliwatts(1));
}

CCA_TEST(ratio_construction_bounds) {
  CCA_CHECK(Ratio::none().is_none());
  CCA_CHECK_EQ(Ratio::none().ppm(), 0U);
  CCA_CHECK(Ratio::one().is_one());
  CCA_CHECK_EQ(Ratio::one().ppm(), 1'000'000U);
  CCA_CHECK_EQ(Ratio::scale(), 1'000'000U);
  CCA_CHECK_EQ(Ratio::unit(), Unit::PartsPerMillion);
  CCA_CHECK(Ratio::none() < Ratio::one());

  CCA_ASSIGN(zero, Ratio::of_ppm(0));
  CCA_CHECK(zero.is_none());
  CCA_ASSIGN(single, Ratio::of_ppm(1));
  CCA_CHECK_EQ(single.ppm(), 1U);
  CCA_CHECK(!single.is_none());
  CCA_CHECK(!single.is_one());
  CCA_ASSIGN(half, Ratio::of_ppm(500'000));
  CCA_CHECK_EQ(half.ppm(), 500'000U);
  CCA_ASSIGN(almost, Ratio::of_ppm(999'999));
  CCA_CHECK_EQ(almost.ppm(), 999'999U);
  CCA_CHECK(almost < Ratio::one());
  CCA_ASSIGN(full, Ratio::of_ppm(1'000'000));
  CCA_CHECK(full.is_one());
  CCA_CHECK_EQ(full, Ratio::one());

  // One past the only bound the ratio domain has.
  CCA_CHECK_CODE(Ratio::of_ppm(1'000'001), ErrorCode::OutOfRange);
  CCA_CHECK_CODE(Ratio::of_ppm(std::numeric_limits<std::uint32_t>::max()),
                 ErrorCode::OutOfRange);
  CCA_CHECK(Ratio::of_ppm(123'456).value() != Ratio::of_ppm(123'457).value());
}

CCA_TEST(thermal_power_checked_add_at_the_ceiling) {
  const ThermalPower ceiling = ThermalPower::ceiling();
  const ThermalPower zero = ThermalPower::zero();

  CCA_ASSIGN(plus_zero, ceiling.checked_add(zero));
  CCA_CHECK(plus_zero.is_ceiling());
  CCA_ASSIGN(zero_plus, zero.checked_add(ceiling));
  CCA_CHECK(zero_plus.is_ceiling());
  CCA_ASSIGN(one_short, ThermalPower::of_milliwatts(kCeiling - 1).checked_add(
                            ThermalPower::of_milliwatts(1)));
  CCA_CHECK(one_short.is_ceiling());
  CCA_ASSIGN(nothing, zero.checked_add(zero));
  CCA_CHECK(nothing.is_zero());

  CCA_CHECK_CODE(ceiling.checked_add(ThermalPower::of_milliwatts(1)),
                 ErrorCode::OutOfRange);
  CCA_CHECK_CODE(ceiling.checked_add(ceiling), ErrorCode::OutOfRange);
  CCA_CHECK_CODE(ThermalPower::of_milliwatts(1).checked_add(ceiling),
                 ErrorCode::OutOfRange);
}

CCA_TEST(thermal_power_checked_sub_underflow) {
  const ThermalPower ceiling = ThermalPower::ceiling();
  const ThermalPower zero = ThermalPower::zero();

  CCA_ASSIGN(cancelled, ceiling.checked_sub(ceiling));
  CCA_CHECK(cancelled.is_zero());
  CCA_ASSIGN(unchanged, ceiling.checked_sub(zero));
  CCA_CHECK(unchanged.is_ceiling());
  CCA_ASSIGN(difference, ceiling.checked_sub(ThermalPower::of_milliwatts(kCeiling - 1)));
  CCA_CHECK_EQ(difference.milliwatts(), 1);
  CCA_ASSIGN(empty, zero.checked_sub(zero));
  CCA_CHECK(empty.is_zero());

  CCA_CHECK_CODE(zero.checked_sub(ThermalPower::of_milliwatts(1)),
                 ErrorCode::NumericUnderflow);
  CCA_CHECK_CODE(ThermalPower::of_milliwatts(1).checked_sub(
                     ThermalPower::of_milliwatts(2)),
                 ErrorCode::NumericUnderflow);
  CCA_CHECK_CODE(zero.checked_sub(ceiling), ErrorCode::NumericUnderflow);
}

CCA_TEST(thermal_power_checked_mul_overflow) {
  const ThermalPower ceiling = ThermalPower::ceiling();

  CCA_ASSIGN(by_one, ceiling.checked_mul(1U));
  CCA_CHECK(by_one.is_ceiling());
  CCA_ASSIGN(by_zero, ceiling.checked_mul(0U));
  CCA_CHECK(by_zero.is_zero());
  CCA_ASSIGN(zero_by_max, ThermalPower::zero().checked_mul(kU64Max));
  CCA_CHECK(zero_by_max.is_zero());
  CCA_ASSIGN(thousand, ThermalPower::of_milliwatts(1000).checked_mul(1000U));
  CCA_CHECK_EQ(thousand.milliwatts(), 1'000'000);

  // Exactly the ceiling is accepted; the ceiling plus one milliwatt is not.
  CCA_ASSIGN(at_ceiling,
             ThermalPower::of_milliwatts(1).checked_mul(
                 static_cast<std::uint64_t>(kCeiling)));
  CCA_CHECK(at_ceiling.is_ceiling());
  CCA_CHECK_CODE(ThermalPower::of_milliwatts(1).checked_mul(
                     static_cast<std::uint64_t>(kCeiling) + 1U),
                 ErrorCode::OutOfRange);
  CCA_CHECK_CODE(ceiling.checked_mul(2U), ErrorCode::OutOfRange);
  // The intermediate 128-bit product itself overflows the representable range.
  CCA_CHECK_CODE(ceiling.checked_mul(kU64Max), ErrorCode::NumericOverflow);
  CCA_CHECK_CODE(ThermalPower::of_milliwatts(2).checked_mul(
                     static_cast<std::uint64_t>(kCeiling)),
                 ErrorCode::OutOfRange);
}

CCA_TEST(thermal_power_scaled_by_conserves_the_whole) {
  struct ScaleCase {
    std::int64_t milliwatts;
    std::uint32_t ppm;
  };
  const std::int64_t values[] = {0,          1,          2,          999'999,
                                 1'000'000,  1'000'001,  123'456'789'012'345LL,
                                 kCeiling};
  const std::uint32_t ratios[] = {0U, 1U, 250'000U, 333'333U, 500'000U, 999'999U,
                                  1'000'000U};
  for (const std::int64_t value : values) {
    for (const std::uint32_t ppm : ratios) {
      const ScaleCase item{value, ppm};
      CCA_ASSIGN(ratio, Ratio::of_ppm(item.ppm));
      const ThermalPower whole = ThermalPower::of_milliwatts(item.milliwatts);
      const ScaledPower scaled = whole.scaled_by(ratio);

      // Independent 64-bit evaluation of floor(whole * ppm / 1000000).
      const std::int64_t expected_part =
          (item.milliwatts / 1'000'000) * static_cast<std::int64_t>(item.ppm) +
          ((item.milliwatts % 1'000'000) * static_cast<std::int64_t>(item.ppm)) /
              1'000'000;
      CCA_CHECK_EQ(scaled.part.milliwatts(), expected_part);
      CCA_CHECK_EQ(scaled.remainder.milliwatts(), item.milliwatts - expected_part);

      // part + remainder == whole, always, so nothing is created or lost.
      CCA_ASSIGN(rejoined, scaled.part.checked_add(scaled.remainder));
      CCA_CHECK_EQ(rejoined.milliwatts(), item.milliwatts);
      CCA_CHECK(scaled.part <= whole);
      CCA_CHECK(scaled.part.milliwatts() >= 0);
      CCA_CHECK(scaled.remainder.milliwatts() >= 0);
      if (item.ppm == 0U) {
        CCA_CHECK(scaled.part.is_zero());
        CCA_CHECK_EQ(scaled.remainder.milliwatts(), item.milliwatts);
      }
      if (item.ppm == 1'000'000U) {
        CCA_CHECK_EQ(scaled.part.milliwatts(), item.milliwatts);
        CCA_CHECK(scaled.remainder.is_zero());
      }
    }
  }
}

CCA_TEST(thermal_power_sum_refuses_overflow) {
  CCA_ASSIGN(empty, ThermalPower::sum(std::vector<ThermalPower>{}));
  CCA_CHECK(empty.is_zero());
  CCA_ASSIGN(single_zero, ThermalPower::sum({ThermalPower::zero()}));
  CCA_CHECK(single_zero.is_zero());
  CCA_ASSIGN(small, ThermalPower::sum({ThermalPower::of_milliwatts(1),
                                       ThermalPower::of_milliwatts(2),
                                       ThermalPower::of_milliwatts(3)}));
  CCA_CHECK_EQ(small.milliwatts(), 6);
  CCA_ASSIGN(only_ceiling, ThermalPower::sum({ThermalPower::ceiling()}));
  CCA_CHECK(only_ceiling.is_ceiling());
  CCA_ASSIGN(fills_up, ThermalPower::sum({ThermalPower::of_milliwatts(kCeiling - 1),
                                          ThermalPower::of_milliwatts(1)}));
  CCA_CHECK(fills_up.is_ceiling());
  CCA_ASSIGN(with_zero, ThermalPower::sum({ThermalPower::ceiling(),
                                           ThermalPower::zero()}));
  CCA_CHECK(with_zero.is_ceiling());

  CCA_CHECK_CODE(ThermalPower::sum({ThermalPower::ceiling(),
                                    ThermalPower::of_milliwatts(1)}),
                 ErrorCode::OutOfRange);
  CCA_CHECK_CODE(ThermalPower::sum({ThermalPower::ceiling(), ThermalPower::ceiling()}),
                 ErrorCode::OutOfRange);
  CCA_CHECK_CODE(ThermalPower::sum({ThermalPower::of_milliwatts(1),
                                    ThermalPower::of_milliwatts(2),
                                    ThermalPower::ceiling()}),
                 ErrorCode::OutOfRange);
}

CCA_TEST(duration_ms_checked_arithmetic) {
  CCA_CHECK(DurationMs::zero().is_zero());
  CCA_CHECK(DurationMs::forever().is_forever());
  CCA_CHECK(DurationMs::zero() < DurationMs::forever());
  CCA_CHECK_EQ(DurationMs::unit(), Unit::Millisecond);
  CCA_CHECK_EQ(DurationMs::of_milliseconds(7).milliseconds(), 7);

  CCA_ASSIGN(zero, DurationMs::checked_milliseconds(0));
  CCA_CHECK(zero.is_zero());
  CCA_CHECK_CODE(DurationMs::checked_milliseconds(-1), ErrorCode::NegativeQuantity);
  CCA_CHECK_CODE(DurationMs::checked_milliseconds(kI64Min),
                 ErrorCode::NegativeQuantity);
  CCA_ASSIGN(largest, DurationMs::checked_milliseconds(kI64Max));
  CCA_CHECK(largest.is_forever());

  CCA_ASSIGN(two, DurationMs::of_milliseconds(1).checked_add(
                      DurationMs::of_milliseconds(1)));
  CCA_CHECK_EQ(two.milliseconds(), 2);
  CCA_ASSIGN(plus_zero, DurationMs::forever().checked_add(DurationMs::zero()));
  CCA_CHECK(plus_zero.is_forever());
  CCA_ASSIGN(one_short, DurationMs::of_milliseconds(kI64Max - 1)
                            .checked_add(DurationMs::of_milliseconds(1)));
  CCA_CHECK(one_short.is_forever());
  CCA_CHECK_CODE(DurationMs::forever().checked_add(DurationMs::of_milliseconds(1)),
                 ErrorCode::NumericOverflow);
  CCA_CHECK_CODE(DurationMs::of_milliseconds(kI64Max - 1)
                     .checked_add(DurationMs::of_milliseconds(2)),
                 ErrorCode::NumericOverflow);

  CCA_ASSIGN(one, DurationMs::of_milliseconds(1).checked_sub(DurationMs::zero()));
  CCA_CHECK_EQ(one.milliseconds(), 1);
  CCA_ASSIGN(drained, DurationMs::of_milliseconds(1).checked_sub(
                          DurationMs::of_milliseconds(1)));
  CCA_CHECK(drained.is_zero());
  CCA_ASSIGN(cancelled, DurationMs::forever().checked_sub(DurationMs::forever()));
  CCA_CHECK(cancelled.is_zero());
  CCA_ASSIGN(barely, DurationMs::forever().checked_sub(
                         DurationMs::of_milliseconds(kI64Max - 1)));
  CCA_CHECK_EQ(barely.milliseconds(), 1);
  CCA_CHECK_CODE(DurationMs::zero().checked_sub(DurationMs::of_milliseconds(1)),
                 ErrorCode::NumericUnderflow);
  CCA_CHECK_CODE(DurationMs::of_milliseconds(1).checked_sub(DurationMs::forever()),
                 ErrorCode::NumericUnderflow);
  CCA_CHECK(DurationMs::forever().checked_sub(DurationMs::zero()).ok());
}

CCA_TEST(timestamp_checked_arithmetic) {
  CCA_CHECK_EQ(Timestamp::epoch().unix_milliseconds(), 0);
  CCA_CHECK_EQ(Timestamp::unit(), Unit::Millisecond);
  CCA_CHECK_EQ(Timestamp::of_unix_milliseconds(5).unix_milliseconds(), 5);

  CCA_ASSIGN(epoch, Timestamp::checked_unix_milliseconds(0));
  CCA_CHECK_EQ(epoch.unix_milliseconds(), 0);
  CCA_CHECK_CODE(Timestamp::checked_unix_milliseconds(-1),
                 ErrorCode::NegativeQuantity);
  CCA_CHECK_CODE(Timestamp::checked_unix_milliseconds(kI64Min),
                 ErrorCode::NegativeQuantity);
  CCA_ASSIGN(largest, Timestamp::checked_unix_milliseconds(kI64Max));
  CCA_CHECK_EQ(largest.unix_milliseconds(), kI64Max);

  CCA_ASSIGN(advanced, Timestamp::of_unix_milliseconds(10)
                           .checked_add(DurationMs::of_milliseconds(5)));
  CCA_CHECK_EQ(advanced.unix_milliseconds(), 15);
  CCA_ASSIGN(held, largest.checked_add(DurationMs::zero()));
  CCA_CHECK_EQ(held.unix_milliseconds(), kI64Max);
  CCA_ASSIGN(topped, Timestamp::of_unix_milliseconds(kI64Max - 1)
                         .checked_add(DurationMs::of_milliseconds(1)));
  CCA_CHECK_EQ(topped.unix_milliseconds(), kI64Max);
  CCA_CHECK_CODE(largest.checked_add(DurationMs::of_milliseconds(1)),
                 ErrorCode::NumericOverflow);
  CCA_CHECK_CODE(Timestamp::of_unix_milliseconds(kI64Max - 1)
                     .checked_add(DurationMs::forever()),
                 ErrorCode::NumericOverflow);

  CCA_ASSIGN(since, Timestamp::of_unix_milliseconds(10).checked_since(
                        Timestamp::epoch()));
  CCA_CHECK_EQ(since.milliseconds(), 10);
  CCA_ASSIGN(same, Timestamp::of_unix_milliseconds(10)
                       .checked_since(Timestamp::of_unix_milliseconds(10)));
  CCA_CHECK(same.is_zero());
  CCA_ASSIGN(full_span, largest.checked_since(Timestamp::epoch()));
  CCA_CHECK_EQ(full_span.milliseconds(), kI64Max);
  CCA_CHECK_CODE(Timestamp::epoch().checked_since(
                     Timestamp::of_unix_milliseconds(1)),
                 ErrorCode::NumericUnderflow);
  CCA_CHECK_CODE(Timestamp::of_unix_milliseconds(9).checked_since(
                     Timestamp::of_unix_milliseconds(10)),
                 ErrorCode::NumericUnderflow);

  CCA_ASSIGN(operated, Timestamp::of_unix_milliseconds(10) +
                           DurationMs::of_milliseconds(5));
  CCA_CHECK_EQ(operated.unix_milliseconds(), 15);
  CCA_CHECK_CODE(largest + DurationMs::of_milliseconds(1),
                 ErrorCode::NumericOverflow);

  CCA_CHECK(Timestamp::epoch() < Timestamp::of_unix_milliseconds(1));
  CCA_CHECK(Timestamp::epoch() <= Timestamp::epoch());
  CCA_CHECK(Timestamp::of_unix_milliseconds(1) > Timestamp::epoch());
  CCA_CHECK(Timestamp::epoch() >= Timestamp::epoch());
  CCA_CHECK(Timestamp::epoch() == Timestamp::epoch());
  CCA_CHECK(Timestamp::epoch() != Timestamp::of_unix_milliseconds(1));
}

CCA_TEST(thermal_delta_signed_arithmetic) {
  const ThermalPower three = ThermalPower::of_milliwatts(3);
  const ThermalPower five = ThermalPower::of_milliwatts(5);

  const ThermalDelta positive = five - three;
  CCA_CHECK_EQ(positive.milliwatts(), 2);
  CCA_CHECK(!positive.is_negative());
  CCA_CHECK(!positive.is_zero());
  CCA_CHECK_EQ(positive.unit(), Unit::Milliwatt);

  const ThermalDelta negative = three - five;
  CCA_CHECK_EQ(negative.milliwatts(), -2);
  CCA_CHECK(negative.is_negative());
  CCA_CHECK(negative < positive);
  CCA_CHECK(positive != negative);
  CCA_CHECK(ThermalDelta::zero() == five - five);
  CCA_CHECK(ThermalDelta::zero().is_zero());

  CCA_CHECK_EQ(negative.magnitude().milliwatts(), 2);
  CCA_CHECK_EQ(positive.magnitude().milliwatts(), 2);
  CCA_CHECK_EQ(ThermalDelta::zero().magnitude().milliwatts(), 0);

  CCA_ASSIGN(flipped_negative, negative.negated());
  CCA_CHECK_EQ(flipped_negative, positive);
  CCA_ASSIGN(flipped_positive, positive.negated());
  CCA_CHECK_EQ(flipped_positive, negative);
  CCA_ASSIGN(flipped_zero, ThermalDelta::zero().negated());
  CCA_CHECK(flipped_zero.is_zero());

  CCA_CHECK_EQ(ThermalDelta::of_milliwatts(kI64Max).milliwatts(), kI64Max);
  CCA_CHECK_EQ(ThermalDelta::of_milliwatts(kI64Min).milliwatts(), kI64Min);
  CCA_CHECK_CODE(ThermalDelta::of_milliwatts(kI64Min).negated(),
                 ErrorCode::NumericOverflow);
}

CCA_TEST(unit_symbols_and_names) {
  CCA_CHECK_EQ(unit_symbol(Unit::Milliwatt), std::string_view("mW"));
  CCA_CHECK_EQ(unit_symbol(Unit::PartsPerMillion), std::string_view("ppm"));
  CCA_CHECK_EQ(unit_symbol(Unit::Millisecond), std::string_view("ms"));
  CCA_CHECK_EQ(unit_name(Unit::Milliwatt), std::string_view("milliwatt"));
  CCA_CHECK_EQ(unit_name(Unit::PartsPerMillion),
               std::string_view("parts per million"));
  CCA_CHECK_EQ(unit_name(Unit::Millisecond), std::string_view("millisecond"));

  CCA_CHECK_EQ(ThermalPower::unit(), Unit::Milliwatt);
  CCA_CHECK_EQ(ThermalDelta::unit(), Unit::Milliwatt);
  CCA_CHECK_EQ(Ratio::unit(), Unit::PartsPerMillion);
  CCA_CHECK_EQ(DurationMs::unit(), Unit::Millisecond);
  CCA_CHECK_EQ(Timestamp::unit(), Unit::Millisecond);

  // The three units are distinct and every symbol/name is non-empty.
  CCA_CHECK(unit_symbol(Unit::Milliwatt) != unit_symbol(Unit::PartsPerMillion));
  CCA_CHECK(unit_symbol(Unit::PartsPerMillion) != unit_symbol(Unit::Millisecond));
  CCA_CHECK(unit_name(Unit::Milliwatt) != unit_name(Unit::PartsPerMillion));
  CCA_CHECK(unit_name(Unit::PartsPerMillion) != unit_name(Unit::Millisecond));
  for (const Unit unit :
       {Unit::Milliwatt, Unit::PartsPerMillion, Unit::Millisecond}) {
    CCA_CHECK(!unit_symbol(unit).empty());
    CCA_CHECK(!unit_name(unit).empty());
  }
}
