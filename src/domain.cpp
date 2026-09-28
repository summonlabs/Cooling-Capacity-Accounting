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

#include "cooling_capacity_accounting/domain.hpp"

#include <array>
#include <cstddef>
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
  return Error::of(ErrorCode::ImpossibleEnumValue,
                   std::string("unknown ") + what)
      .with_subject(std::string(text));
}

constexpr std::array<EnumName, 2> kMediumNames{{{1, "Air"}, {2, "Liquid"}}};

constexpr std::array<EnumName, 11> kEquipmentClassKindNames{{
    {1, "ComputerRoomAirHandler"},
    {2, "ComputerRoomAirConditioner"},
    {3, "Chiller"},
    {4, "CoolantDistributionUnit"},
    {5, "CoolingTower"},
    {6, "DryCooler"},
    {7, "Pump"},
    {8, "ImmersionTank"},
    {9, "RearDoorHeatExchanger"},
    {10, "Economizer"},
    {99, "Other"},
}};

constexpr std::array<EnumName, 4> kScopeKindNames{
    {{1, "Site"}, {2, "Zone"}, {3, "Loop"}, {4, "ClassBand"}}};

constexpr std::array<EnumName, 6> kRedundancyClassNames{{
    {0, "N"}, {1, "N+1"}, {2, "N+2"}, {3, "2N"}, {4, "2N+1"}, {5, "2N+2"},
}};

constexpr std::array<EnumName, 2> kDomainDeclarationStatusNames{
    {{0, "Evidenced"}, {1, "Unevidenced"}}};

}  // namespace

std::string_view medium_name(Medium medium) noexcept {
  return lookup(kMediumNames, medium);
}

Result<Medium> parse_medium(std::string_view text) {
  return lookup_parse<Medium>(kMediumNames, text, "medium");
}

std::string_view equipment_class_kind_name(EquipmentClassKind kind) noexcept {
  return lookup(kEquipmentClassKindNames, kind);
}

Result<EquipmentClassKind> parse_equipment_class_kind(std::string_view text) {
  return lookup_parse<EquipmentClassKind>(kEquipmentClassKindNames, text,
                            "equipment class kind");
}

std::string_view scope_kind_name(ScopeKind kind) noexcept {
  return lookup(kScopeKindNames, kind);
}

Result<ScopeKind> parse_scope_kind(std::string_view text) {
  return lookup_parse<ScopeKind>(kScopeKindNames, text, "scope kind");
}

bool scope_kind_may_nest_under(ScopeKind child, ScopeKind parent) noexcept {
  switch (child) {
    case ScopeKind::Site:
      // A site is a root: it has no parent and no sibling site contains it.
      return false;
    case ScopeKind::Zone:
      return parent == ScopeKind::Site;
    case ScopeKind::Loop:
      // A loop belongs to a zone, or directly to the site when the facility has
      // no zoning between the site and the loop.
      return parent == ScopeKind::Zone || parent == ScopeKind::Site;
    case ScopeKind::ClassBand:
      // A class band narrows one equipment class inside a loop or a zone.
      return parent == ScopeKind::Loop || parent == ScopeKind::Zone;
  }
  return false;
}

std::uint32_t scope_kind_depth(ScopeKind kind) noexcept {
  switch (kind) {
    case ScopeKind::Site:
      return 0;
    case ScopeKind::Zone:
      return 1;
    case ScopeKind::Loop:
      return 2;
    case ScopeKind::ClassBand:
      return 3;
  }
  return 0;
}

std::string_view redundancy_class_name(RedundancyClass klass) noexcept {
  return lookup(kRedundancyClassNames, klass);
}

Result<RedundancyClass> parse_redundancy_class(std::string_view text) {
  if (text == "2(N+1)") {
    return RedundancyClass::TwoNPlus2;
  }
  if (text == "2(n+1)") {
    return RedundancyClass::TwoNPlus2;
  }
  return lookup_parse<RedundancyClass>(kRedundancyClassNames, text, "redundancy class");
}

std::uint32_t redundancy_copies(RedundancyClass klass) noexcept {
  switch (klass) {
    case RedundancyClass::N:
    case RedundancyClass::NPlus1:
    case RedundancyClass::NPlus2:
      return 1;
    case RedundancyClass::TwoN:
    case RedundancyClass::TwoNPlus1:
    case RedundancyClass::TwoNPlus2:
      return 2;
  }
  return 1;
}

std::uint32_t redundancy_failure_domains(RedundancyClass klass) noexcept {
  switch (klass) {
    case RedundancyClass::N:
    case RedundancyClass::TwoN:
      return 0;
    case RedundancyClass::NPlus1:
    case RedundancyClass::TwoNPlus1:
      return 1;
    case RedundancyClass::NPlus2:
    case RedundancyClass::TwoNPlus2:
      return 2;
  }
  return 0;
}

std::string_view domain_declaration_status_name(
    DomainDeclarationStatus status) noexcept {
  return lookup(kDomainDeclarationStatusNames, status);
}

}  // namespace cooling_capacity_accounting
