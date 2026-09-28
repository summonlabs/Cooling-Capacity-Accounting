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

// Derates: absolute reductions are subtracted before the factors, factors
// compose exactly as floor(retained * product(ppm) / 1000000^k) with a single
// truncating division, and the result never depends on the order the derates
// were supplied in. Every quantity here is SYNTHETIC.

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

EquipmentClass class_node(std::string_view id, Medium medium) {
  EquipmentClass klass;
  klass.id = id_of<EquipmentClassId>(id);
  klass.kind = EquipmentClassKind::Other;
  klass.medium = medium;
  return klass;
}

AccountingInput base_input() {
  AccountingInput input;
  input.generations = GenerationBundle::initial();
  input.policy = cca_test::relaxed_policy();
  input.scopes.push_back(scope_node("site.alpha", ScopeKind::Site, "", Medium::Air));
  input.scopes.push_back(scope_node("zone.alpha", ScopeKind::Zone, "site.alpha", Medium::Air));
  input.scopes.push_back(scope_node("loop.a", ScopeKind::Loop, "zone.alpha", Medium::Air));
  input.equipment_classes.push_back(class_node("class.crah", Medium::Air));
  return input;
}

Ratio ppm(std::uint32_t value) {
  const Result<Ratio> ratio = Ratio::of_ppm(value);
  CCA_CHECK(ratio.ok());
  return ratio.value_or(Ratio::one());
}

std::string derate_name(std::size_t index) {
  return std::string("derate.") + static_cast<char>('a' + static_cast<int>(index));
}

DerateFactor factor_derate(std::string_view id, std::uint32_t ppm_value) {
  return DerateFactor(id_of<DerateId>(id), ppm(ppm_value));
}

DerateFactor absolute_derate(std::string_view id, std::int64_t reduction_mw) {
  return DerateFactor(id_of<DerateId>(id), ThermalPower::of_milliwatts(reduction_mw));
}

/// Rounds down the single-division composition of every factor derate, exactly
/// as the documented formula states it. The caller chooses quantities whose
/// product fits an unsigned 64-bit integer, which the assertions verify.
std::int64_t expected_retained(std::int64_t installed_mw,
                               const std::vector<std::uint32_t>& factors) {
  unsigned long long numerator = static_cast<unsigned long long>(installed_mw);
  unsigned long long denominator = 1ULL;
  for (const std::uint32_t factor : factors) {
    numerator *= static_cast<unsigned long long>(factor);
    denominator *= 1000000ULL;
  }
  return static_cast<std::int64_t>(numerator / denominator);
}

/// Truncating composition applied one factor at a time, which is NOT the
/// documented formula. It exists so a test can show that the two disagree.
std::int64_t sequential_retained(std::int64_t installed_mw,
                                 const std::vector<std::uint32_t>& factors) {
  std::int64_t value = installed_mw;
  for (const std::uint32_t factor : factors) {
    value = static_cast<std::int64_t>(
        (static_cast<unsigned long long>(value) * factor) / 1000000ULL);
  }
  return value;
}

Contribution degraded_contribution(std::int64_t installed_mw,
                                   const std::vector<DerateFactor>& derates) {
  Contribution contribution;
  contribution.id = id_of<ContributionId>("unit.degraded");
  contribution.home_scope = id_of<ScopeId>("loop.a");
  contribution.equipment = id_of<EquipmentId>("equipment.degraded");
  contribution.equipment_class = id_of<EquipmentClassId>("class.crah");
  contribution.medium = Medium::Air;
  contribution.classification = ContributionClass::Additive;
  contribution.installed =
      Measure<ThermalPower>::known(ThermalPower::of_milliwatts(installed_mw));
  contribution.service = ServiceState::Degraded;
  contribution.derates = derates;
  contribution.observed_at = cca_test::fixture_now();
  contribution.binding = EvidenceBinding::initial();
  return contribution;
}

