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

#ifndef COOLING_CAPACITY_ACCOUNTING_UNITS_HPP
#define COOLING_CAPACITY_ACCOUNTING_UNITS_HPP

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "cooling_capacity_accounting/errors.hpp"
#include "cooling_capacity_accounting/limits.hpp"
#include "cooling_capacity_accounting/wide.hpp"

namespace cooling_capacity_accounting {

/// Canonical units of the accounting domain. Every quantity type names exactly
/// one of these, and no quantity is ever implicitly converted between units.
enum class Unit : std::int32_t {
  Milliwatt = 1,
  PartsPerMillion = 2,
  Millisecond = 3,
};

/// Stable name of a unit ("mW", "ppm", "ms").
[[nodiscard]] std::string_view unit_symbol(Unit unit) noexcept;
/// Stable long name of a unit.
[[nodiscard]] std::string_view unit_name(Unit unit) noexcept;

/// An exact ratio in parts per million inside [0, 1000000].
class Ratio {
 public:
  Ratio() = delete;

  [[nodiscard]] static Result<Ratio> of_ppm(std::uint32_t ppm);
  /// 1000000 ppm: the whole quantity.
  [[nodiscard]] static constexpr Ratio one() noexcept { return Ratio(kScale); }
  /// 0 ppm: none of the quantity.
  [[nodiscard]] static constexpr Ratio none() noexcept { return Ratio(0); }
  [[nodiscard]] static constexpr std::uint32_t scale() noexcept { return kScale; }

  [[nodiscard]] constexpr std::uint32_t ppm() const noexcept { return ppm_; }
  [[nodiscard]] constexpr bool is_none() const noexcept { return ppm_ == 0; }
  [[nodiscard]] constexpr bool is_one() const noexcept { return ppm_ == kScale; }
  [[nodiscard]] static constexpr Unit unit() noexcept { return Unit::PartsPerMillion; }

  friend constexpr bool operator==(Ratio lhs, Ratio rhs) noexcept {
    return lhs.ppm_ == rhs.ppm_;
  }
  friend constexpr bool operator!=(Ratio lhs, Ratio rhs) noexcept {
    return lhs.ppm_ != rhs.ppm_;
  }
  friend constexpr bool operator<(Ratio lhs, Ratio rhs) noexcept {
    return lhs.ppm_ < rhs.ppm_;
  }

 private:
  static constexpr std::uint32_t kScale = 1'000'000U;
  constexpr explicit Ratio(std::uint32_t ppm) noexcept : ppm_(ppm) {}
  std::uint32_t ppm_ = kScale;
};

/// Result of an exact scaling: the part that was apportioned and the part that
/// was left over. Both are exact integers and they always re-sum to the whole.
/// Declared before ThermalPower so that scaled_by can name it, and defined
/// after it because it holds one by value.
struct ScaledPower;

/// An exact non-negative thermal power in milliwatts.
class ThermalPower {
 public:
  ThermalPower() = default;

  /// Precondition: value >= 0. Used for values that are non-negative by
  /// construction; untrusted input goes through checked_milliwatts.
  [[nodiscard]] static ThermalPower of_milliwatts(std::int64_t value) noexcept;
  /// Validates an untrusted milliwatt value.
  [[nodiscard]] static Result<ThermalPower> checked_milliwatts(std::int64_t value);
  [[nodiscard]] static constexpr ThermalPower zero() noexcept { return ThermalPower(0); }
  [[nodiscard]] static constexpr ThermalPower ceiling() noexcept {
    return ThermalPower(kMaxThermalPowerMilliwatts);
  }
  [[nodiscard]] static constexpr Unit unit() noexcept { return Unit::Milliwatt; }

  [[nodiscard]] constexpr std::int64_t milliwatts() const noexcept { return milliwatts_; }
  [[nodiscard]] constexpr bool is_zero() const noexcept { return milliwatts_ == 0; }
  [[nodiscard]] constexpr bool is_ceiling() const noexcept {
    return milliwatts_ == kMaxThermalPowerMilliwatts;
  }

  [[nodiscard]] Result<ThermalPower> checked_add(ThermalPower other) const;
  [[nodiscard]] Result<ThermalPower> checked_sub(ThermalPower other) const;
  [[nodiscard]] Result<ThermalPower> checked_mul(std::uint64_t factor) const;

