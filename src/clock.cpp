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

#include "cooling_capacity_accounting/clock.hpp"

#include <chrono>
#include <cstdint>
#include <string>

#include "cooling_capacity_accounting/text.hpp"

namespace cooling_capacity_accounting {
namespace {

constexpr std::int64_t kMillisecondsPerDay = 86'400'000;
constexpr std::int64_t kMillisecondsPerSecond = 1'000;
constexpr std::int64_t kMillisecondsPerMinute = 60'000;
constexpr std::int64_t kMillisecondsPerHour = 3'600'000;

/// Days from 1970-01-01 to y-m-d, valid for the whole proleptic Gregorian range
/// the millisecond domain can reach.
[[nodiscard]] std::int64_t days_from_civil(std::int64_t year, unsigned month,
                                           unsigned day) noexcept {
  year -= month <= 2U ? 1 : 0;
  const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
  const unsigned year_of_era = static_cast<unsigned>(year - era * 400);
  const unsigned month_prime = (month > 2U) ? (month - 3U) : (month + 9U);
  const unsigned day_of_year = (153U * month_prime + 2U) / 5U + day - 1U;
  const unsigned day_of_era =
      year_of_era * 365U + year_of_era / 4U - year_of_era / 100U + day_of_year;
  return era * 146097 + static_cast<std::int64_t>(day_of_era) - 719468;
}

struct CivilDate {
  std::int64_t year;
  unsigned month;
  unsigned day;
};

[[nodiscard]] CivilDate civil_from_days(std::int64_t days) noexcept {
  days += 719468;
  const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const unsigned day_of_era = static_cast<unsigned>(days - era * 146097);
  const unsigned year_of_era = (day_of_era - day_of_era / 1460U + day_of_era / 36524U -
                                day_of_era / 146096U) / 365U;
  std::int64_t year = static_cast<std::int64_t>(year_of_era) + era * 400;
  const unsigned day_of_year =
      day_of_era - (365U * year_of_era + year_of_era / 4U - year_of_era / 100U);
  const unsigned month_prime = (5U * day_of_year + 2U) / 153U;
  const unsigned day = day_of_year - (153U * month_prime + 2U) / 5U + 1U;
  const unsigned month = (month_prime < 10U) ? (month_prime + 3U) : (month_prime - 9U);
  year += month <= 2U ? 1 : 0;
  return CivilDate{year, month, day};
}

void append_padded(std::string& text, std::int64_t value, std::size_t width) {
  std::string digits = std::to_string(value < 0 ? -value : value);
  while (digits.size() < width) {
    digits.insert(digits.begin(), '0');
  }
  if (value < 0) {
    text.push_back('-');
  }
  text += digits;
}

[[nodiscard]] Result<std::uint32_t> parse_fixed(std::string_view text,
                                                std::size_t offset,
                                                std::size_t length,
                                                const char* field) {
  if (offset + length > text.size()) {
    return Error::of(ErrorCode::InvalidTimestampText, "the instant is too short")
        .with_detail(std::string("field ") + field);
  }
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < length; ++index) {
    const char byte = text[offset + index];
    if (!is_ascii_digit(byte)) {
      return Error::of(ErrorCode::InvalidTimestampText,
                       "the instant must contain digits in this position")
          .with_detail(std::string("field ") + field);
    }
    value = value * 10U + static_cast<std::uint32_t>(byte - '0');
  }
  return value;
}

constexpr std::size_t kCanonicalTimestampLength = 24;

}  // namespace

Timestamp SystemClock::now() const {
  const auto since_epoch = std::chrono::system_clock::now().time_since_epoch();
  const auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch).count();
  if (millis < 0) {
    return Timestamp::epoch();
  }
  return Timestamp::of_unix_milliseconds(static_cast<std::int64_t>(millis));
}

Result<void> ManualClock::advance(DurationMs duration) {
  CCA_TRY_ASSIGN(next, now_.checked_add(duration));
  now_ = next;
  return Ok{};
}

Result<void> ManualClock::set(Timestamp instant) {
  now_ = instant;
  return Ok{};
}

