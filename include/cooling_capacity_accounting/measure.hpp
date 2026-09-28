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

#ifndef COOLING_CAPACITY_ACCOUNTING_MEASURE_HPP
#define COOLING_CAPACITY_ACCOUNTING_MEASURE_HPP

#include <cstdint>
#include <string_view>
#include <utility>

#include "cooling_capacity_accounting/errors.hpp"
#include "cooling_capacity_accounting/text.hpp"

namespace cooling_capacity_accounting {

/// State of a measured or declared quantity.
enum class MeasureState : std::int32_t {
  /// The quantity is established and usable.
  Known = 0,
  /// The quantity cannot be established. It is *not* zero, and it is never
  /// treated as zero.
  Unknown = 1,
  /// The quantity does not exist in this configuration at all: a definite
  /// negative rather than a missing fact.
  Unsupported = 2,
};

[[nodiscard]] std::string_view measure_state_name(MeasureState state) noexcept;
[[nodiscard]] Result<MeasureState> parse_measure_state(std::string_view text);

/// Machine-readable explanation of why a quantity is not known.
enum class MeasureReason : std::int32_t {
  None = 0,
  NotMeasured = 1,
  SensorAbsent = 2,
  SensorFault = 3,
  EvidenceMissing = 4,
  EvidenceStale = 5,
  EvidenceConflicting = 6,
  EvidenceSuperseded = 7,
  TopologyUnknown = 8,
  InventoryUnknown = 9,
  VendorSilent = 10,
  NotCommissioned = 11,
  OutOfScope = 12,
  Other = 99,
};

[[nodiscard]] std::string_view measure_reason_name(MeasureReason reason) noexcept;
[[nodiscard]] Result<MeasureReason> parse_measure_reason(std::string_view text);

/// A quantity that is either exactly known, unknown or unsupported. The three
/// states are distinct and the type refuses to collapse them: there is no
/// implicit conversion to T and no default that reads as zero.
template <typename T>
class Measure {
 public:
  Measure() = default;

  [[nodiscard]] static Measure known(T value) {
    Measure result;
    result.state_ = MeasureState::Known;
    result.reason_ = MeasureReason::None;
    result.value_ = std::move(value);
    return result;
  }
  [[nodiscard]] static Measure unknown(MeasureReason reason, BoundedText explanation) {
    Measure result;
    result.state_ = MeasureState::Unknown;
    result.reason_ = reason;
    result.explanation_ = std::move(explanation);
    return result;
  }
  [[nodiscard]] static Measure unsupported(MeasureReason reason, BoundedText explanation) {
    Measure result;
    result.state_ = MeasureState::Unsupported;
    result.reason_ = reason;
    result.explanation_ = std::move(explanation);
    return result;
  }

  [[nodiscard]] MeasureState state() const noexcept { return state_; }
  [[nodiscard]] bool is_known() const noexcept { return state_ == MeasureState::Known; }
  [[nodiscard]] bool is_unknown() const noexcept { return state_ == MeasureState::Unknown; }
  [[nodiscard]] bool is_unsupported() const noexcept {
    return state_ == MeasureState::Unsupported;
  }
  [[nodiscard]] MeasureReason reason() const noexcept { return reason_; }
  [[nodiscard]] const BoundedText& explanation() const noexcept { return explanation_; }

  /// Precondition: is_known().
  [[nodiscard]] const T& value() const noexcept { return value_; }
  /// The known value, or the caller's fallback without ever inventing one.
  [[nodiscard]] T value_or(T fallback) const {
    return is_known() ? value_ : std::move(fallback);
  }

  friend bool operator==(const Measure& lhs, const Measure& rhs) {
    if (lhs.state_ != rhs.state_) {
      return false;
    }
    if (lhs.state_ != MeasureState::Known) {
      return lhs.reason_ == rhs.reason_ && lhs.explanation_ == rhs.explanation_;
    }
    return lhs.value_ == rhs.value_;
  }
  friend bool operator!=(const Measure& lhs, const Measure& rhs) {
    return !(lhs == rhs);
  }

 private:
  MeasureState state_ = MeasureState::Unknown;
  MeasureReason reason_ = MeasureReason::NotMeasured;
  T value_{};
  BoundedText explanation_;
};

}  // namespace cooling_capacity_accounting

#endif  // COOLING_CAPACITY_ACCOUNTING_MEASURE_HPP
