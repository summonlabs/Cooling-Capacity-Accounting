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

#ifndef COOLING_CAPACITY_ACCOUNTING_ACCOUNTING_HPP
#define COOLING_CAPACITY_ACCOUNTING_ACCOUNTING_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cooling_capacity_accounting/contribution.hpp"
#include "cooling_capacity_accounting/digest.hpp"
#include "cooling_capacity_accounting/ids.hpp"
#include "cooling_capacity_accounting/scope.hpp"
#include "cooling_capacity_accounting/units.hpp"

namespace cooling_capacity_accounting {

/// Severity of an accounting finding. Severity never changes a quantity; it
/// says how strongly the finding argues against the claim the quantity carries.
enum class FindingSeverity : std::int32_t {
  Info = 0,
  Warning = 1,
  Error = 2,
};

[[nodiscard]] std::string_view finding_severity_name(FindingSeverity severity) noexcept;

/// Machine-readable finding codes.
enum class FindingCode : std::int32_t {
  UnknownInstalledQuantity = 1,
  UnknownServiceState = 2,
  StaleEvidence = 3,
  SupersededEvidence = 4,
  FutureEvidence = 5,
  MissingEvidence = 6,
  MissingOutOfServiceEvidence = 7,
  MissingDerateEvidence = 8,
  MissingClassification = 9,
  NameplateMismatch = 10,
  UnexplainedResidual = 11,
  UnapportionedShare = 12,
  OverApportioned = 13,
  IndependenceNotDeclared = 14,
  MissingIndependenceEvidence = 15,
  ReserveShortfall = 16,
  ReserveUnresolved = 17,
  CoverageBelowMinimum = 18,
  MutualExclusionResolved = 19,
  SubstitutiveSpareWithheld = 20,
  AliasRecorded = 21,
  NonContributingClass = 22,
  MediumMismatch = 23,
  EmptyScope = 24,
  DeclaredTotalMissing = 25,
  DuplicateContributionIdentity = 26,
  ConflictingContributionIdentity = 27,
  UnsupportedQuantity = 28,
  AbsoluteDerateExceedsInstalled = 29,
  ReserveOnlyWithheld = 30,
};

[[nodiscard]] std::string_view finding_code_name(FindingCode code) noexcept;

/// One preserved discrepancy, unknown or explanation produced while accounting.
/// Findings are values: they carry no authority and change no quantity.
struct Finding {
  FindingCode code = FindingCode::UnknownInstalledQuantity;
  FindingSeverity severity = FindingSeverity::Warning;
  std::string message;
  ScopeId scope;
  ContributionId contribution;
  EvidenceId evidence;
  /// Optional signed amount the finding is about, in milliwatts.
  std::optional<ThermalDelta> amount;
  /// Optional count the finding is about.
  std::optional<std::uint64_t> count;
};

/// Why an in-service quantity is withheld from allocatable claims.
enum class WithheldReason : std::int32_t {
  /// Withheld to satisfy a redundancy reserve obligation.
  ReserveObligation = 0,
  /// The contribution's class is reserve by construction.
  ReserveOnlyClass = 1,
  /// A substitutive group has more available members than it requires.
  SubstitutiveSpare = 2,
  /// A mutually exclusive group counts exactly one member.
  MutualExclusion = 3,
  /// The contribution is accounted but never contributes to installed total.
  NonContributingClass = 4,
};

inline constexpr std::size_t kWithheldReasonCount = 5;

[[nodiscard]] std::string_view withheld_reason_name(WithheldReason reason) noexcept;

/// Withheld mass by reason. The total is the sum of the entries.
struct WithheldBreakdown {
  ThermalPower total;
  ThermalPower by_reason[kWithheldReasonCount];

  void add(WithheldReason reason, ThermalPower amount) noexcept;
  [[nodiscard]] ThermalPower of(WithheldReason reason) const noexcept;
  [[nodiscard]] Result<void> verify_total() const;
};

/// The partition of a determinate installed mass.
///
/// The accounting identity of one scope is
///
///   declared_installed == allocatable + withheld + degraded_loss
///                         + unavailable + indeterminate
///
/// where every term is an exact integer and the five categories are disjoint:
/// each milliwatt of determinate accounted mass lands in exactly one of them.
/// Mass whose *quantity* is unknown appears in none of these terms; it is
/// counted separately and leaves the identity conditional over the determinate
/// mass. A disagreement with an external declared total is not a sixth category
/// but a signed, preserved residual, because an external declaration may exceed
/// or fall short of the constituent accounting and folding either sign into an
/// additive bucket would make the identity meaningless.
struct DispositionTotals {
  ThermalPower allocatable;
  ThermalPower withheld;
  ThermalPower degraded_loss;
  ThermalPower unavailable;
  ThermalPower indeterminate;

