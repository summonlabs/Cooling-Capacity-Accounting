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

#include "cooling_capacity_accounting/contribution.hpp"

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

constexpr std::array<EnumName, 6> kContributionClassNames{{
    {0, "Unspecified"},
    {1, "Additive"},
    {2, "Substitutive"},
    {3, "Redundant"},
    {4, "ReserveOnly"},
    {5, "MutuallyExclusive"},
}};

constexpr std::array<EnumName, 5> kServiceStateNames{{
    {0, "Unspecified"},
    {1, "InService"},
    {2, "OutOfService"},
    {3, "Degraded"},
    {4, "Unknown"},
}};

constexpr std::array<EnumName, 2> kDerateKindNames{
    {{1, "Factor"}, {2, "Absolute"}}};

}  // namespace

std::string_view contribution_class_name(ContributionClass klass) noexcept {
  return lookup(kContributionClassNames, klass);
}

Result<ContributionClass> parse_contribution_class(std::string_view text) {
  return lookup_parse<ContributionClass>(kContributionClassNames, text,
                                "contribution class");
}

bool contribution_class_requires_group(ContributionClass klass) noexcept {
  switch (klass) {
    case ContributionClass::Substitutive:
    case ContributionClass::Redundant:
    case ContributionClass::MutuallyExclusive:
      return true;
    case ContributionClass::Additive:
    case ContributionClass::ReserveOnly:
    case ContributionClass::Unspecified:
      return false;
  }
  return false;
}

std::string_view service_state_name(ServiceState state) noexcept {
  return lookup(kServiceStateNames, state);
}

Result<ServiceState> parse_service_state(std::string_view text) {
  return lookup_parse<ServiceState>(kServiceStateNames, text, "service state");
}

std::string_view derate_kind_name(DerateKind kind) noexcept {
  return lookup(kDerateKindNames, kind);
}

Result<DerateKind> parse_derate_kind(std::string_view text) {
  return lookup_parse<DerateKind>(kDerateKindNames, text, "derate kind");
}

}  // namespace cooling_capacity_accounting
