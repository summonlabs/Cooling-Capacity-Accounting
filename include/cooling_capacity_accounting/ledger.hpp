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

#ifndef COOLING_CAPACITY_ACCOUNTING_LEDGER_HPP
#define COOLING_CAPACITY_ACCOUNTING_LEDGER_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "cooling_capacity_accounting/accounting.hpp"
#include "cooling_capacity_accounting/contribution.hpp"
#include "cooling_capacity_accounting/domain.hpp"
#include "cooling_capacity_accounting/evidence.hpp"
#include "cooling_capacity_accounting/limits.hpp"
#include "cooling_capacity_accounting/policy.hpp"
#include "cooling_capacity_accounting/scope.hpp"

namespace cooling_capacity_accounting {

/// The complete, validated input of one accounting generation.
///
/// The order of every vector is insignificant: no result this library produces
/// depends on input order, and the canonical encoding sorts deterministically
/// before it hashes.
struct AccountingInput {
  std::vector<AccountingScope> scopes;
  std::vector<Contribution> contributions;
  std::vector<ContributionGroup> groups;
  std::vector<EvidenceRecord> evidence;
  std::vector<IndependenceDomain> independence_domains;
  std::vector<ManifestDeclaration> manifest_declarations;
  std::vector<EquipmentClass> equipment_classes;
  /// The generations this input claims to be complete for.
  GenerationBundle generations;
  /// The policy the input is evaluated under.
  AccountingPolicy policy;
};

/// How one contribution landed in one scope.
struct ContributionAllocation {
  ScopeId scope;
  /// The part of the installed quantity that landed in this scope.
  ThermalPower installed;
  /// The installed quantity after derates.
  ThermalPower retained;
  ThermalPower allocatable;
  ThermalPower withheld;
  ThermalPower degraded_loss;
  ThermalPower unavailable;
  ThermalPower indeterminate;
  /// Set when the withheld mass has a single dominating reason.
  std::optional<WithheldReason> withheld_reason;
  WithheldBreakdown withheld_by_reason;
};

/// The complete accounting of one contribution, across every scope it touches.
struct ContributionAccounting {
  ContributionId id;
  ContributionClass classification = ContributionClass::Additive;
  ServiceState service = ServiceState::Unknown;
  MeasureState installed_state = MeasureState::Unknown;
  ThermalPower installed;
  Ratio applied_derate = Ratio::one();
  std::vector<ContributionAllocation> allocations;
  std::vector<Finding> findings;
};

/// Deterministic accounting engine for one immutable generation of input.
///
/// The ledger performs the whole computation once, at construction, and then
/// answers every query from immutable data. It starts no thread, takes no lock
/// and calls no callback, so its accessors are safe to call concurrently on a
/// published instance.
class AccountingLedger {
 public:
  AccountingLedger() = default;

  /// Validates and accounts one input at one instant.
  [[nodiscard]] static Result<AccountingLedger> build(AccountingInput input,
                                                      Timestamp accounted_at,
                                                      const Limits& limits = Limits{});
  /// Validates an input without accounting it. The first failure is the one
  /// reported, in the documented validation precedence order.
  [[nodiscard]] static Result<void> validate_input(const AccountingInput& input,
                                                   Timestamp accounted_at,
                                                   const Limits& limits = Limits{});

  [[nodiscard]] const AccountingInput& input() const noexcept { return input_; }
  [[nodiscard]] const AccountingPolicy& policy() const noexcept {
    return input_.policy;
  }
  [[nodiscard]] const GenerationBundle& generations() const noexcept {
    return input_.generations;
  }
  [[nodiscard]] Timestamp accounted_at() const noexcept { return accounted_at_; }

  [[nodiscard]] const std::vector<ScopeAccounting>& scopes() const noexcept {
    return scopes_;
  }
  /// The accounting cell of one scope, or nullptr when it is not in this
  /// generation.
  [[nodiscard]] const ScopeAccounting* find_scope(const ScopeId& scope) const noexcept;
  [[nodiscard]] const ContributionAccounting* find_contribution(
      const ContributionId& id) const noexcept;

  /// Totals over every scope that has no parent.
  [[nodiscard]] AccountingTotals site_totals() const;
  /// Totals over a scope and every scope nested below it.
  [[nodiscard]] Result<RollupView> scope_rollup(const ScopeId& root) const;
  /// Totals over every scope, grouped by equipment class.
  [[nodiscard]] Result<RollupView> class_rollup(const EquipmentClassId& klass) const;
  /// Totals over every scope, grouped by medium.
  [[nodiscard]] Result<RollupView> medium_rollup(Medium medium) const;
  /// Totals over every scope in the generation.
  [[nodiscard]] AccountingTotals overall_totals() const;

  [[nodiscard]] const std::vector<ReserveObligation>& reserve_obligations() const noexcept {
    return obligations_;
  }
  [[nodiscard]] const std::vector<Finding>& findings() const noexcept { return findings_; }
  [[nodiscard]] const std::vector<ContributionAccounting>& contributions() const noexcept {
    return contributions_;
  }
  /// Records that lost an identity conflict. They are preserved here, are never
  /// counted and are never silently dropped.
  [[nodiscard]] const std::vector<Contribution>& conflicting_contributions()
      const noexcept {
    return conflicts_;
  }

  /// Re-derives the identity of every scope from the stored cells and checks it.
  [[nodiscard]] ClosureCheck verify_closure() const;

  /// Digest of the accounted content: input digest, policy digest, generations
  /// and accounting instant.
  [[nodiscard]] Digest digest() const noexcept { return digest_; }

  /// Stable, ASCII-only, line-oriented report of the generation.
  [[nodiscard]] std::string to_report() const;

 private:
  /// One (scope, equipment class) accounting cell. Scope totals are the sum of
  /// their cells; class and medium rollups are built from them, so a class
  /// rollup can never accidentally include another class's mass.
  struct ClassCell {
    ScopeId scope;
    EquipmentClassId equipment_class;
    Medium medium = Medium::Air;
    std::size_t contribution_count = 0;
    ThermalPower declared_installed;
    DispositionTotals totals;
    WithheldBreakdown withheld;
    bool external_total_present = false;
    ThermalPower external_total;
    ThermalDelta residual;
    ClosureStatus status = ClosureStatus::Empty;
  };

  [[nodiscard]] Result<void> account(const Limits& limits);
  [[nodiscard]] Result<RollupView> class_rollup_from_cells(const EquipmentClassId& klass,
                                                           Medium medium_filter,
                                                           bool filter_by_medium,
                                                           RollupDimension dimension,
                                                           std::string root) const;

  AccountingInput input_;
  Timestamp accounted_at_;
  std::vector<ScopeAccounting> scopes_;
  std::vector<ContributionAccounting> contributions_;
  std::vector<ReserveObligation> obligations_;
  std::vector<Finding> findings_;
  std::vector<ClassCell> cells_;
  std::vector<Contribution> conflicts_;
  Digest digest_;
};

}  // namespace cooling_capacity_accounting

#endif  // COOLING_CAPACITY_ACCOUNTING_LEDGER_HPP