std::string format_utc(Timestamp instant) {
  const std::int64_t millis = instant.unix_milliseconds();
  const std::int64_t days = millis / kMillisecondsPerDay;
  std::int64_t remainder = millis % kMillisecondsPerDay;
  const CivilDate date = civil_from_days(days);

  const std::int64_t hours = remainder / kMillisecondsPerHour;
  remainder %= kMillisecondsPerHour;
  const std::int64_t minutes = remainder / kMillisecondsPerMinute;
  remainder %= kMillisecondsPerMinute;
  const std::int64_t seconds = remainder / kMillisecondsPerSecond;
  const std::int64_t millis_of_second = remainder % kMillisecondsPerSecond;

  std::string text;
  text.reserve(kCanonicalTimestampLength);
  append_padded(text, date.year, 4);
  text.push_back('-');
  append_padded(text, date.month, 2);
  text.push_back('-');
  append_padded(text, date.day, 2);
  text.push_back('T');
  append_padded(text, hours, 2);
  text.push_back(':');
  append_padded(text, minutes, 2);
  text.push_back(':');
  append_padded(text, seconds, 2);
  text.push_back('.');
  append_padded(text, millis_of_second, 3);
  text.push_back('Z');
  return text;
}

Result<Timestamp> parse_utc(std::string_view text) {
  if (text.size() != kCanonicalTimestampLength) {
    return Error::of(ErrorCode::InvalidTimestampText,
                     "an instant is exactly YYYY-MM-DDTHH:MM:SS.mmmZ")
        .with_subject(std::string(text));
  }
  if (text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' ||
      text[16] != ':' || text[19] != '.' || text[23] != 'Z') {
    return Error::of(ErrorCode::InvalidTimestampText,
                     "an instant is exactly YYYY-MM-DDTHH:MM:SS.mmmZ")
        .with_subject(std::string(text));
  }
  CCA_TRY_ASSIGN(year, parse_fixed(text, 0, 4, "year"));
  CCA_TRY_ASSIGN(month, parse_fixed(text, 5, 2, "month"));
  CCA_TRY_ASSIGN(day, parse_fixed(text, 8, 2, "day"));
  CCA_TRY_ASSIGN(hours, parse_fixed(text, 11, 2, "hour"));
  CCA_TRY_ASSIGN(minutes, parse_fixed(text, 14, 2, "minute"));
  CCA_TRY_ASSIGN(seconds, parse_fixed(text, 17, 2, "second"));
  CCA_TRY_ASSIGN(millis, parse_fixed(text, 20, 3, "millisecond"));

  if (month < 1U || month > 12U) {
    return Error::of(ErrorCode::InvalidTimestampText, "the month is out of range")
        .with_subject(std::string(text));
  }
  if (day < 1U || day > 31U) {
    return Error::of(ErrorCode::InvalidTimestampText, "the day is out of range")
        .with_subject(std::string(text));
  }
  if (hours > 23U || minutes > 59U || seconds > 59U) {
    return Error::of(ErrorCode::InvalidTimestampText, "the time of day is out of range")
        .with_subject(std::string(text));
  }
  const std::int64_t days =
      days_from_civil(static_cast<std::int64_t>(year), month, day);
  const std::int64_t total =
      days * kMillisecondsPerDay + static_cast<std::int64_t>(hours) * kMillisecondsPerHour +
      static_cast<std::int64_t>(minutes) * kMillisecondsPerMinute +
      static_cast<std::int64_t>(seconds) * kMillisecondsPerSecond +
      static_cast<std::int64_t>(millis);
  // Round-tripping is the check that the civil date was a real calendar date.
  if (format_utc(Timestamp::of_unix_milliseconds(total)) != std::string(text)) {
    return Error::of(ErrorCode::InvalidTimestampText,
                     "the instant is not a real calendar date")
        .with_subject(std::string(text));
  }
  return Timestamp::of_unix_milliseconds(total);
}

std::string format_duration(DurationMs duration) {
  const std::int64_t millis = duration.milliseconds();
  if (millis != 0 && millis % 1'000 == 0) {
    return to_decimal(millis / 1'000) + "s";
  }
  return to_decimal(millis) + "ms";
}

std::int64_t utc_seconds(Timestamp instant) noexcept {
  return instant.unix_milliseconds() / kMillisecondsPerSecond;
}

}  // namespace cooling_capacity_accounting
