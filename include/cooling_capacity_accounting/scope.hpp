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

#ifndef COOLING_CAPACITY_ACCOUNTING_SCOPE_HPP
#define COOLING_CAPACITY_ACCOUNTING_SCOPE_HPP

#include <optional>
#include <string_view>
#include <vector>

#include "cooling_capacity_accounting/domain.hpp"
#include "cooling_capacity_accounting/ids.hpp"
#include "cooling_capacity_accounting/measure.hpp"
#include "cooling_capacity_accounting/text.hpp"
#include "cooling_capacity_accounting/units.hpp"

namespace cooling_capacity_accounting {

/// One node of the accounting hierarchy. Sites contain zones, zones contain
/// loops, and a loop may contain per-class bands. Nesting is a containment
/// relation, not a claim about airflow, piping or control.
struct AccountingScope {
  ScopeId id;
  ScopeKind kind = ScopeKind::Site;
  /// Absent for a site, required for every other kind.
  std::optional<ScopeId> parent;
  BoundedText label;
  /// The medium this scope accounts. A loop and a class band fix one medium.
  Medium medium = Medium::Air;
  /// Equipment classes this scope declares. A class band declares exactly one.
  std::vector<EquipmentClassId> classes;
  TopologyGeneration topology_generation;
  ControlPlaneEpoch epoch;
  /// Evidence-backed external declaration of this scope's own installed total,
  /// used only to compute the unexplained residual of the scope. It never
  /// replaces the constituent contributions.
  Measure<ThermalPower> declared_installed_total;
  std::vector<EvidenceId> declared_total_evidence;

  [[nodiscard]] bool is_root() const noexcept { return !parent.has_value(); }
};

/// An external, evidence-backed declaration of installed capacity for a scope,
/// optionally narrowed to one equipment class. Declarations are compared with
/// the constituent accounting; the difference is preserved as a residual and is
/// never used to adjust a contribution.
struct ManifestDeclaration {
  ManifestId id;
  ScopeId scope;
  /// When set, the declaration covers only this equipment class inside the
  /// scope, and its residual is reported in the class rollup rather than in the
  /// scope residual.
  std::optional<EquipmentClassId> equipment_class;
  Medium medium = Medium::Air;
  ThermalPower declared_installed;
  EvidenceId evidence;
  Timestamp observed_at;
  EvidenceBinding binding;
};

}  // namespace cooling_capacity_accounting

#endif  // COOLING_CAPACITY_ACCOUNTING_SCOPE_HPP
