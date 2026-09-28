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

#ifndef COOLING_CAPACITY_ACCOUNTING_CLOCK_HPP
#define COOLING_CAPACITY_ACCOUNTING_CLOCK_HPP

#include <string>
#include <string_view>

#include "cooling_capacity_accounting/errors.hpp"
#include "cooling_capacity_accounting/units.hpp"

namespace cooling_capacity_accounting {

/// Time source. The library never reads the wall clock on its own: every
/// freshness comparison is made against a time the caller supplied, so an
/// accounting result is reproducible.
class Clock {
 public:
  Clock() = default;
  Clock(const Clock&) = delete;
  Clock& operator=(const Clock&) = delete;
  virtual ~Clock() = default;

  [[nodiscard]] virtual Timestamp now() const = 0;
};

/// The host clock, in milliseconds since the Unix epoch, UTC.
class SystemClock final : public Clock {
 public:
  [[nodiscard]] Timestamp now() const override;
};

/// A clock that only moves when the caller moves it.
class ManualClock final : public Clock {
 public:
  ManualClock() = default;
  explicit ManualClock(Timestamp start) : now_(start) {}

  [[nodiscard]] Timestamp now() const override { return now_; }
  [[nodiscard]] Result<void> advance(DurationMs duration);
  [[nodiscard]] Result<void> set(Timestamp instant);

 private:
  Timestamp now_;
};

/// Canonical UTC rendering: "YYYY-MM-DDTHH:MM:SS.mmmZ".
[[nodiscard]] std::string format_utc(Timestamp instant);
/// Strict parse of the canonical rendering. Only 'Z' is accepted as the zone.
[[nodiscard]] Result<Timestamp> parse_utc(std::string_view text);
/// Canonical rendering of a duration: "<n>ms" or "<n>s" when exact.
[[nodiscard]] std::string format_duration(DurationMs duration);

/// Seconds since the Unix epoch, or 0 when the instant precedes it.
[[nodiscard]] std::int64_t utc_seconds(Timestamp instant) noexcept;

}  // namespace cooling_capacity_accounting

#endif  // COOLING_CAPACITY_ACCOUNTING_CLOCK_HPP