/// One degraded contribution carrying the given derates in loop.a.
Result<AccountingLedger> degraded_ledger(std::int64_t installed_mw,
                                         const std::vector<DerateFactor>& derates) {
  AccountingInput input = base_input();
  input.contributions.push_back(degraded_contribution(installed_mw, derates));
  return AccountingLedger::build(input, cca_test::fixture_now());
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

const ScopeAccounting* scope_of(const AccountingLedger& ledger, std::string_view id) {
  return ledger.find_scope(id_of<ScopeId>(id));
}

// ---------------------------------------------------------------------------

CCA_TEST(derates_a_single_factor_truncates_exactly) {
  // 1000001 mW * 0.5 = 500000.5 mW: the half milliwatt is truncated, not
  // rounded, and it is preserved as degraded loss.
  CCA_ASSIGN(ledger, degraded_ledger(1000001, {factor_derate("derate.half", 500000)}));
  const ScopeAccounting* loop = scope_of(ledger, "loop.a");
  CCA_CHECK(loop != nullptr);
  if (loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(loop->declared_installed.milliwatts(), std::int64_t{1000001});
  CCA_CHECK_EQ(loop->totals.allocatable.milliwatts(), std::int64_t{500000});
  CCA_CHECK_EQ(loop->totals.degraded_loss.milliwatts(), std::int64_t{500001});
  CCA_CHECK_EQ(loop->totals.sum(), loop->declared_installed);

  // The exact boundaries: a factor of 1.0 changes nothing, a factor of 0 ppm
  // removes the whole quantity.
  CCA_ASSIGN(whole, degraded_ledger(1000001, {factor_derate("derate.one", 1000000)}));
  CCA_CHECK_EQ(scope_of(whole, "loop.a")->totals.allocatable.milliwatts(), std::int64_t{1000001});
  CCA_CHECK_EQ(scope_of(whole, "loop.a")->totals.degraded_loss.milliwatts(), std::int64_t{0});
  CCA_ASSIGN(nothing, degraded_ledger(1000001, {factor_derate("derate.none", 0)}));
  CCA_CHECK_EQ(scope_of(nothing, "loop.a")->totals.allocatable.milliwatts(), std::int64_t{0});
  CCA_CHECK_EQ(scope_of(nothing, "loop.a")->totals.degraded_loss.milliwatts(),
               std::int64_t{1000001});
}

CCA_TEST(derates_stacked_factors_compose_exactly) {
  // floor(1000003 * 0.6 * 0.6) = floor(360001.08) = 360001. Truncating after
  // each factor would give 360000, so this case decides the formula.
  const std::vector<std::uint32_t> pair{600000, 600000};
  const std::int64_t expected = expected_retained(1000003, pair);
  CCA_CHECK_EQ(expected, std::int64_t{360001});
  CCA_CHECK_EQ(sequential_retained(1000003, pair), std::int64_t{360000});
  CCA_ASSIGN(ledger, degraded_ledger(1000003, {factor_derate("derate.a", 600000),
                                               factor_derate("derate.b", 600000)}));
  const ScopeAccounting* loop = scope_of(ledger, "loop.a");
  CCA_CHECK(loop != nullptr);
  if (loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(loop->totals.allocatable.milliwatts(), expected);
  CCA_CHECK_EQ(loop->totals.degraded_loss.milliwatts(), 1000003 - expected);
  CCA_CHECK_EQ(loop->totals.sum(), loop->declared_installed);

  // Three factors: floor(1000000 * 0.5 * 0.5 * 0.000002) = floor(0.5) = 0.
  const std::vector<std::uint32_t> triple{500000, 500000, 2};
  const std::int64_t three = expected_retained(1000000, triple);
  CCA_CHECK_EQ(three, std::int64_t{0});
  CCA_ASSIGN(cascaded, degraded_ledger(1000000, {factor_derate("derate.a", 500000),
                                                 factor_derate("derate.b", 500000),
                                                 factor_derate("derate.c", 2)}));
  CCA_CHECK_EQ(scope_of(cascaded, "loop.a")->totals.allocatable.milliwatts(), std::int64_t{0});
  CCA_CHECK_EQ(scope_of(cascaded, "loop.a")->totals.degraded_loss.milliwatts(),
               std::int64_t{1000000});

  // A factor of exactly 1.0 among the factors does not shift the truncation:
  // 1000003 * 1.0 * 0.6 and 1000003 * 0.6 retain the same mass.
  CCA_CHECK_EQ(expected_retained(1000003, {1000000, 600000}), std::int64_t{600001});
  CCA_ASSIGN(identity, degraded_ledger(1000003, {factor_derate("derate.a", 1000000),
                                                 factor_derate("derate.b", 600000)}));
  CCA_CHECK_EQ(scope_of(identity, "loop.a")->totals.allocatable.milliwatts(),
               std::int64_t{600001});
}

CCA_TEST(derates_absolute_reductions_are_subtracted_before_the_factors) {
  // The absolute derate is supplied after the factor and must still be applied
  // first: (1000000 - 400000) * 0.5 = 300000.
  CCA_ASSIGN(ledger, degraded_ledger(1000000, {factor_derate("derate.half", 500000),
                                               absolute_derate("derate.abs", 400000)}));
  const ScopeAccounting* loop = scope_of(ledger, "loop.a");
  CCA_CHECK(loop != nullptr);
  if (loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(loop->totals.allocatable.milliwatts(), std::int64_t{300000});
  CCA_CHECK_EQ(loop->totals.degraded_loss.milliwatts(), std::int64_t{700000});
  CCA_CHECK_EQ(loop->totals.sum(), loop->declared_installed);

  // Two absolute derates are added together, then the factors apply.
  CCA_ASSIGN(stacked, degraded_ledger(1000000, {absolute_derate("derate.abs1", 400000),
                                                factor_derate("derate.half", 500000),
                                                absolute_derate("derate.abs2", 100000)}));
  CCA_CHECK_EQ(scope_of(stacked, "loop.a")->totals.allocatable.milliwatts(),
               std::int64_t{250000});
  CCA_CHECK_EQ(scope_of(stacked, "loop.a")->totals.degraded_loss.milliwatts(),
               std::int64_t{750000});
}

CCA_TEST(derates_absolute_beyond_installed_keeps_the_loss) {
  CCA_ASSIGN(ledger, degraded_ledger(100, {absolute_derate("derate.abs", 500)}));
  const ScopeAccounting* loop = scope_of(ledger, "loop.a");
  CCA_CHECK(loop != nullptr);
  if (loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(loop->totals.allocatable.milliwatts(), std::int64_t{0});
  CCA_CHECK_EQ(loop->totals.degraded_loss.milliwatts(), std::int64_t{100});
  CCA_CHECK_EQ(loop->totals.degraded_loss, loop->declared_installed);
  CCA_CHECK_EQ(loop->totals.sum(), loop->declared_installed);
  CCA_CHECK_EQ(count_code(loop->findings, FindingCode::AbsoluteDerateExceedsInstalled),
               std::size_t{1});
  CCA_CHECK_EQ(count_code(ledger.findings(), FindingCode::AbsoluteDerateExceedsInstalled),
               std::size_t{1});
  CCA_CHECK_EQ(loop->status, ClosureStatus::Closed);

  // Exactly the installed quantity is not "beyond" it: the loss is the whole
  // quantity and no finding claims an excess.
  CCA_ASSIGN(exact, degraded_ledger(100, {absolute_derate("derate.abs", 100)}));
  const ScopeAccounting* exact_loop = scope_of(exact, "loop.a");
  CCA_CHECK(exact_loop != nullptr);
  if (exact_loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(exact_loop->totals.allocatable.milliwatts(), std::int64_t{0});
  CCA_CHECK_EQ(exact_loop->totals.degraded_loss.milliwatts(), std::int64_t{100});
  CCA_CHECK_EQ(count_code(exact_loop->findings, FindingCode::AbsoluteDerateExceedsInstalled),
               std::size_t{0});
}

CCA_TEST(derates_four_factors_are_refused) {
  const std::vector<DerateFactor> three{factor_derate("derate.a", 500000),
                                        factor_derate("derate.b", 500000),
                                        factor_derate("derate.c", 500000)};
  CCA_CHECK(degraded_ledger(1000000, three).ok());

  std::vector<DerateFactor> four = three;
  four.push_back(factor_derate("derate.d", 500000));
  CCA_CHECK_CODE(degraded_ledger(1000000, four), ErrorCode::LimitExceeded);

  // The bound is the policy's, and the policy may only lower it.
  AccountingInput input = base_input();
  input.contributions.push_back(degraded_contribution(1000000, three));
  input.policy.max_derate_factors = 2;
  CCA_CHECK_CODE(AccountingLedger::build(input, cca_test::fixture_now()),
                 ErrorCode::LimitExceeded);
  input.policy.max_derate_factors = 3;
  CCA_CHECK(AccountingLedger::build(input, cca_test::fixture_now()).ok());
}

CCA_TEST(derates_are_only_meaningful_for_a_degraded_contribution) {
  const std::vector<DerateFactor> derates{factor_derate("derate.a", 500000)};

  // A derate on a contribution that is not degraded is refused...
  AccountingInput in_service = base_input();
  Contribution running = degraded_contribution(1000, derates);
  running.id = id_of<ContributionId>("unit.running");
  running.equipment = id_of<EquipmentId>("equipment.running");
  running.service = ServiceState::InService;
  in_service.contributions.push_back(running);
  CCA_CHECK_CODE(AccountingLedger::build(in_service, cca_test::fixture_now()),
                 ErrorCode::PolicyViolation);

  // ... and so is a degraded contribution with no derate at all.
  AccountingInput degraded = base_input();
  Contribution bare = degraded_contribution(1000, {});
  bare.id = id_of<ContributionId>("unit.bare");
  bare.equipment = id_of<EquipmentId>("equipment.bare");
  degraded.contributions.push_back(bare);
  CCA_CHECK_CODE(AccountingLedger::build(degraded, cca_test::fixture_now()),
                 ErrorCode::PolicyViolation);

  // The rule follows the service state: out of service and unmeasured are
  // refused as well.
  AccountingInput out_of_service = base_input();
  Contribution stopped = degraded_contribution(1000, derates);
  stopped.id = id_of<ContributionId>("unit.stopped");
  stopped.equipment = id_of<EquipmentId>("equipment.stopped");
  stopped.service = ServiceState::OutOfService;
  out_of_service.contributions.push_back(stopped);
  CCA_CHECK_CODE(AccountingLedger::build(out_of_service, cca_test::fixture_now()),
                 ErrorCode::PolicyViolation);

  AccountingInput unknown = base_input();
  Contribution unmeasured = degraded_contribution(1000, derates);
  unmeasured.id = id_of<ContributionId>("unit.unmeasured");
  unmeasured.equipment = id_of<EquipmentId>("equipment.unmeasured");
  unmeasured.service = ServiceState::Unknown;
  unknown.contributions.push_back(unmeasured);
  CCA_CHECK_CODE(AccountingLedger::build(unknown, cca_test::fixture_now()),
                 ErrorCode::PolicyViolation);
}

CCA_TEST(derates_are_refused_when_they_are_malformed) {
  // A factor derate carrying an absolute reduction.
  DerateFactor mixed = factor_derate("derate.mixed", 500000);
  mixed.absolute = ThermalPower::of_milliwatts(5);
  CCA_CHECK_CODE(degraded_ledger(1000, {mixed}), ErrorCode::DerateOutOfRange);

  // An absolute derate carrying a factor.
  DerateFactor scaled = absolute_derate("derate.scaled", 100);
  scaled.factor = ppm(500000);
  CCA_CHECK_CODE(degraded_ledger(1000, {scaled}), ErrorCode::DerateOutOfRange);

  // An absolute derate of zero reduces nothing.
  CCA_CHECK_CODE(degraded_ledger(1000, {absolute_derate("derate.zero", 0)}),
                 ErrorCode::DerateOutOfRange);

  // A derate must be identifiable and may not repeat inside a contribution.
  DerateFactor unnamed = factor_derate("derate.unnamed", 500000);
  unnamed.id = DerateId();
  CCA_CHECK_CODE(degraded_ledger(1000, {unnamed}), ErrorCode::MissingRequiredField);
  CCA_CHECK_CODE(degraded_ledger(1000, {factor_derate("derate.same", 500000),
                                        factor_derate("derate.same", 250000)}),
                 ErrorCode::DuplicateIdentity);

  // The boundaries of the range rules are accepted: a factor of zero with no
  // absolute reduction, and an absolute derate of exactly one milliwatt.
  DerateFactor zero_factor = factor_derate("derate.zeroppm", 0);
  CCA_CHECK(degraded_ledger(1000, {zero_factor}).ok());
  DerateFactor absolute = absolute_derate("derate.absolute", 1);
  CCA_CHECK(degraded_ledger(1000, {absolute}).ok());
}

CCA_TEST(derates_are_independent_of_the_order_they_were_supplied_in) {
  // 100 * 0.6 * 0.5 * 0.333333 = 9.99999, truncated to 9.
  const std::int64_t installed = 100;
  const std::vector<std::uint32_t> factors{600000, 500000, 333333};
  const std::int64_t expected = expected_retained(installed, factors);
  CCA_CHECK_EQ(expected, std::int64_t{9});

  CCA_ASSIGN(reference, degraded_ledger(installed, {factor_derate("derate.a", 600000),
                                                    factor_derate("derate.b", 500000),
                                                    factor_derate("derate.c", 333333)}));
  const ScopeAccounting* reference_loop = scope_of(reference, "loop.a");
  CCA_CHECK(reference_loop != nullptr);
  if (reference_loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(reference_loop->totals.allocatable.milliwatts(), expected);

  // Every permutation of the same three derates retains exactly the same mass
  // and produces the same accounting generation.
  const std::vector<std::vector<std::size_t>> orders{
      {0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
  for (const std::vector<std::size_t>& order : orders) {
    std::vector<DerateFactor> derates;
    for (const std::size_t index : order) {
      derates.push_back(factor_derate(derate_name(index), factors[index]));
    }
    CCA_ASSIGN(candidate, degraded_ledger(installed, derates));
    const ScopeAccounting* loop = scope_of(candidate, "loop.a");
    CCA_CHECK(loop != nullptr);
    if (loop == nullptr) {
      return;
    }
    CCA_CHECK_EQ(loop->totals.allocatable.milliwatts(), expected);
    CCA_CHECK_EQ(loop->totals.degraded_loss.milliwatts(), installed - expected);
    CCA_CHECK_EQ(candidate.digest(), reference.digest());
  }
}

}  // namespace