  /// Exact scaling by a ratio. part + remainder == *this always holds, so an
  /// apportionment that keeps every remainder still conserves the whole.
  [[nodiscard]] ScaledPower scaled_by(Ratio ratio) const;

  /// Sum of a sequence, refusing overflow past the domain ceiling.
  [[nodiscard]] static Result<ThermalPower> sum(const std::vector<ThermalPower>& values);

  friend constexpr bool operator==(ThermalPower lhs, ThermalPower rhs) noexcept {
    return lhs.milliwatts_ == rhs.milliwatts_;
  }
  friend struct ScaledPower;
  friend class ThermalDelta;
  friend ThermalDelta operator-(ThermalPower lhs, ThermalPower rhs) noexcept;
  friend constexpr bool operator!=(ThermalPower lhs, ThermalPower rhs) noexcept {
    return lhs.milliwatts_ != rhs.milliwatts_;
  }
  friend constexpr bool operator<(ThermalPower lhs, ThermalPower rhs) noexcept {
    return lhs.milliwatts_ < rhs.milliwatts_;
  }
  friend constexpr bool operator<=(ThermalPower lhs, ThermalPower rhs) noexcept {
    return lhs.milliwatts_ <= rhs.milliwatts_;
  }
  friend constexpr bool operator>(ThermalPower lhs, ThermalPower rhs) noexcept {
    return lhs.milliwatts_ > rhs.milliwatts_;
  }
  friend constexpr bool operator>=(ThermalPower lhs, ThermalPower rhs) noexcept {
    return lhs.milliwatts_ >= rhs.milliwatts_;
  }

 private:
  constexpr explicit ThermalPower(std::int64_t milliwatts) noexcept
      : milliwatts_(milliwatts) {}
  std::int64_t milliwatts_ = 0;
};

/// Result of an exact scaling: the part that was apportioned and the part that
/// was left over. Both are exact integers and they always re-sum to the whole.
struct ScaledPower {
  ThermalPower part;
  ThermalPower remainder;
};

class ThermalDelta;

/// The signed difference of two thermal powers, in milliwatts.
[[nodiscard]] ThermalDelta operator-(ThermalPower lhs, ThermalPower rhs) noexcept;

/// A signed thermal power difference in milliwatts. Residuals, shortfalls and
/// generation-to-generation changes are deltas, never quantities.
class ThermalDelta {
 public:
  ThermalDelta() = default;

  [[nodiscard]] static ThermalDelta of_milliwatts(std::int64_t value) noexcept;
  [[nodiscard]] static ThermalDelta zero() noexcept { return ThermalDelta(0); }
  [[nodiscard]] static constexpr Unit unit() noexcept { return Unit::Milliwatt; }

  [[nodiscard]] constexpr std::int64_t milliwatts() const noexcept { return milliwatts_; }
  [[nodiscard]] constexpr bool is_zero() const noexcept { return milliwatts_ == 0; }
  [[nodiscard]] constexpr bool is_negative() const noexcept { return milliwatts_ < 0; }

  [[nodiscard]] ThermalPower magnitude() const noexcept;
  /// The opposite delta. Refuses the one value with no positive counterpart.
  [[nodiscard]] Result<ThermalDelta> negated() const;

  friend constexpr bool operator==(ThermalDelta lhs, ThermalDelta rhs) noexcept {
    return lhs.milliwatts_ == rhs.milliwatts_;
  }
  friend constexpr bool operator!=(ThermalDelta lhs, ThermalDelta rhs) noexcept {
    return lhs.milliwatts_ != rhs.milliwatts_;
  }
  friend constexpr bool operator<(ThermalDelta lhs, ThermalDelta rhs) noexcept {
    return lhs.milliwatts_ < rhs.milliwatts_;
  }

 private:
  friend ThermalDelta operator-(ThermalPower lhs, ThermalPower rhs) noexcept;

  constexpr explicit ThermalDelta(std::int64_t milliwatts) noexcept
      : milliwatts_(milliwatts) {}
  std::int64_t milliwatts_ = 0;
};

/// An exact non-negative duration in milliseconds.
class DurationMs {
 public:
  DurationMs() = default;

