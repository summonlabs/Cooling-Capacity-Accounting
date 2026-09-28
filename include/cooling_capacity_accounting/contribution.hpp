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

#ifndef COOLING_CAPACITY_ACCOUNTING_CONTRIBUTION_HPP
#define COOLING_CAPACITY_ACCOUNTING_CONTRIBUTION_HPP

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "cooling_capacity_accounting/domain.hpp"
#include "cooling_capacity_accounting/evidence.hpp"
#include "cooling_capacity_accounting/ids.hpp"
#include "cooling_capacity_accounting/limits.hpp"
#include "cooling_capacity_accounting/measure.hpp"
#include "cooling_capacity_accounting/text.hpp"
#include "cooling_capacity_accounting/units.hpp"

namespace cooling_capacity_accounting {

/// How a contribution combines with its peers.
///
/// * Additive - the contribution stands on its own. Two additive contributions
///   in one scope both count.
/// * Substitutive - the members of its group provide the same service and only
///   required_concurrent of them may be counted at once. The rest are accounted
///   as withheld spare, never double counted.
/// * Redundant - the members of its group together satisfy a redundancy class.
///   All in-service members count as installed, and the reserve obligation
///   derived from the class withholds part of the pool.
/// * ReserveOnly - the contribution is reserve by construction. It is accounted
///   in full and is never allocatable, whatever else is true.
/// * MutuallyExclusive - members of its group exclude each other. Exactly one
///   member (highest priority, then lowest identifier) is counted; the others
///   are withheld with the mutual-exclusion reason.
enum class ContributionClass : std::int32_t {
  /// The record does not state how it combines. A contribution in this state is
  /// refused by the accounting policy: combining is not a default.
  Unspecified = 0,
  Additive = 1,
  Substitutive = 2,
  Redundant = 3,
  ReserveOnly = 4,
  MutuallyExclusive = 5,
};

[[nodiscard]] std::string_view contribution_class_name(ContributionClass klass) noexcept;
[[nodiscard]] Result<ContributionClass> parse_contribution_class(std::string_view text);
/// True when the class requires membership of a contribution group.
[[nodiscard]] bool contribution_class_requires_group(ContributionClass klass) noexcept;

/// Service state declared for a contribution.
enum class ServiceState : std::int32_t {
  /// The record does not state the service state. It is not the same as
  /// Unknown, which is a positive declaration that the state could not be
  /// established.
  Unspecified = 0,
  InService = 1,
  OutOfService = 2,
  /// In service at a reduced quantity; at least one derate factor applies.
  Degraded = 3,
  Unknown = 4,
};

[[nodiscard]] std::string_view service_state_name(ServiceState state) noexcept;
[[nodiscard]] Result<ServiceState> parse_service_state(std::string_view text);

/// How a derate reduces the installed quantity.
enum class DerateKind : std::int32_t {
  /// A ratio in parts per million applied multiplicatively.
  Factor = 1,
  /// An absolute reduction in milliwatts applied before the factors.
  Absolute = 2,
};

[[nodiscard]] std::string_view derate_kind_name(DerateKind kind) noexcept;
[[nodiscard]] Result<DerateKind> parse_derate_kind(std::string_view text);

/// One named, evidenced reduction of a contribution's installed quantity.
struct DerateFactor {
  DerateId id;
  DerateKind kind = DerateKind::Factor;
  /// Used when kind is Factor. Must be 1.0 for an absolute derate.
  /// The default is "no reduction": a derate that is never stated reduces
  /// nothing, rather than silently reducing everything.
  Ratio factor = Ratio::one();
  /// Used when kind is Absolute. Must be zero for a factor derate.
  ThermalPower absolute;
  EvidenceId evidence;
  BoundedText label;

  DerateFactor() = default;
  DerateFactor(DerateId derate_id, Ratio ppm)
      : id(std::move(derate_id)), kind(DerateKind::Factor), factor(ppm) {}
  DerateFactor(DerateId derate_id, ThermalPower reduction)
      : id(std::move(derate_id)), kind(DerateKind::Absolute), factor(Ratio::one()),
        absolute(reduction) {}
};

/// One entry of an apportionment: how much of a shared contribution lands in
/// another scope.
struct ApportionmentShare {
  ScopeId target;
  /// The default is 0 ppm: a share that is never stated apportions nothing to
  /// its target rather than everything.
  Ratio share = Ratio::none();
};

/// How a contribution's accounted mass is distributed across scopes.
///
/// Exclusive keeps everything in the home scope. Apportioned sends the declared
/// shares to other scopes and keeps the unapportioned remainder in the home
/// scope, where it is reported as an unapportioned residual. Shares may never
/// sum above 1.0, and no share may target the home scope.
struct Sharing {
  enum class Kind : std::int32_t {
    Exclusive = 1,
    Apportioned = 2,
  };

  Kind kind = Kind::Exclusive;
  std::vector<ApportionmentShare> shares;
};

/// A declared group of contributions that share a service obligation.
struct ContributionGroup {
  ContributionGroupId id;
  ScopeId scope;
  ContributionClass classification = ContributionClass::Additive;
  Medium medium = Medium::Air;
  /// How many members must be simultaneously available to serve the protected
  /// quantity. Zero means "not declared".
  std::uint32_t required_concurrent = 0;
  RedundancyClass redundancy = RedundancyClass::N;
  /// The protected quantity the redundancy class applies to. Absent evidence
  /// leaves the reserve obligation unresolved rather than guessed.
  Measure<ThermalPower> protected_quantity;
  EvidenceId protected_quantity_evidence;
  /// Declared shared-fate boundaries. Independence is never inferred.
  std::vector<IndependenceDomainId> independence_domains;
  EvidenceBinding binding;
};

/// A single accounted contributor to cooling capacity in one scope.
struct Contribution {
  ContributionId id;
  ScopeId home_scope;
  EquipmentId equipment;
  EquipmentClassId equipment_class;
  Medium medium = Medium::Air;
  ContributionClass classification = ContributionClass::Additive;
  /// The installed (nameplate) quantity. This is evidence, never an assumption:
  /// an unknown nameplate stays unknown.
  Measure<ThermalPower> installed;
  ServiceState service = ServiceState::Unknown;
  std::vector<DerateFactor> derates;
  /// Lower values are preferred when a deterministic choice is required.
  std::uint32_t priority = 0;
  Sharing sharing;
  std::optional<ContributionGroupId> group;
  /// The explicitly declared shared-fate boundary this contributor belongs to.
  std::optional<IndependenceDomainId> independence_domain;
  /// Other scopes that merely reference this contribution. They account zero
  /// for it: a contribution is counted once, in its home scope.
  std::vector<ScopeId> aliases;
  /// The generations this record was planned against.
  EvidenceBinding binding;
  EvidenceId primary_evidence;
  std::vector<EvidenceId> evidence;
  Timestamp observed_at;
  ObservationSequence observation_sequence;
};

}  // namespace cooling_capacity_accounting

#endif  // COOLING_CAPACITY_ACCOUNTING_CONTRIBUTION_HPP
