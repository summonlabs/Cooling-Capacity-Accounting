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

// The accounting identity of one scope:
//
//   declared_installed == allocatable + withheld + degraded_loss
//                         + unavailable + indeterminate
//
// where declared_installed is the sum of the KNOWN installed quantities
// accounted in that scope. This file asserts the identity, the disjointness of
// the five buckets, the treatment of an unknown installed quantity, the signed
// external residual and the closure status precedence.
//
// Every fixture here is SYNTHETIC: nothing was measured on real equipment.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cooling_capacity_accounting/cooling_capacity_accounting.hpp"

#include "fixtures.hpp"
#include "test_harness.hpp"

namespace {

using namespace cooling_capacity_accounting;
using cca_test::id_of;

// ---------------------------------------------------------------------------
// A file-local synthetic facility builder: site.alpha > zone.alpha > loop.a/b,
// with one contributing air equipment class.
// ---------------------------------------------------------------------------

AccountingScope scope_node(std::string_view id, ScopeKind kind, std::string_view parent,
                           Medium medium) {
  AccountingScope scope;
  scope.id = id_of<ScopeId>(id);
  scope.kind = kind;
  if (!parent.empty()) {
    scope.parent = id_of<ScopeId>(parent);
  }
  scope.medium = medium;
  scope.topology_generation = TopologyGeneration::initial();
  scope.epoch = ControlPlaneEpoch::initial();
  return scope;
}

EquipmentClass class_node(std::string_view id, Medium medium,
                          bool contributes_to_installed = true) {
  EquipmentClass klass;
  klass.id = id_of<EquipmentClassId>(id);
  klass.kind = EquipmentClassKind::Other;
  klass.medium = medium;
  klass.contributes_to_installed = contributes_to_installed;
  return klass;
}

Contribution contribution_node(std::string_view id, std::string_view home,
                               std::string_view equipment_class, std::int64_t installed_mw,
                               ServiceState service = ServiceState::InService) {
  Contribution contribution;
  contribution.id = id_of<ContributionId>(id);
  contribution.home_scope = id_of<ScopeId>(home);
  contribution.equipment = id_of<EquipmentId>(std::string("equipment.") + std::string(id));
  contribution.equipment_class = id_of<EquipmentClassId>(equipment_class);
  contribution.medium = Medium::Air;
  contribution.classification = ContributionClass::Additive;
  contribution.installed =
      Measure<ThermalPower>::known(ThermalPower::of_milliwatts(installed_mw));
  contribution.service = service;
  contribution.observed_at = cca_test::fixture_now();
  contribution.binding = EvidenceBinding::initial();
  return contribution;
}

AccountingInput base_input() {
  AccountingInput input;
  input.generations = GenerationBundle::initial();
  input.policy = cca_test::relaxed_policy();
  input.scopes.push_back(scope_node("site.alpha", ScopeKind::Site, "", Medium::Air));
  input.scopes.push_back(scope_node("zone.alpha", ScopeKind::Zone, "site.alpha", Medium::Air));
  input.scopes.push_back(scope_node("loop.a", ScopeKind::Loop, "zone.alpha", Medium::Air));
  input.scopes.push_back(scope_node("loop.b", ScopeKind::Loop, "zone.alpha", Medium::Air));
  input.equipment_classes.push_back(class_node("class.crah", Medium::Air));
  return input;
}

Result<AccountingLedger> build(const AccountingInput& input) {
  return AccountingLedger::build(input, cca_test::fixture_now());
}

const ScopeAccounting* scope_of(const AccountingLedger& ledger, std::string_view id) {
  return ledger.find_scope(id_of<ScopeId>(id));
}

std::size_t count_code(const std::vector<Finding>& findings, FindingCode code) {
  std::size_t count = 0;
  for (const Finding& finding : findings) {
    if (finding.code == code) {
      count += 1;
    }
  }
  return count;
}

const Finding* find_code(const std::vector<Finding>& findings, FindingCode code) {
  for (const Finding& finding : findings) {
    if (finding.code == code) {
      return &finding;
    }
  }
  return nullptr;
}

// ---------------------------------------------------------------------------

CCA_TEST(closure_identity_holds_for_every_scope) {
  AccountingInput input = base_input();
  input.equipment_classes.push_back(class_node("class.monitor", Medium::Air, false));
  input.contributions.push_back(contribution_node("unit.a1", "loop.a", "class.crah", 1000));
  Contribution degraded =
      contribution_node("unit.a2", "loop.a", "class.crah", 1001, ServiceState::Degraded);
  degraded.derates.push_back(
      DerateFactor(id_of<DerateId>("derate.half"), Ratio::of_ppm(500000).value()));
  input.contributions.push_back(degraded);
  input.contributions.push_back(
      contribution_node("unit.a3", "loop.a", "class.crah", 2000, ServiceState::OutOfService));
  input.contributions.push_back(contribution_node("unit.b1", "loop.b", "class.monitor", 3000));
  input.contributions.push_back(
      contribution_node("unit.b2", "loop.b", "class.crah", 1500, ServiceState::Unknown));
  input.contributions.push_back(contribution_node("unit.z1", "zone.alpha", "class.crah", 400));

  CCA_ASSIGN(ledger, build(input));
  CCA_CHECK_EQ(ledger.scopes().size(), std::size_t{4});

  // The identity holds in every scope and each bucket is a part of the whole:
  // five non-negative terms that re-sum to the declared mass cannot overlap.
  for (const ScopeAccounting& scope : ledger.scopes()) {
    CCA_CHECK(scope.totals.verify(scope.declared_installed).ok());
    CCA_CHECK(scope.withheld.verify_total().ok());
    CCA_CHECK_EQ(scope.totals.sum(), scope.declared_installed);
    CCA_CHECK(scope.totals.allocatable.milliwatts() <=
              scope.declared_installed.milliwatts());
    CCA_CHECK(scope.totals.withheld.milliwatts() <= scope.declared_installed.milliwatts());
    CCA_CHECK(scope.totals.degraded_loss.milliwatts() <=
              scope.declared_installed.milliwatts());
    CCA_CHECK(scope.totals.unavailable.milliwatts() <=
              scope.declared_installed.milliwatts());
    CCA_CHECK(scope.totals.indeterminate.milliwatts() <=
              scope.declared_installed.milliwatts());
    CCA_CHECK(scope.installed_fully_known);
  }

  // Exact per-scope numbers: 1000 allocatable + floor(1001 * 0.5) = 500
  // allocatable and 501 degraded loss, an out-of-service 2000 and an
  // unmeasured-installed 3000 withheld because its class does not contribute.
  const ScopeAccounting* loop_a = scope_of(ledger, "loop.a");
  const ScopeAccounting* loop_b = scope_of(ledger, "loop.b");
  const ScopeAccounting* zone = scope_of(ledger, "zone.alpha");
  const ScopeAccounting* site = scope_of(ledger, "site.alpha");
  CCA_CHECK(loop_a != nullptr && loop_b != nullptr && zone != nullptr && site != nullptr);
  if (loop_a == nullptr || loop_b == nullptr || zone == nullptr || site == nullptr) {
    return;
  }
  CCA_CHECK_EQ(loop_a->declared_installed.milliwatts(), std::int64_t{4001});
  CCA_CHECK_EQ(loop_a->totals.allocatable.milliwatts(), std::int64_t{1500});
  CCA_CHECK_EQ(loop_a->totals.degraded_loss.milliwatts(), std::int64_t{501});
  CCA_CHECK_EQ(loop_a->totals.unavailable.milliwatts(), std::int64_t{2000});
  CCA_CHECK_EQ(loop_a->totals.withheld.milliwatts(), std::int64_t{0});
  CCA_CHECK_EQ(loop_b->declared_installed.milliwatts(), std::int64_t{4500});
  CCA_CHECK_EQ(loop_b->totals.withheld.milliwatts(), std::int64_t{3000});
  CCA_CHECK_EQ(loop_b->withheld.of(WithheldReason::NonContributingClass).milliwatts(),
               std::int64_t{3000});
  CCA_CHECK_EQ(loop_b->totals.indeterminate.milliwatts(), std::int64_t{1500});
  CCA_CHECK_EQ(zone->declared_installed.milliwatts(), std::int64_t{400});
  CCA_CHECK_EQ(site->declared_installed.milliwatts(), std::int64_t{0});
  CCA_CHECK_EQ(ledger.overall_totals().declared_installed.milliwatts(), std::int64_t{8901});

  const ClosureCheck closure = ledger.verify_closure();
  CCA_CHECK(closure.exact);
  CCA_CHECK_EQ(closure.violations.size(), std::size_t{0});
  CCA_CHECK_EQ(closure.scopes_checked, ledger.scopes().size());
  CCA_CHECK_EQ(closure.scopes_conditional, std::size_t{0});
}

CCA_TEST(closure_buckets_are_disjoint_and_partition_installed) {
  AccountingInput input = base_input();
  input.equipment_classes.push_back(class_node("class.monitor", Medium::Air, false));
  input.contributions.push_back(contribution_node("bucket.alloc", "loop.a", "class.crah", 1000));
  input.contributions.push_back(
      contribution_node("bucket.withheld", "loop.a", "class.monitor", 1000));
  Contribution degraded = contribution_node("bucket.degraded", "loop.a", "class.crah", 1000,
                                             ServiceState::Degraded);
  degraded.derates.push_back(DerateFactor(id_of<DerateId>("derate.zero"), Ratio::none()));
  input.contributions.push_back(degraded);
  input.contributions.push_back(
      contribution_node("bucket.oos", "loop.a", "class.crah", 1000, ServiceState::OutOfService));
  input.contributions.push_back(contribution_node("bucket.unknown", "loop.a", "class.crah", 1000,
                                                  ServiceState::Unknown));

  CCA_ASSIGN(ledger, build(input));
  const ScopeAccounting* loop = scope_of(ledger, "loop.a");
  CCA_CHECK(loop != nullptr);
  if (loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(loop->contribution_count, std::size_t{5});
  CCA_CHECK_EQ(loop->declared_installed.milliwatts(), std::int64_t{5000});
  CCA_CHECK_EQ(loop->totals.allocatable.milliwatts(), std::int64_t{1000});
  CCA_CHECK_EQ(loop->totals.withheld.milliwatts(), std::int64_t{1000});
  CCA_CHECK_EQ(loop->totals.degraded_loss.milliwatts(), std::int64_t{1000});
  CCA_CHECK_EQ(loop->totals.unavailable.milliwatts(), std::int64_t{1000});
  CCA_CHECK_EQ(loop->totals.indeterminate.milliwatts(), std::int64_t{1000});
  CCA_CHECK_EQ(loop->totals.sum().milliwatts(), std::int64_t{5000});
  CCA_CHECK(loop->totals.verify(loop->declared_installed).ok());
  // The partition is exact even though one contribution is unattributable: the
  // status reports that, the identity does not break.
  CCA_CHECK_EQ(loop->indeterminate_contribution_count, std::size_t{1});
  CCA_CHECK_EQ(loop->status, ClosureStatus::Indeterminate);
}

CCA_TEST(closure_unknown_installed_is_never_counted_as_zero) {
  AccountingInput input = base_input();
  Contribution unknown = contribution_node("unit.unknown", "loop.a", "class.crah", 0);
  unknown.installed = Measure<ThermalPower>::unknown(
      MeasureReason::SensorFault, BoundedText::from_validated("synthetic sensor fault"));
  input.contributions.push_back(unknown);

  CCA_ASSIGN(ledger, build(input));
  const ScopeAccounting* loop = scope_of(ledger, "loop.a");
  CCA_CHECK(loop != nullptr);
  if (loop == nullptr) {
    return;
  }
  // Nothing is asserted for the unknown quantity: it is not zero, so it enters
  // no bucket and the identity stays conditional over the determinate mass.
  CCA_CHECK_EQ(loop->declared_installed.milliwatts(), std::int64_t{0});
  CCA_CHECK_EQ(loop->totals.sum().milliwatts(), std::int64_t{0});
  CCA_CHECK_EQ(loop->unknown_installed_count, std::size_t{1});
  CCA_CHECK(!loop->installed_fully_known);
  CCA_CHECK_EQ(loop->contribution_count, std::size_t{1});
  CCA_CHECK_EQ(loop->indeterminate_contribution_count, std::size_t{1});
  CCA_CHECK_EQ(loop->determinate_contribution_count, std::size_t{0});
  CCA_CHECK_EQ(loop->status, ClosureStatus::Indeterminate);
  CCA_CHECK_EQ(count_code(loop->findings, FindingCode::UnknownInstalledQuantity), std::size_t{1});
  const Finding* finding = find_code(loop->findings, FindingCode::UnknownInstalledQuantity);
  CCA_CHECK(finding != nullptr);
  if (finding != nullptr) {
    CCA_CHECK_EQ(finding->severity, FindingSeverity::Warning);
  }
  CCA_CHECK(loop->totals.verify(loop->declared_installed).ok());

  // The identity is reported as conditional, not violated.
  const ClosureCheck closure = ledger.verify_closure();
  CCA_CHECK(closure.exact);
  CCA_CHECK_EQ(closure.scopes_checked, ledger.scopes().size());
  CCA_CHECK_EQ(closure.scopes_conditional, std::size_t{1});
}

CCA_TEST(closure_unsupported_installed_is_a_definite_negative) {
  // Unsupported is not Unknown: the configuration has no such quantity at all,
  // so no mass is invented and the scope stays fully known and exactly closed.
  AccountingInput input = base_input();
  Contribution unsupported = contribution_node("unit.unsupported", "loop.a", "class.crah", 0);
  unsupported.installed = Measure<ThermalPower>::unsupported(
      MeasureReason::OutOfScope, BoundedText::from_validated("synthetic out of scope"));
  input.contributions.push_back(unsupported);

  CCA_ASSIGN(ledger, build(input));
  const ScopeAccounting* loop = scope_of(ledger, "loop.a");
  CCA_CHECK(loop != nullptr);
  if (loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(loop->declared_installed.milliwatts(), std::int64_t{0});
  CCA_CHECK_EQ(loop->unknown_installed_count, std::size_t{0});
  CCA_CHECK(loop->installed_fully_known);
  CCA_CHECK_EQ(loop->contribution_count, std::size_t{1});
  CCA_CHECK_EQ(loop->status, ClosureStatus::Closed);
  CCA_CHECK_EQ(count_code(loop->findings, FindingCode::UnsupportedQuantity), std::size_t{1});
  CCA_CHECK(ledger.verify_closure().exact);
}

CCA_TEST(closure_helpers_verify_and_refuse) {
  AccountingInput input = base_input();
  input.contributions.push_back(contribution_node("unit.a1", "loop.a", "class.crah", 1000));

  CCA_ASSIGN(ledger, build(input));
  const ScopeAccounting* loop = scope_of(ledger, "loop.a");
  CCA_CHECK(loop != nullptr);
  if (loop == nullptr) {
    return;
  }

  CCA_CHECK(loop->totals.verify(loop->declared_installed).ok());
  CCA_CHECK(loop->withheld.verify_total().ok());

  // One milliwatt of disagreement is refused, not rounded away.
  CCA_CHECK_CODE(loop->totals.verify(ThermalPower::of_milliwatts(1001)),
                 ErrorCode::ClosureViolation);
  CCA_CHECK_CODE(loop->totals.verify(ThermalPower::of_milliwatts(999)),
                 ErrorCode::ClosureViolation);

  // A breakdown whose total does not match its entries is refused.
  WithheldBreakdown breakdown;
  breakdown.add(WithheldReason::ReserveObligation, ThermalPower::of_milliwatts(5));
  breakdown.add(WithheldReason::SubstitutiveSpare, ThermalPower::of_milliwatts(7));
  CCA_CHECK_EQ(breakdown.total.milliwatts(), std::int64_t{12});
  CCA_CHECK_EQ(breakdown.of(WithheldReason::ReserveObligation).milliwatts(), std::int64_t{5});
  CCA_CHECK(breakdown.verify_total().ok());
  breakdown.by_reason[static_cast<std::size_t>(WithheldReason::ReserveObligation)] =
      ThermalPower::of_milliwatts(4);
  CCA_CHECK_CODE(breakdown.verify_total(), ErrorCode::ClosureViolation);
}

CCA_TEST(closure_empty_scope_is_empty) {
  AccountingInput input = base_input();
  input.contributions.push_back(contribution_node("unit.a1", "loop.a", "class.crah", 1000));

  CCA_ASSIGN(ledger, build(input));
  // loop.b and both ancestors have no contributions and no external declaration.
  const ScopeAccounting* empty_loop = scope_of(ledger, "loop.b");
  const ScopeAccounting* empty_site = scope_of(ledger, "site.alpha");
  CCA_CHECK(empty_loop != nullptr && empty_site != nullptr);
  if (empty_loop == nullptr || empty_site == nullptr) {
    return;
  }
  CCA_CHECK_EQ(empty_loop->status, ClosureStatus::Empty);
  CCA_CHECK_EQ(empty_loop->contribution_count, std::size_t{0});
  CCA_CHECK_EQ(empty_loop->declared_installed.milliwatts(), std::int64_t{0});
  CCA_CHECK(!empty_loop->external_total_present);
  CCA_CHECK(empty_loop->residual.is_zero());
  CCA_CHECK_EQ(empty_site->status, ClosureStatus::Empty);
  CCA_CHECK(empty_loop->totals.verify(empty_loop->declared_installed).ok());

  // An external declaration alone makes the scope non-empty: it is a claim
  // about the scope even when no contribution has been accounted there yet.
  AccountingInput declared = base_input();
  declared.contributions.push_back(contribution_node("unit.a1", "loop.a", "class.crah", 1000));
  for (AccountingScope& scope : declared.scopes) {
    if (scope.id == id_of<ScopeId>("loop.b")) {
      scope.declared_installed_total =
          Measure<ThermalPower>::known(ThermalPower::of_milliwatts(500));
    }
  }
  CCA_ASSIGN(with_declaration, build(declared));
  const ScopeAccounting* declared_loop = scope_of(with_declaration, "loop.b");
  CCA_CHECK(declared_loop != nullptr);
  if (declared_loop == nullptr) {
    return;
  }
  CCA_CHECK(declared_loop->external_total_present);
  CCA_CHECK_EQ(declared_loop->status, ClosureStatus::ClosedWithResidual);
  CCA_CHECK_EQ(declared_loop->residual.milliwatts(), std::int64_t{500});
}

CCA_TEST(closure_external_declaration_residual_is_signed) {
  AccountingInput larger = base_input();
  larger.contributions.push_back(contribution_node("unit.a1", "loop.a", "class.crah", 1000));
  for (AccountingScope& scope : larger.scopes) {
    if (scope.id == id_of<ScopeId>("loop.a")) {
      scope.declared_installed_total =
          Measure<ThermalPower>::known(ThermalPower::of_milliwatts(1500));
    }
  }
  CCA_ASSIGN(over, build(larger));
  const ScopeAccounting* over_loop = scope_of(over, "loop.a");
  CCA_CHECK(over_loop != nullptr);
  if (over_loop == nullptr) {
    return;
  }
  CCA_CHECK(over_loop->external_total_present);
  CCA_CHECK_EQ(over_loop->external_total.milliwatts(), std::int64_t{1500});
  CCA_CHECK_EQ(over_loop->declared_installed.milliwatts(), std::int64_t{1000});
  CCA_CHECK_EQ(over_loop->residual.milliwatts(), std::int64_t{500});
  CCA_CHECK_EQ(over_loop->status, ClosureStatus::ClosedWithResidual);
  const Finding* unexplained = find_code(over_loop->findings, FindingCode::UnexplainedResidual);
  CCA_CHECK(unexplained != nullptr);
  if (unexplained != nullptr) {
    CCA_CHECK(unexplained->amount.has_value());
    if (unexplained->amount.has_value()) {
      CCA_CHECK_EQ(unexplained->amount->milliwatts(), std::int64_t{500});
    }
  }

  AccountingInput smaller = base_input();
  smaller.contributions.push_back(contribution_node("unit.a1", "loop.a", "class.crah", 1000));
  for (AccountingScope& scope : smaller.scopes) {
    if (scope.id == id_of<ScopeId>("loop.a")) {
      scope.declared_installed_total =
          Measure<ThermalPower>::known(ThermalPower::of_milliwatts(600));
    }
  }
  CCA_ASSIGN(under, build(smaller));
  const ScopeAccounting* under_loop = scope_of(under, "loop.a");
  CCA_CHECK(under_loop != nullptr);
  if (under_loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(under_loop->residual.milliwatts(), std::int64_t{-400});
  CCA_CHECK(under_loop->residual.is_negative());
  CCA_CHECK_EQ(under_loop->status, ClosureStatus::ClosedWithResidual);

  // A declaration that agrees exactly leaves no residual and no finding.
  AccountingInput exact = base_input();
  exact.contributions.push_back(contribution_node("unit.a1", "loop.a", "class.crah", 1000));
  for (AccountingScope& scope : exact.scopes) {
    if (scope.id == id_of<ScopeId>("loop.a")) {
      scope.declared_installed_total =
          Measure<ThermalPower>::known(ThermalPower::of_milliwatts(1000));
    }
  }
  CCA_ASSIGN(agreeing, build(exact));
  const ScopeAccounting* agreeing_loop = scope_of(agreeing, "loop.a");
  CCA_CHECK(agreeing_loop != nullptr);
  if (agreeing_loop == nullptr) {
    return;
  }
  CCA_CHECK(agreeing_loop->external_total_present);
  CCA_CHECK(agreeing_loop->residual.is_zero());
  CCA_CHECK_EQ(agreeing_loop->status, ClosureStatus::Closed);
  CCA_CHECK_EQ(count_code(agreeing_loop->findings, FindingCode::UnexplainedResidual),
               std::size_t{0});
}

CCA_TEST(closure_residual_never_adjusts_a_contribution) {
  AccountingInput plain = base_input();
  plain.contributions.push_back(contribution_node("unit.a1", "loop.a", "class.crah", 1000));

  AccountingInput declared = base_input();
  declared.contributions.push_back(contribution_node("unit.a1", "loop.a", "class.crah", 1000));
  for (AccountingScope& scope : declared.scopes) {
    if (scope.id == id_of<ScopeId>("loop.a")) {
      scope.declared_installed_total =
          Measure<ThermalPower>::known(ThermalPower::of_milliwatts(1234));
    }
  }

  CCA_ASSIGN(without, build(plain));
  CCA_ASSIGN(with, build(declared));
  const ScopeAccounting* before = scope_of(without, "loop.a");
  const ScopeAccounting* after = scope_of(with, "loop.a");
  CCA_CHECK(before != nullptr && after != nullptr);
  if (before == nullptr || after == nullptr) {
    return;
  }

  CCA_CHECK_EQ(after->residual.milliwatts(), std::int64_t{234});
  CCA_CHECK_EQ(after->declared_installed, before->declared_installed);
  CCA_CHECK_EQ(after->totals.allocatable, before->totals.allocatable);
  CCA_CHECK_EQ(after->totals.withheld, before->totals.withheld);
  CCA_CHECK_EQ(after->totals.degraded_loss, before->totals.degraded_loss);
  CCA_CHECK_EQ(after->totals.unavailable, before->totals.unavailable);
  CCA_CHECK_EQ(after->totals.indeterminate, before->totals.indeterminate);
  CCA_CHECK_EQ(after->contribution_count, before->contribution_count);
  CCA_CHECK_EQ(after->withheld.total, before->withheld.total);
  CCA_CHECK_EQ(after->status, ClosureStatus::ClosedWithResidual);
  CCA_CHECK_EQ(before->status, ClosureStatus::Closed);

  CCA_CHECK_EQ(without.contributions().size(), std::size_t{1});
  CCA_CHECK_EQ(with.contributions().size(), std::size_t{1});
  CCA_CHECK_EQ(without.contributions().front().allocations.size(),
               with.contributions().front().allocations.size());
  CCA_CHECK_EQ(without.contributions().front().allocations.front().allocatable,
               with.contributions().front().allocations.front().allocatable);
  CCA_CHECK_EQ(without.contributions().front().installed,
               with.contributions().front().installed);
}

CCA_TEST(closure_status_precedence) {
  // Closed: determinate mass, no external declaration.
  AccountingInput closed_input = base_input();
  closed_input.contributions.push_back(
      contribution_node("unit.a1", "loop.a", "class.crah", 1000));
  CCA_ASSIGN(closed_ledger, build(closed_input));
  const ScopeAccounting* closed = scope_of(closed_ledger, "loop.a");
  CCA_CHECK(closed != nullptr);
  if (closed == nullptr) {
    return;
  }
  CCA_CHECK_EQ(closed->status, ClosureStatus::Closed);

  // ClosedWithResidual beats Closed: the same scope plus a disagreeing
  // external declaration.
  AccountingInput residual_input = base_input();
  residual_input.contributions.push_back(
      contribution_node("unit.a1", "loop.a", "class.crah", 1000));
  for (AccountingScope& scope : residual_input.scopes) {
    if (scope.id == id_of<ScopeId>("loop.a")) {
      scope.declared_installed_total =
          Measure<ThermalPower>::known(ThermalPower::of_milliwatts(1400));
    }
  }
  CCA_ASSIGN(residual_ledger, build(residual_input));
  const ScopeAccounting* residual = scope_of(residual_ledger, "loop.a");
  CCA_CHECK(residual != nullptr);
  if (residual == nullptr) {
    return;
  }
  CCA_CHECK_EQ(residual->status, ClosureStatus::ClosedWithResidual);

  // Indeterminate beats ClosedWithResidual: add an unattributable mass.
  AccountingInput indeterminate_input = base_input();
  indeterminate_input.contributions.push_back(
      contribution_node("unit.a1", "loop.a", "class.crah", 1000));
  indeterminate_input.contributions.push_back(
      contribution_node("unit.a2", "loop.a", "class.crah", 700, ServiceState::Unknown));
  for (AccountingScope& scope : indeterminate_input.scopes) {
    if (scope.id == id_of<ScopeId>("loop.a")) {
      scope.declared_installed_total =
          Measure<ThermalPower>::known(ThermalPower::of_milliwatts(1400));
    }
  }
  CCA_ASSIGN(indeterminate_ledger, build(indeterminate_input));
  const ScopeAccounting* indeterminate = scope_of(indeterminate_ledger, "loop.a");
  CCA_CHECK(indeterminate != nullptr);
  if (indeterminate == nullptr) {
    return;
  }
  CCA_CHECK(!indeterminate->residual.is_zero());
  CCA_CHECK_EQ(indeterminate->status, ClosureStatus::Indeterminate);

  // Conflicting outranks Indeterminate: a scope that preserves a contradicting
  // identity reports the contradiction even when some of its mass is also
  // unattributable.
  AccountingInput conflicting_input = base_input();
  conflicting_input.contributions.push_back(
      contribution_node("unit.same", "loop.a", "class.crah", 1000));
  conflicting_input.contributions.push_back(
      contribution_node("unit.same", "loop.a", "class.crah", 2000, ServiceState::Unknown));
  CCA_ASSIGN(conflicting_ledger, build(conflicting_input));
  const ScopeAccounting* conflicting = scope_of(conflicting_ledger, "loop.a");
  CCA_CHECK(conflicting != nullptr);
  if (conflicting == nullptr) {
    return;
  }
  CCA_CHECK_EQ(conflicting->status, ClosureStatus::Conflicting);
  CCA_CHECK(conflicting->totals.verify(conflicting->declared_installed).ok());

  // Empty is the last resort of a scope that declares nothing at all.
  const ScopeAccounting* empty = scope_of(closed_ledger, "loop.b");
  CCA_CHECK(empty != nullptr);
  if (empty != nullptr) {
    CCA_CHECK_EQ(empty->status, ClosureStatus::Empty);
  }
}

CCA_TEST(closure_conflicting_identity_is_preserved_not_dropped) {
  // Two contributions share an identifier but disagree about their content.
  // Both are preserved; exactly one is counted, and which one is counted does
  // not depend on the order the caller supplied them in.
  Contribution first = contribution_node("unit.same", "loop.a", "class.crah", 1000);
  Contribution second = contribution_node("unit.same", "loop.a", "class.crah", 2000);

  AccountingInput forward_input = base_input();
  forward_input.contributions.push_back(first);
  forward_input.contributions.push_back(second);
  CCA_ASSIGN(forward, build(forward_input));

  AccountingInput reversed_input = base_input();
  reversed_input.contributions.push_back(second);
  reversed_input.contributions.push_back(first);
  CCA_ASSIGN(reversed, build(reversed_input));

  CCA_CHECK_EQ(forward.contributions().size(), std::size_t{1});
  CCA_CHECK_EQ(reversed.contributions().size(), std::size_t{1});
  CCA_CHECK_EQ(forward.conflicting_contributions().size(), std::size_t{1});
  CCA_CHECK_EQ(reversed.conflicting_contributions().size(), std::size_t{1});
  CCA_CHECK_EQ(count_code(forward.findings(), FindingCode::ConflictingContributionIdentity),
               std::size_t{1});
  CCA_CHECK_EQ(count_code(reversed.findings(), FindingCode::ConflictingContributionIdentity),
               std::size_t{1});

  const Finding* conflict =
      find_code(forward.findings(), FindingCode::ConflictingContributionIdentity);
  CCA_CHECK(conflict != nullptr);
  if (conflict != nullptr) {
    CCA_CHECK_EQ(conflict->severity, FindingSeverity::Error);
    CCA_CHECK(conflict->scope == id_of<ScopeId>("loop.a"));
    CCA_CHECK(conflict->contribution == id_of<ContributionId>("unit.same"));
  }

  const std::int64_t counted = forward.contributions().front().installed.milliwatts();
  const std::int64_t loser = forward.conflicting_contributions().front().installed.value().milliwatts();
  CCA_CHECK(counted == 1000 || counted == 2000);
  CCA_CHECK_EQ(counted + loser, std::int64_t{3000});
  // The same record wins in both orders, so the counted mass is identical.
  CCA_CHECK_EQ(reversed.contributions().front().installed.milliwatts(), counted);
  CCA_CHECK_EQ(reversed.conflicting_contributions().front().installed.value().milliwatts(),
               loser);
  CCA_CHECK_EQ(forward.digest(), reversed.digest());

  const ScopeAccounting* loop = scope_of(forward, "loop.a");
  CCA_CHECK(loop != nullptr);
  if (loop == nullptr) {
    return;
  }
  // The conflicting record is never counted twice.
  CCA_CHECK_EQ(loop->contribution_count, std::size_t{1});
  CCA_CHECK_EQ(loop->declared_installed.milliwatts(), counted);
  CCA_CHECK(loop->totals.verify(loop->declared_installed).ok());
  // The contradiction is the scope's own: Conflicting outranks every other
  // closure status, so the scope does not quietly report Closed.
  CCA_CHECK_EQ(loop->status, ClosureStatus::Conflicting);
  CCA_CHECK(count_code(loop->findings, FindingCode::ConflictingContributionIdentity) ==
            std::size_t{1});
}

CCA_TEST(closure_byte_identical_replay_is_counted_once) {
  Contribution unit = contribution_node("unit.replay", "loop.a", "class.crah", 1000);
  AccountingInput input = base_input();
  input.contributions.push_back(unit);
  input.contributions.push_back(unit);
  CCA_ASSIGN(ledger, build(input));

  CCA_CHECK_EQ(ledger.contributions().size(), std::size_t{1});
  CCA_CHECK_EQ(ledger.conflicting_contributions().size(), std::size_t{0});
  CCA_CHECK_EQ(count_code(ledger.findings(), FindingCode::DuplicateContributionIdentity),
               std::size_t{1});
  const Finding* duplicate =
      find_code(ledger.findings(), FindingCode::DuplicateContributionIdentity);
  CCA_CHECK(duplicate != nullptr);
  if (duplicate != nullptr) {
    CCA_CHECK_EQ(duplicate->severity, FindingSeverity::Info);
  }
  const ScopeAccounting* loop = scope_of(ledger, "loop.a");
  CCA_CHECK(loop != nullptr);
  if (loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(loop->contribution_count, std::size_t{1});
  CCA_CHECK_EQ(loop->declared_installed.milliwatts(), std::int64_t{1000});
}

}  // namespace
