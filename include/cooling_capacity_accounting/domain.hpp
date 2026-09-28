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

#ifndef COOLING_CAPACITY_ACCOUNTING_DOMAIN_HPP
#define COOLING_CAPACITY_ACCOUNTING_DOMAIN_HPP

#include <cstdint>
#include <string_view>

#include "cooling_capacity_accounting/errors.hpp"
#include "cooling_capacity_accounting/ids.hpp"
#include "cooling_capacity_accounting/text.hpp"

namespace cooling_capacity_accounting {

/// Thermal transport medium. Capacity in one medium is never spent, summed or
/// compared against capacity in the other.
enum class Medium : std::int32_t {
  Air = 1,
  Liquid = 2,
};

[[nodiscard]] std::string_view medium_name(Medium medium) noexcept;
[[nodiscard]] Result<Medium> parse_medium(std::string_view text);

/// Equipment classes are accounting buckets that a facility declares. The kind
/// is descriptive; the class identifier is what contributions and rollups use.
enum class EquipmentClassKind : std::int32_t {
  ComputerRoomAirHandler = 1,
  ComputerRoomAirConditioner = 2,
  Chiller = 3,
  CoolantDistributionUnit = 4,
  CoolingTower = 5,
  DryCooler = 6,
  Pump = 7,
  ImmersionTank = 8,
  RearDoorHeatExchanger = 9,
  Economizer = 10,
  Other = 99,
};

[[nodiscard]] std::string_view equipment_class_kind_name(EquipmentClassKind kind) noexcept;
[[nodiscard]] Result<EquipmentClassKind> parse_equipment_class_kind(std::string_view text);

/// A declared class of thermal-removal equipment.
struct EquipmentClass {
  EquipmentClassId id;
  EquipmentClassKind kind = EquipmentClassKind::Other;
  Medium medium = Medium::Air;
  BoundedText label;
  /// When false, quantities of this class are accounted but never counted as
  /// installed cooling capacity (for example a monitoring-only device).
  bool contributes_to_installed = true;
};

/// Kind of accounting scope. Scopes nest: a site contains zones and loops, a
/// zone contains loops and class bands, and a loop contains class bands.
enum class ScopeKind : std::int32_t {
  Site = 1,
  Zone = 2,
  Loop = 3,
  ClassBand = 4,
};

[[nodiscard]] std::string_view scope_kind_name(ScopeKind kind) noexcept;
[[nodiscard]] Result<ScopeKind> parse_scope_kind(std::string_view text);
/// Containment rules: a zone belongs to a site, a loop to a zone or directly to
/// a site, and a class band to a loop or a zone. A site is always a root, so the
/// nesting relation is acyclic and every scope reaches exactly one site.
[[nodiscard]] bool scope_kind_may_nest_under(ScopeKind child, ScopeKind parent) noexcept;
/// Depth of a scope kind below a site, used for the nesting bound.
[[nodiscard]] std::uint32_t scope_kind_depth(ScopeKind kind) noexcept;

/// Redundancy ladder. The class states how many independent copies of the
/// protected capability must exist and how many declared failure domains may be
/// lost at once.
enum class RedundancyClass : std::int32_t {
  N = 0,
  NPlus1 = 1,
  NPlus2 = 2,
  TwoN = 3,
  TwoNPlus1 = 4,
  /// 2N+2, which is the same as 2(N+1).
  TwoNPlus2 = 5,
};

[[nodiscard]] std::string_view redundancy_class_name(RedundancyClass klass) noexcept;
/// Parses "N", "N+1", "N+2", "2N", "2N+1", "2N+2" and the equivalent "2(N+1)".
[[nodiscard]] Result<RedundancyClass> parse_redundancy_class(std::string_view text);
/// Number of complete independent copies the class requires: 1 or 2.
[[nodiscard]] std::uint32_t redundancy_copies(RedundancyClass klass) noexcept;
/// Number of declared failure domains the class must survive at once: 0, 1 or 2.
[[nodiscard]] std::uint32_t redundancy_failure_domains(RedundancyClass klass) noexcept;

/// An explicitly declared shared-fate boundary: equipment listed in the same
/// domain is asserted to fail together, and that assertion carries evidence.
///
/// This library never infers independence. Two devices with different
/// identifiers are not independent unless a domain declaration says so.
struct IndependenceDomain {
  IndependenceDomainId id;
  BoundedText label;
  /// Evidence for the declaration itself. An empty reference leaves every
  /// obligation that depends on this domain unresolved.
  EvidenceId evidence;
  ScopeId scope;
  TopologyGeneration topology_generation;
  ControlPlaneEpoch epoch;
};

/// Result of checking whether a declaration is usable.
enum class DomainDeclarationStatus : std::int32_t {
  /// The domain is declared and carries evidence for the declaration.
  Evidenced = 0,
  /// The domain is named but no evidence backs the declaration.
  Unevidenced = 1,
};

[[nodiscard]] std::string_view domain_declaration_status_name(
    DomainDeclarationStatus status) noexcept;

}  // namespace cooling_capacity_accounting

#endif  // COOLING_CAPACITY_ACCOUNTING_DOMAIN_HPP
