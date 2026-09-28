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

#include "cooling_capacity_accounting/units.hpp"

#include <cassert>

#include "cooling_capacity_accounting/text.hpp"

namespace cooling_capacity_accounting {
namespace {

constexpr std::int64_t kCeiling = kMaxThermalPowerMilliwatts;

[[nodiscard]] Error ceiling_error(const char* operation, std::int64_t observed) {
  return Error::of(ErrorCode::OutOfRange, operation)
      .with_detail("value " + to_decimal(observed) +
                   " mW exceeds the accounting ceiling of " + to_decimal(kCeiling) +
                   " mW");
}

}  // namespace

std::string_view unit_symbol(Unit unit) noexcept {
  switch (unit) {
    case Unit::Milliwatt:
      return "mW";
    case Unit::PartsPerMillion:
      return "ppm";
    case Unit::Millisecond:
      return "ms";
  }
  return "?";
}

std::string_view unit_name(Unit unit) noexcept {
  switch (unit) {
    case Unit::Milliwatt:
      return "milliwatt";
    case Unit::PartsPerMillion:
      return "parts per million";
    case Unit::Millisecond:
      return "millisecond";
  }
  return "unknown unit";
}

Result<Ratio> Ratio::of_ppm(std::uint32_t ppm) {
  if (ppm > kScale) {
    return Error::of(ErrorCode::OutOfRange,
                     "a ratio is expressed in parts per million between 0 and 1000000")
        .with_detail("ppm " + to_decimal(static_cast<std::uint64_t>(ppm)));
  }
  return Ratio(ppm);
}

ThermalPower ThermalPower::of_milliwatts(std::int64_t value) noexcept {
  assert(value >= 0 && value <= kCeiling);
  return ThermalPower(value);
}

Result<ThermalPower> ThermalPower::checked_milliwatts(std::int64_t value) {
  if (value < 0) {
    return Error::of(ErrorCode::NegativeQuantity,
                     "a thermal power may not be negative");
  }
  if (value > kCeiling) {
    return ceiling_error("ThermalPower::checked_milliwatts", value);
  }
  return ThermalPower(value);
}

Result<ThermalPower> ThermalPower::checked_add(ThermalPower other) const {
  if (milliwatts_ > kCeiling - other.milliwatts_) {
    return ceiling_error("ThermalPower::checked_add", milliwatts_);
  }
  return ThermalPower(milliwatts_ + other.milliwatts_);
}

Result<ThermalPower> ThermalPower::checked_sub(ThermalPower other) const {
  if (other.milliwatts_ > milliwatts_) {
    return Error::of(ErrorCode::NumericUnderflow,
                     "subtracting a larger thermal power would go below zero")
        .with_detail(to_decimal(milliwatts_) + " - " + to_decimal(other.milliwatts_));
  }
  return ThermalPower(milliwatts_ - other.milliwatts_);
}

Result<ThermalPower> ThermalPower::checked_mul(std::uint64_t factor) const {
  CCA_TRY_ASSIGN(product,
                 checked_mul_add(static_cast<std::uint64_t>(milliwatts_), factor, 0U));
  if (product > static_cast<std::uint64_t>(kCeiling)) {
    return ceiling_error("ThermalPower::checked_mul", milliwatts_);
  }
  return ThermalPower(static_cast<std::int64_t>(product));
}

ScaledPower ThermalPower::scaled_by(Ratio ratio) const {
  const Result<std::uint64_t> scaled = checked_mul_div(
      static_cast<std::uint64_t>(milliwatts_),
      static_cast<std::uint64_t>(ratio.ppm()), Ratio::scale());
  // Exact by construction: milliwatts <= 1e15 and ppm <= 1e6, so the product is
  // at most 1e21 and always fits the 128-bit intermediate.
  assert(scaled.ok());
  const std::int64_t part = scaled.ok() ? static_cast<std::int64_t>(scaled.value()) : 0;
  return ScaledPower{ThermalPower(part), ThermalPower(milliwatts_ - part)};
}

Result<ThermalPower> ThermalPower::sum(const std::vector<ThermalPower>& values) {
  std::int64_t total = 0;
  for (const ThermalPower value : values) {
    if (total > kCeiling - value.milliwatts_) {
      return ceiling_error("ThermalPower::sum", total);
    }
    total += value.milliwatts_;
  }
  return ThermalPower(total);
}

ThermalDelta ThermalDelta::of_milliwatts(std::int64_t value) noexcept {
  return ThermalDelta(value);
}

ThermalPower ThermalDelta::magnitude() const noexcept {
  if (milliwatts_ >= 0) {
    return ThermalPower(milliwatts_);
  }
  return ThermalPower(-milliwatts_);
}

Result<ThermalDelta> ThermalDelta::negated() const {
  if (milliwatts_ == std::numeric_limits<std::int64_t>::min()) {
    return Error::of(ErrorCode::NumericOverflow,
                     "the most negative delta has no positive counterpart");
  }
  return ThermalDelta(-milliwatts_);
}

ThermalDelta operator-(ThermalPower lhs, ThermalPower rhs) noexcept {
  return ThermalDelta(lhs.milliwatts() - rhs.milliwatts());
}

DurationMs DurationMs::of_milliseconds(std::int64_t value) noexcept {
  assert(value >= 0);
  return DurationMs{value};
}

Result<DurationMs> DurationMs::checked_milliseconds(std::int64_t value) {
  if (value < 0) {
    return Error::of(ErrorCode::NegativeQuantity, "a duration may not be negative");
  }
  return DurationMs(value);
}

Result<DurationMs> DurationMs::checked_add(DurationMs other) const {
  if (millis_ > std::numeric_limits<std::int64_t>::max() - other.millis_) {
    return Error::of(ErrorCode::NumericOverflow, "DurationMs::checked_add");
  }
  return DurationMs(millis_ + other.millis_);
}

Result<DurationMs> DurationMs::checked_sub(DurationMs other) const {
  if (other.millis_ > millis_) {
    return Error::of(ErrorCode::NumericUnderflow,
                     "subtracting a longer duration would go below zero");
  }
  return DurationMs(millis_ - other.millis_);
}

Timestamp Timestamp::of_unix_milliseconds(std::int64_t value) noexcept {
  assert(value >= 0);
  return Timestamp(value);
}

Result<Timestamp> Timestamp::checked_unix_milliseconds(std::int64_t value) {
  if (value < 0) {
    return Error::of(ErrorCode::NegativeQuantity,
                     "an instant before the Unix epoch is not accepted");
  }
  return Timestamp(value);
}

Result<Timestamp> Timestamp::checked_add(DurationMs duration) const {
  if (millis_ > std::numeric_limits<std::int64_t>::max() - duration.milliseconds()) {
    return Error::of(ErrorCode::NumericOverflow, "Timestamp::checked_add");
  }
  return Timestamp(millis_ + duration.milliseconds());
}

Result<DurationMs> Timestamp::checked_since(Timestamp earlier) const {
  if (earlier.millis_ > millis_) {
    return Error::of(ErrorCode::NumericUnderflow,
                     "the instant precedes the instant it is compared with");
  }
  return DurationMs(millis_ - earlier.millis_);
}

Result<Timestamp> operator+(Timestamp lhs, DurationMs rhs) noexcept {
  return lhs.checked_add(rhs);
}

}  // namespace cooling_capacity_accounting