  /// Precondition: value >= 0.
  [[nodiscard]] static DurationMs of_milliseconds(std::int64_t value) noexcept;
  [[nodiscard]] static Result<DurationMs> checked_milliseconds(std::int64_t value);
  [[nodiscard]] static constexpr DurationMs zero() noexcept { return DurationMs(0); }
  [[nodiscard]] static constexpr DurationMs forever() noexcept {
    return DurationMs(std::numeric_limits<std::int64_t>::max());
  }
  [[nodiscard]] static constexpr Unit unit() noexcept { return Unit::Millisecond; }

  [[nodiscard]] constexpr std::int64_t milliseconds() const noexcept { return millis_; }
  [[nodiscard]] constexpr bool is_zero() const noexcept { return millis_ == 0; }
  [[nodiscard]] constexpr bool is_forever() const noexcept {
    return millis_ == std::numeric_limits<std::int64_t>::max();
  }

  [[nodiscard]] Result<DurationMs> checked_add(DurationMs other) const;
  [[nodiscard]] Result<DurationMs> checked_sub(DurationMs other) const;

  friend constexpr bool operator==(DurationMs lhs, DurationMs rhs) noexcept {
    return lhs.millis_ == rhs.millis_;
  }
  friend constexpr bool operator!=(DurationMs lhs, DurationMs rhs) noexcept {
    return lhs.millis_ != rhs.millis_;
  }
  friend constexpr bool operator<(DurationMs lhs, DurationMs rhs) noexcept {
    return lhs.millis_ < rhs.millis_;
  }
  friend constexpr bool operator<=(DurationMs lhs, DurationMs rhs) noexcept {
    return lhs.millis_ <= rhs.millis_;
  }
  friend constexpr bool operator>(DurationMs lhs, DurationMs rhs) noexcept {
    return lhs.millis_ > rhs.millis_;
  }
  friend constexpr bool operator>=(DurationMs lhs, DurationMs rhs) noexcept {
    return lhs.millis_ >= rhs.millis_;
  }

 private:
  friend class Timestamp;

  constexpr explicit DurationMs(std::int64_t millis) noexcept : millis_(millis) {}
  std::int64_t millis_ = 0;
};

/// An exact instant in milliseconds since the Unix epoch, UTC.
class Timestamp {
 public:
  Timestamp() = default;

  /// Precondition: value >= 0.
  [[nodiscard]] static Timestamp of_unix_milliseconds(std::int64_t value) noexcept;
  [[nodiscard]] static Result<Timestamp> checked_unix_milliseconds(std::int64_t value);
  [[nodiscard]] static constexpr Timestamp epoch() noexcept { return Timestamp(0); }
  [[nodiscard]] static constexpr Unit unit() noexcept { return Unit::Millisecond; }

  [[nodiscard]] constexpr std::int64_t unix_milliseconds() const noexcept { return millis_; }

  [[nodiscard]] Result<Timestamp> checked_add(DurationMs duration) const;
  /// Duration from the earlier instant to this one. Underflows when this
  /// instant precedes the other; time does not run backwards silently.
  [[nodiscard]] Result<DurationMs> checked_since(Timestamp earlier) const;

  friend constexpr bool operator==(Timestamp lhs, Timestamp rhs) noexcept {
    return lhs.millis_ == rhs.millis_;
  }
  friend constexpr bool operator!=(Timestamp lhs, Timestamp rhs) noexcept {
    return lhs.millis_ != rhs.millis_;
  }
  friend constexpr bool operator<(Timestamp lhs, Timestamp rhs) noexcept {
    return lhs.millis_ < rhs.millis_;
  }
  friend constexpr bool operator<=(Timestamp lhs, Timestamp rhs) noexcept {
    return lhs.millis_ <= rhs.millis_;
  }
  friend constexpr bool operator>(Timestamp lhs, Timestamp rhs) noexcept {
    return lhs.millis_ > rhs.millis_;
  }
  friend constexpr bool operator>=(Timestamp lhs, Timestamp rhs) noexcept {
    return lhs.millis_ >= rhs.millis_;
  }

  friend Result<Timestamp> operator+(Timestamp lhs, DurationMs rhs) noexcept;
  friend class Timestamp;

 private:
  constexpr explicit Timestamp(std::int64_t millis) noexcept : millis_(millis) {}
  std::int64_t millis_ = 0;
};

}  // namespace cooling_capacity_accounting

#endif  // COOLING_CAPACITY_ACCOUNTING_UNITS_HPP