  [[nodiscard]] ThermalPower sum() const;
  [[nodiscard]] Result<void> verify(const ThermalPower& declared_installed) const;
};

/// Closure state of one scope.
enum class ClosureStatus : std::int32_t {
  /// Nothing is declared in this scope.
  Empty = 0,
  /// The identity holds exactly, with no residual and no indeterminate mass.
  Closed = 1,
  /// The identity holds exactly, but an unexplained residual is non-zero.
  ClosedWithResidual = 2,
  /// Part of the declared mass cannot be attributed.
  Indeterminate = 3,
  /// Contributions or declarations contradict each other.
  Conflicting = 4,
};

[[nodiscard]] std::string_view closure_status_name(ClosureStatus status) noexcept;

/// Accounting of exactly one scope. Rollups are computed from these cells; a
/// contribution is counted once, in the scope its home_scope names.
struct ScopeAccounting {
  ScopeId scope;
  ScopeKind kind = ScopeKind::Site;
  Medium medium = Medium::Air;
  std::size_t contribution_count = 0;
  std::size_t determinate_contribution_count = 0;
  std::size_t indeterminate_contribution_count = 0;
  /// Sum of the exactly known installed quantities accounted in this scope.
  /// This is the mass the identity above decomposes.
  ThermalPower declared_installed;
  /// Number of accounted contributions whose installed quantity is not known.
  std::size_t unknown_installed_count = 0;
  /// False when at least one contribution's installed quantity is unknown, in
  /// which case the identity above holds over the determinate mass only.
  bool installed_fully_known = true;
  DispositionTotals totals;
  WithheldBreakdown withheld;
  /// True when an external manifest declaration covered this scope.
  bool external_total_present = false;
  ThermalPower external_total;
  /// external_total - declared_installed. Never used to adjust a contribution.
  ThermalDelta residual;
  /// Determinate share of the declared mass, in ppm.
  Ratio coverage = Ratio::one();
  ClosureStatus status = ClosureStatus::Empty;
  std::vector<Finding> findings;
  Digest digest;
};

/// Why a reserve obligation is or is not established.
enum class ObligationStatus : std::int32_t {
  /// The obligation follows from declared capacity and declared, evidenced
  /// failure domains.
  Established = 0,
  /// The redundancy class is N: there is no reserve to hold.
  NoReserveRequired = 1,
  /// At least one in-service member declares no shared-fate boundary.
  IndependenceNotDeclared = 2,
  /// A declared shared-fate boundary carries no evidence for the declaration.
  MissingIndependenceEvidence = 3,
  /// The protected quantity is unknown or unsupported.
  ProtectedQuantityUnknown = 4,
  /// Another structural condition prevents the obligation from being computed.
  Unresolved = 5,
};

[[nodiscard]] std::string_view obligation_status_name(ObligationStatus status) noexcept;

/// The reserve a redundancy class places on one group.
struct ReserveObligation {
  ContributionGroupId group;
  ScopeId scope;
  RedundancyClass redundancy = RedundancyClass::N;
  Medium medium = Medium::Air;
  ObligationStatus status = ObligationStatus::Unresolved;
  /// The protected quantity the class applies to.
  ThermalPower protected_quantity;
  /// Total installed capacity the class requires.
  ThermalPower required_installed;
  /// required_installed - protected_quantity: the reserve to hold.
  ThermalPower obligation;
  /// The part of the in-service pool actually withheld.
  ThermalPower withheld;
  /// required_installed - in-service retained mass, when positive.
  ThermalDelta shortfall;
  /// The declared failure domains, largest first, then by identifier.
  std::vector<IndependenceDomainId> ranked_domains;
  std::string explanation;
};

/// Aggregate accounting over a set of scopes.
struct AccountingTotals {
  std::size_t scope_count = 0;
  std::size_t contribution_count = 0;
  std::size_t indeterminate_contribution_count = 0;
  ThermalPower declared_installed;
  bool installed_fully_known = true;
  DispositionTotals totals;
  WithheldBreakdown withheld;
  ThermalDelta residual;
  std::size_t unknown_installed_count = 0;
  std::size_t finding_count = 0;
  std::size_t error_finding_count = 0;
  /// True when every scope in the set closed exactly with no residual.
  bool closed_exactly = false;

  void add(const ScopeAccounting& scope) noexcept;
};

/// A mechanically re-derived check of the accounting identity.
struct ClosureViolation {
  ScopeId scope;
  ThermalPower declared_installed;
  ThermalPower decomposed;
  ThermalDelta difference;
  std::string message;
};

struct ClosureCheck {
  bool exact = true;
  std::size_t scopes_checked = 0;
  std::size_t scopes_conditional = 0;
  std::vector<ClosureViolation> violations;
};

/// One scope or dimension rollup.
enum class RollupDimension : std::int32_t {
  Scope = 0,
  EquipmentClass = 1,
  Medium = 2,
};

[[nodiscard]] std::string_view rollup_dimension_name(RollupDimension dimension) noexcept;

struct RollupView {
  RollupDimension dimension = RollupDimension::Scope;
  /// Identifier of the rollup root: a scope, an equipment class, or a medium.
  std::string root;
  BoundedText label;
  AccountingTotals totals;
  std::vector<ScopeAccounting> scopes;
  std::vector<Finding> findings;
  Digest digest;
};

}  // namespace cooling_capacity_accounting

#endif  // COOLING_CAPACITY_ACCOUNTING_ACCOUNTING_HPP
