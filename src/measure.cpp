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

#include "cooling_capacity_accounting/measure.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace cooling_capacity_accounting {
namespace {

struct EnumName {
  std::int32_t value;
  std::string_view name;
};

template <std::size_t N, typename Enum>
[[nodiscard]] std::string_view lookup(const std::array<EnumName, N>& table,
                                      Enum value) noexcept {
  for (const EnumName& entry : table) {
    if (entry.value == static_cast<std::int32_t>(value)) {
      return entry.name;
    }
  }
  return "Unknown";
}

template <typename Enum, std::size_t N>
[[nodiscard]] Result<Enum> lookup_parse(const std::array<EnumName, N>& table,
                                        std::string_view text, const char* what) {
  for (const EnumName& entry : table) {
    if (entry.name == text) {
      return static_cast<Enum>(entry.value);
    }
  }
  return Error::of(ErrorCode::ImpossibleEnumValue, std::string("unknown ") + what)
      .with_subject(std::string(text));
}

constexpr std::array<EnumName, 3> kMeasureStateNames{
    {{0, "Known"}, {1, "Unknown"}, {2, "Unsupported"}}};

constexpr std::array<EnumName, 14> kMeasureReasonNames{{
    {0, "None"},
    {1, "NotMeasured"},
    {2, "SensorAbsent"},
    {3, "SensorFault"},
    {4, "EvidenceMissing"},
    {5, "EvidenceStale"},
    {6, "EvidenceConflicting"},
    {7, "EvidenceSuperseded"},
    {8, "TopologyUnknown"},
    {9, "InventoryUnknown"},
    {10, "VendorSilent"},
    {11, "NotCommissioned"},
    {12, "OutOfScope"},
    {99, "Other"},
}};

}  // namespace

std::string_view measure_state_name(MeasureState state) noexcept {
  return lookup(kMeasureStateNames, state);
}

Result<MeasureState> parse_measure_state(std::string_view text) {
  return lookup_parse<MeasureState>(kMeasureStateNames, text, "measure state");
}

std::string_view measure_reason_name(MeasureReason reason) noexcept {
  return lookup(kMeasureReasonNames, reason);
}

Result<MeasureReason> parse_measure_reason(std::string_view text) {
  return lookup_parse<MeasureReason>(kMeasureReasonNames, text, "measure reason");
}

}  // namespace cooling_capacity_accounting
