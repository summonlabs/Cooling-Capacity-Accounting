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

// Evidence: freshness classification, the policy requirement flags (which
// produce indeterminate dispositions plus findings rather than hard errors),
// nameplate disagreement and the hard future-timestamp errors. Every fixture
// here is SYNTHETIC.

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

constexpr std::int64_t kDayMs = 86'400'000;
constexpr std::int64_t kTwentyYearsMs = 20LL * 365 * 24 * 60 * 60 * 1000;

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

AccountingInput base_input(const AccountingPolicy& policy) {
  AccountingInput input;
  input.generations = GenerationBundle::initial();
  input.policy = policy;
  input.scopes.push_back(scope_node("site.alpha", ScopeKind::Site, "", Medium::Air));
  input.scopes.push_back(scope_node("zone.alpha", ScopeKind::Zone, "site.alpha", Medium::Air));
  input.scopes.push_back(scope_node("loop.a", ScopeKind::Loop, "zone.alpha", Medium::Air));
  EquipmentClass klass;
  klass.id = id_of<EquipmentClassId>("class.crah");
  klass.kind = EquipmentClassKind::Other;
  klass.medium = Medium::Air;
  input.equipment_classes.push_back(klass);
  return input;
}

Contribution contribution_node(std::string_view id, std::int64_t installed_mw,
                               ServiceState service = ServiceState::InService) {
  Contribution contribution;
  contribution.id = id_of<ContributionId>(id);
  contribution.home_scope = id_of<ScopeId>("loop.a");
  contribution.equipment = id_of<EquipmentId>(std::string("equipment.") + std::string(id));
  contribution.equipment_class = id_of<EquipmentClassId>("class.crah");
  contribution.medium = Medium::Air;
  contribution.classification = ContributionClass::Additive;
  contribution.installed =
      Measure<ThermalPower>::known(ThermalPower::of_milliwatts(installed_mw));
  contribution.service = service;
  contribution.observed_at = cca_test::fixture_now();
  contribution.binding = EvidenceBinding::initial();
  return contribution;
}

EvidenceBinding binding_of(const GenerationBundle& generations) {
  return EvidenceBinding{generations.epoch, generations.topology, generations.policy,
                         generations.evidence};
}

Result<AccountingLedger> build(const AccountingInput& input) {
  return AccountingLedger::build(input, cca_test::fixture_now());
}

const ScopeAccounting* sole_loop(const AccountingLedger& ledger) {
  return ledger.find_scope(id_of<ScopeId>("loop.a"));
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

CCA_TEST(evidence_freshness_classification_ladder) {
  const GenerationBundle generations = GenerationBundle::initial();
  const EvidenceBinding current = binding_of(generations);
  const DurationMs window = DurationMs::of_milliseconds(kDayMs);
  const Timestamp now = cca_test::fixture_now();

  const EvidenceRecord fresh = cca_test::make_evidence(
      "evidence.fresh", EvidenceKind::Nameplate, now, generations);
  CCA_CHECK_EQ(classify_evidence(fresh, current, window, now), EvidenceFreshness::Current);

  // Exactly at the window is not older than the window.
  const EvidenceRecord edge = cca_test::make_evidence(
      "evidence.edge", EvidenceKind::Nameplate, cca_test::fixture_ago(kDayMs), generations);
  CCA_CHECK_EQ(classify_evidence(edge, current, window, now), EvidenceFreshness::Current);

  // One millisecond past it is stale.
  const EvidenceRecord stale = cca_test::make_evidence(
      "evidence.stale", EvidenceKind::Nameplate, cca_test::fixture_ago(kDayMs + 1),
      generations);
  CCA_CHECK_EQ(classify_evidence(stale, current, window, now), EvidenceFreshness::Stale);

  // A zero window means the evidence never expires.
  const EvidenceRecord ancient = cca_test::make_evidence(
      "evidence.ancient", EvidenceKind::Nameplate, cca_test::fixture_ago(kTwentyYearsMs),
      generations);
  CCA_CHECK_EQ(classify_evidence(ancient, current, DurationMs::zero(), now),
               EvidenceFreshness::Current);
  CCA_CHECK_EQ(classify_evidence(ancient, current, window, now), EvidenceFreshness::Stale);

  // Future beats every other disqualification.
  EvidenceRecord future = cca_test::make_evidence(
      "evidence.future", EvidenceKind::Nameplate,
      Timestamp::of_unix_milliseconds(now.unix_milliseconds() + 1), generations);
  CCA_CHECK_EQ(classify_evidence(future, current, window, now), EvidenceFreshness::Future);
  CCA_CHECK_EQ(classify_evidence(future, current, DurationMs::zero(), now),
               EvidenceFreshness::Future);
  future.binding.topology = TopologyGeneration::of(9).value();
  CCA_CHECK_EQ(classify_evidence(future, current, window, now), EvidenceFreshness::Future);
}

CCA_TEST(evidence_superseded_bindings_are_not_current) {
  const GenerationBundle generations = GenerationBundle::initial();
  const EvidenceBinding current = binding_of(generations);
  const DurationMs window = DurationMs::of_milliseconds(kDayMs);
  const Timestamp now = cca_test::fixture_now();

  EvidenceRecord record = cca_test::make_evidence(
      "evidence.bound", EvidenceKind::Nameplate, now, generations);
  CCA_CHECK_EQ(classify_evidence(record, current, window, now), EvidenceFreshness::Current);

  // A later control-plane epoch, topology generation or policy generation is a
  // superseded binding.
  EvidenceRecord other = record;
  other.binding.epoch = ControlPlaneEpoch::of(current.epoch.value() + 1U).value();
  CCA_CHECK_EQ(classify_evidence(other, current, window, now), EvidenceFreshness::Superseded);
  other = record;
  other.binding.topology = TopologyGeneration::of(current.topology.value() + 1U).value();
  CCA_CHECK_EQ(classify_evidence(other, current, window, now), EvidenceFreshness::Superseded);
  other = record;
  other.binding.policy = PolicyGeneration::of(current.policy.value() + 1U).value();
  CCA_CHECK_EQ(classify_evidence(other, current, window, now), EvidenceFreshness::Superseded);

  // An evidence generation later than the accounting one is superseded too.
  other = record;
  other.binding.evidence = EvidenceGeneration::of(current.evidence.value() + 1U).value();
  CCA_CHECK_EQ(classify_evidence(other, current, window, now), EvidenceFreshness::Superseded);

  // An equal or older evidence generation is not.
  other = record;
  other.binding.evidence = EvidenceGeneration::of(current.evidence.value()).value();
  CCA_CHECK_EQ(classify_evidence(other, current, window, now), EvidenceFreshness::Current);
  EvidenceBinding later = current;
  later.evidence = EvidenceGeneration::of(current.evidence.value() + 3U).value();
  CCA_CHECK_EQ(classify_evidence(record, later, window, now), EvidenceFreshness::Current);
}

CCA_TEST(evidence_policy_requires_installed_evidence) {
  AccountingInput strict = base_input(cca_test::strict_policy());
  strict.contributions.push_back(contribution_node("unit.one", 1000));
  CCA_ASSIGN(strict_ledger, build(strict));
  const ScopeAccounting* strict_loop = sole_loop(strict_ledger);
  CCA_CHECK(strict_loop != nullptr);
  if (strict_loop == nullptr) {
    return;
  }
  // The requirement is a finding plus an indeterminate disposition, not a
  // refused input.
  CCA_CHECK_EQ(strict_loop->status, ClosureStatus::Indeterminate);
  CCA_CHECK_EQ(strict_loop->totals.indeterminate.milliwatts(), std::int64_t{1000});
  CCA_CHECK_EQ(strict_loop->totals.allocatable.milliwatts(), std::int64_t{0});
  CCA_CHECK_EQ(count_code(strict_loop->findings, FindingCode::MissingEvidence), std::size_t{1});
  const Finding* missing = find_code(strict_loop->findings, FindingCode::MissingEvidence);
  CCA_CHECK(missing != nullptr);
  if (missing != nullptr) {
    CCA_CHECK_EQ(missing->severity, FindingSeverity::Error);
  }
  CCA_CHECK_EQ(strict_loop->totals.sum(), strict_loop->declared_installed);

  // Relaxing the flag makes the very same input determinate.
  AccountingInput relaxed = base_input(cca_test::strict_policy());
  relaxed.policy.require_installed_evidence = false;
  relaxed.contributions.push_back(contribution_node("unit.one", 1000));
  CCA_ASSIGN(relaxed_ledger, build(relaxed));
  const ScopeAccounting* relaxed_loop = sole_loop(relaxed_ledger);
  CCA_CHECK(relaxed_loop != nullptr);
  if (relaxed_loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(relaxed_loop->status, ClosureStatus::Closed);
  CCA_CHECK_EQ(relaxed_loop->totals.allocatable.milliwatts(), std::int64_t{1000});
  CCA_CHECK_EQ(relaxed_loop->totals.indeterminate.milliwatts(), std::int64_t{0});
  CCA_CHECK_EQ(count_code(relaxed_loop->findings, FindingCode::MissingEvidence), std::size_t{0});

  // Naming current primary evidence satisfies the requirement as well.
  AccountingInput evidenced = base_input(cca_test::strict_policy());
  evidenced.evidence.push_back(cca_test::make_evidence(
      "evidence.nameplate", EvidenceKind::Nameplate, cca_test::fixture_now(),
      evidenced.generations));
  Contribution named = contribution_node("unit.one", 1000);
  named.primary_evidence = id_of<EvidenceId>("evidence.nameplate");
  evidenced.contributions.push_back(named);
  CCA_ASSIGN(evidenced_ledger, build(evidenced));
  const ScopeAccounting* evidenced_loop = sole_loop(evidenced_ledger);
  CCA_CHECK(evidenced_loop != nullptr);
  if (evidenced_loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(evidenced_loop->status, ClosureStatus::Closed);
  CCA_CHECK_EQ(evidenced_loop->totals.allocatable.milliwatts(), std::int64_t{1000});

  // An unknown quantity is not a known quantity without evidence: it raises the
  // unknown-quantity finding, not a missing-evidence one.
  AccountingInput unmeasured = base_input(cca_test::strict_policy());
  Contribution unknown = contribution_node("unit.unknown", 0);
  unknown.installed = Measure<ThermalPower>::unknown(
      MeasureReason::SensorAbsent, BoundedText::from_validated("synthetic absent sensor"));
  unmeasured.contributions.push_back(unknown);
  CCA_ASSIGN(unmeasured_ledger, build(unmeasured));
  const ScopeAccounting* unmeasured_loop = sole_loop(unmeasured_ledger);
  CCA_CHECK(unmeasured_loop != nullptr);
  if (unmeasured_loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(count_code(unmeasured_loop->findings, FindingCode::UnknownInstalledQuantity),
               std::size_t{1});
  CCA_CHECK_EQ(count_code(unmeasured_loop->findings, FindingCode::MissingEvidence),
               std::size_t{0});
}

CCA_TEST(evidence_policy_requires_out_of_service_evidence) {
  AccountingInput bare = base_input(cca_test::strict_policy());
  bare.contributions.push_back(
      contribution_node("unit.stopped", 2000, ServiceState::OutOfService));
  CCA_ASSIGN(bare_ledger, build(bare));
  const ScopeAccounting* bare_loop = sole_loop(bare_ledger);
  CCA_CHECK(bare_loop != nullptr);
  if (bare_loop == nullptr) {
    return;
  }
  // An unevidenced out-of-service declaration is indeterminate, not
  // unavailable: it does not silently free the mass.
  CCA_CHECK_EQ(bare_loop->status, ClosureStatus::Indeterminate);
  CCA_CHECK_EQ(bare_loop->totals.unavailable.milliwatts(), std::int64_t{0});
  CCA_CHECK_EQ(bare_loop->totals.indeterminate.milliwatts(), std::int64_t{2000});
  CCA_CHECK_EQ(count_code(bare_loop->findings, FindingCode::MissingOutOfServiceEvidence),
               std::size_t{1});
  const Finding* missing =
      find_code(bare_loop->findings, FindingCode::MissingOutOfServiceEvidence);
  CCA_CHECK(missing != nullptr);
  if (missing != nullptr) {
    CCA_CHECK_EQ(missing->severity, FindingSeverity::Error);
  }

  // Relaxing both evidentiary flags makes the declaration determinate.
  AccountingInput relaxed = base_input(cca_test::strict_policy());
  relaxed.policy.require_out_of_service_evidence = false;
  relaxed.policy.require_installed_evidence = false;
  relaxed.contributions.push_back(
      contribution_node("unit.stopped", 2000, ServiceState::OutOfService));
  CCA_ASSIGN(relaxed_ledger, build(relaxed));
  const ScopeAccounting* relaxed_loop = sole_loop(relaxed_ledger);
  CCA_CHECK(relaxed_loop != nullptr);
  if (relaxed_loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(relaxed_loop->status, ClosureStatus::Closed);
  CCA_CHECK_EQ(relaxed_loop->totals.unavailable.milliwatts(), std::int64_t{2000});
  CCA_CHECK_EQ(relaxed_loop->totals.allocatable.milliwatts(), std::int64_t{0});

  // With evidence the declaration is determinate even under the strict policy.
  AccountingInput evidenced = base_input(cca_test::strict_policy());
  evidenced.evidence.push_back(cca_test::make_evidence(
      "evidence.order", EvidenceKind::OutOfServiceOrder, cca_test::fixture_now(),
      evidenced.generations));
  Contribution stopped = contribution_node("unit.stopped", 2000, ServiceState::OutOfService);
  stopped.primary_evidence = id_of<EvidenceId>("evidence.order");
  evidenced.contributions.push_back(stopped);
  CCA_ASSIGN(evidenced_ledger, build(evidenced));
  const ScopeAccounting* evidenced_loop = sole_loop(evidenced_ledger);
  CCA_CHECK(evidenced_loop != nullptr);
  if (evidenced_loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(evidenced_loop->status, ClosureStatus::Closed);
  CCA_CHECK_EQ(evidenced_loop->totals.unavailable.milliwatts(), std::int64_t{2000});
}

CCA_TEST(evidence_policy_requires_derate_evidence) {
  AccountingInput strict = base_input(cca_test::strict_policy());
  strict.evidence.push_back(cca_test::make_evidence(
      "evidence.nameplate", EvidenceKind::Nameplate, cca_test::fixture_now(),
      strict.generations));
  Contribution degraded = contribution_node("unit.degraded", 1000, ServiceState::Degraded);
  degraded.primary_evidence = id_of<EvidenceId>("evidence.nameplate");
  degraded.derates.push_back(
      DerateFactor(id_of<DerateId>("derate.one"), Ratio::of_ppm(500000).value()));
  strict.contributions.push_back(degraded);
  CCA_ASSIGN(strict_ledger, build(strict));
  const ScopeAccounting* strict_loop = sole_loop(strict_ledger);
  CCA_CHECK(strict_loop != nullptr);
  if (strict_loop == nullptr) {
    return;
  }
  // The derate is unsubstantiated, so the whole quantity is indeterminate and
  // nothing is quietly retained.
  CCA_CHECK_EQ(strict_loop->status, ClosureStatus::Indeterminate);
  CCA_CHECK_EQ(strict_loop->totals.indeterminate.milliwatts(), std::int64_t{1000});
  CCA_CHECK_EQ(strict_loop->totals.allocatable.milliwatts(), std::int64_t{0});
  CCA_CHECK_EQ(strict_loop->totals.degraded_loss.milliwatts(), std::int64_t{0});
  CCA_CHECK_EQ(count_code(strict_loop->findings, FindingCode::MissingDerateEvidence),
               std::size_t{1});
  const Finding* missing = find_code(strict_loop->findings, FindingCode::MissingDerateEvidence);
  CCA_CHECK(missing != nullptr);
  if (missing != nullptr) {
    CCA_CHECK_EQ(missing->severity, FindingSeverity::Error);
  }

  // Relaxing the flag makes the same derate determinate.
  AccountingInput relaxed = base_input(cca_test::strict_policy());
  relaxed.policy.require_derate_evidence = false;
  relaxed.evidence.push_back(cca_test::make_evidence(
      "evidence.nameplate", EvidenceKind::Nameplate, cca_test::fixture_now(),
      relaxed.generations));
  Contribution relaxed_degraded =
      contribution_node("unit.degraded", 1000, ServiceState::Degraded);
  relaxed_degraded.primary_evidence = id_of<EvidenceId>("evidence.nameplate");
  relaxed_degraded.derates.push_back(
      DerateFactor(id_of<DerateId>("derate.one"), Ratio::of_ppm(500000).value()));
  relaxed.contributions.push_back(relaxed_degraded);
  CCA_ASSIGN(relaxed_ledger, build(relaxed));
  const ScopeAccounting* relaxed_loop = sole_loop(relaxed_ledger);
  CCA_CHECK(relaxed_loop != nullptr);
  if (relaxed_loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(relaxed_loop->status, ClosureStatus::Closed);
  CCA_CHECK_EQ(relaxed_loop->totals.allocatable.milliwatts(), std::int64_t{500});
  CCA_CHECK_EQ(relaxed_loop->totals.degraded_loss.milliwatts(), std::int64_t{500});

  // A derate that names current evidence is determinate under the strict policy.
  AccountingInput evidenced = base_input(cca_test::strict_policy());
  evidenced.evidence.push_back(cca_test::make_evidence(
      "evidence.nameplate", EvidenceKind::Nameplate, cca_test::fixture_now(),
      evidenced.generations));
  evidenced.evidence.push_back(cca_test::make_evidence(
      "evidence.derate", EvidenceKind::CapacityTest, cca_test::fixture_now(),
      evidenced.generations));
  Contribution evidenced_degraded =
      contribution_node("unit.degraded", 1000, ServiceState::Degraded);
  evidenced_degraded.primary_evidence = id_of<EvidenceId>("evidence.nameplate");
  DerateFactor evidenced_derate(id_of<DerateId>("derate.one"), Ratio::of_ppm(500000).value());
  evidenced_derate.evidence = id_of<EvidenceId>("evidence.derate");
  evidenced_degraded.derates.push_back(evidenced_derate);
  evidenced.contributions.push_back(evidenced_degraded);
  CCA_ASSIGN(evidenced_ledger, build(evidenced));
  const ScopeAccounting* evidenced_loop = sole_loop(evidenced_ledger);
  CCA_CHECK(evidenced_loop != nullptr);
  if (evidenced_loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(evidenced_loop->status, ClosureStatus::Closed);
  CCA_CHECK_EQ(evidenced_loop->totals.allocatable.milliwatts(), std::int64_t{500});
}

CCA_TEST(evidence_stale_and_superseded_make_a_contribution_indeterminate) {
  // Stale: the record is older than the policy window.
  AccountingInput stale = base_input(cca_test::strict_policy());
  stale.evidence.push_back(cca_test::make_evidence(
      "evidence.old", EvidenceKind::Nameplate, cca_test::fixture_ago(2 * kDayMs),
      stale.generations));
  Contribution stale_unit = contribution_node("unit.one", 1000);
  stale_unit.primary_evidence = id_of<EvidenceId>("evidence.old");
  stale.contributions.push_back(stale_unit);
  CCA_ASSIGN(stale_ledger, build(stale));
  const ScopeAccounting* stale_loop = sole_loop(stale_ledger);
  CCA_CHECK(stale_loop != nullptr);
  if (stale_loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(stale_loop->status, ClosureStatus::Indeterminate);
  CCA_CHECK_EQ(stale_loop->totals.indeterminate.milliwatts(), std::int64_t{1000});
  CCA_CHECK_EQ(count_code(stale_loop->findings, FindingCode::StaleEvidence), std::size_t{1});
  const Finding* stale_finding = find_code(stale_loop->findings, FindingCode::StaleEvidence);
  CCA_CHECK(stale_finding != nullptr);
  if (stale_finding != nullptr) {
    CCA_CHECK_EQ(stale_finding->severity, FindingSeverity::Error);
    CCA_CHECK(stale_finding->evidence == id_of<EvidenceId>("evidence.old"));
  }

  // Superseded: the record was produced against an older topology generation.
  AccountingInput superseded = base_input(cca_test::strict_policy());
  superseded.generations.topology = TopologyGeneration::of(2).value();
  GenerationBundle older = GenerationBundle::initial();
  superseded.evidence.push_back(cca_test::make_evidence(
      "evidence.old-topology", EvidenceKind::Nameplate, cca_test::fixture_now(), older));
  Contribution superseded_unit = contribution_node("unit.one", 1000);
  superseded_unit.primary_evidence = id_of<EvidenceId>("evidence.old-topology");
  superseded.contributions.push_back(superseded_unit);
  CCA_ASSIGN(superseded_ledger, build(superseded));
  const ScopeAccounting* superseded_loop = sole_loop(superseded_ledger);
  CCA_CHECK(superseded_loop != nullptr);
  if (superseded_loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(superseded_loop->status, ClosureStatus::Indeterminate);
  CCA_CHECK_EQ(superseded_loop->totals.indeterminate.milliwatts(), std::int64_t{1000});
  CCA_CHECK_EQ(count_code(superseded_loop->findings, FindingCode::SupersededEvidence),
               std::size_t{1});
}

CCA_TEST(evidence_nameplate_mismatch_is_preserved_and_changes_nothing) {
  AccountingInput larger = base_input(cca_test::strict_policy());
  EvidenceRecord nameplate = cca_test::make_evidence(
      "evidence.nameplate", EvidenceKind::Nameplate, cca_test::fixture_now(),
      larger.generations);
  nameplate.declared_value = Measure<ThermalPower>::known(ThermalPower::of_milliwatts(900));
  larger.evidence.push_back(nameplate);
  Contribution unit = contribution_node("unit.one", 1000);
  unit.primary_evidence = id_of<EvidenceId>("evidence.nameplate");
  larger.contributions.push_back(unit);
  CCA_ASSIGN(larger_ledger, build(larger));
  const ScopeAccounting* larger_loop = sole_loop(larger_ledger);
  CCA_CHECK(larger_loop != nullptr);
  if (larger_loop == nullptr) {
    return;
  }
  // The accounted quantity is kept; only the signed difference is preserved.
  CCA_CHECK_EQ(larger_loop->status, ClosureStatus::Closed);
  CCA_CHECK_EQ(larger_loop->totals.allocatable.milliwatts(), std::int64_t{1000});
  CCA_CHECK_EQ(larger_loop->declared_installed.milliwatts(), std::int64_t{1000});
  CCA_CHECK_EQ(count_code(larger_loop->findings, FindingCode::NameplateMismatch),
               std::size_t{1});
  const Finding* mismatch = find_code(larger_loop->findings, FindingCode::NameplateMismatch);
  CCA_CHECK(mismatch != nullptr);
  if (mismatch != nullptr) {
    CCA_CHECK_EQ(mismatch->severity, FindingSeverity::Warning);
    CCA_CHECK(mismatch->amount.has_value());
    if (mismatch->amount.has_value()) {
      CCA_CHECK_EQ(mismatch->amount->milliwatts(), std::int64_t{100});
      CCA_CHECK(!mismatch->amount->is_negative());
    }
  }

  // The sign follows the direction of the disagreement.
  AccountingInput smaller = base_input(cca_test::strict_policy());
  EvidenceRecord over = cca_test::make_evidence(
      "evidence.nameplate", EvidenceKind::Nameplate, cca_test::fixture_now(),
      smaller.generations);
  over.declared_value = Measure<ThermalPower>::known(ThermalPower::of_milliwatts(1200));
  smaller.evidence.push_back(over);
  Contribution smaller_unit = contribution_node("unit.one", 1000);
  smaller_unit.primary_evidence = id_of<EvidenceId>("evidence.nameplate");
  smaller.contributions.push_back(smaller_unit);
  CCA_ASSIGN(smaller_ledger, build(smaller));
  const ScopeAccounting* smaller_loop = sole_loop(smaller_ledger);
  CCA_CHECK(smaller_loop != nullptr);
  if (smaller_loop == nullptr) {
    return;
  }
  const Finding* negative = find_code(smaller_loop->findings, FindingCode::NameplateMismatch);
  CCA_CHECK(negative != nullptr);
  if (negative != nullptr) {
    CCA_CHECK(negative->amount.has_value());
    if (negative->amount.has_value()) {
      CCA_CHECK_EQ(negative->amount->milliwatts(), std::int64_t{-200});
      CCA_CHECK(negative->amount->is_negative());
    }
  }
  CCA_CHECK_EQ(smaller_loop->totals.allocatable.milliwatts(), std::int64_t{1000});

  // Agreement, and evidence that is silent about a quantity, produce no finding.
  AccountingInput agreeing = base_input(cca_test::strict_policy());
  EvidenceRecord matching = cca_test::make_evidence(
      "evidence.nameplate", EvidenceKind::Nameplate, cca_test::fixture_now(),
      agreeing.generations);
  matching.declared_value = Measure<ThermalPower>::known(ThermalPower::of_milliwatts(1000));
  agreeing.evidence.push_back(matching);
  Contribution agreeing_unit = contribution_node("unit.one", 1000);
  agreeing_unit.primary_evidence = id_of<EvidenceId>("evidence.nameplate");
  agreeing.contributions.push_back(agreeing_unit);
  CCA_ASSIGN(agreeing_ledger, build(agreeing));
  const ScopeAccounting* agreeing_loop = sole_loop(agreeing_ledger);
  CCA_CHECK(agreeing_loop != nullptr);
  if (agreeing_loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(count_code(agreeing_loop->findings, FindingCode::NameplateMismatch),
               std::size_t{0});
}

CCA_TEST(evidence_future_timestamps_are_hard_errors) {
  const Timestamp now = cca_test::fixture_now();
  const Timestamp future = Timestamp::of_unix_milliseconds(now.unix_milliseconds() + 1);

  // A contribution observed after the accounting instant.
  AccountingInput late_contribution = base_input(cca_test::relaxed_policy());
  Contribution late = contribution_node("unit.late", 1000);
  late.observed_at = future;
  late_contribution.contributions.push_back(late);
  CCA_CHECK_CODE(AccountingLedger::build(late_contribution, now), ErrorCode::FutureTimestamp);

  // An evidence record observed after it.
  AccountingInput late_evidence = base_input(cca_test::relaxed_policy());
  late_evidence.evidence.push_back(cca_test::make_evidence(
      "evidence.late", EvidenceKind::Nameplate, future, late_evidence.generations));
  CCA_CHECK_CODE(AccountingLedger::build(late_evidence, now), ErrorCode::FutureTimestamp);

  // An evidence record recorded after it.
  AccountingInput recorded_late = base_input(cca_test::relaxed_policy());
  EvidenceRecord recorded = cca_test::make_evidence(
      "evidence.recorded", EvidenceKind::Nameplate, now, recorded_late.generations);
  recorded.recorded_at = future;
  recorded_late.evidence.push_back(recorded);
  CCA_CHECK_CODE(AccountingLedger::build(recorded_late, now), ErrorCode::FutureTimestamp);

  // A manifest declaration observed after it.
  AccountingInput late_manifest = base_input(cca_test::relaxed_policy());
  ManifestDeclaration declaration;
  declaration.id = id_of<ManifestId>("manifest.late");
  declaration.scope = id_of<ScopeId>("loop.a");
  declaration.medium = Medium::Air;
  declaration.declared_installed = ThermalPower::of_milliwatts(1000);
  declaration.observed_at = future;
  declaration.binding = EvidenceBinding::initial();
  late_manifest.manifest_declarations.push_back(declaration);
  CCA_CHECK_CODE(AccountingLedger::build(late_manifest, now), ErrorCode::FutureTimestamp);

  // Observed exactly at the accounting instant, all three are accepted.
  AccountingInput on_time = base_input(cca_test::relaxed_policy());
  Contribution punctual = contribution_node("unit.punctual", 1000);
  punctual.observed_at = now;
  on_time.contributions.push_back(punctual);
  on_time.evidence.push_back(
      cca_test::make_evidence("evidence.now", EvidenceKind::Nameplate, now, on_time.generations));
  ManifestDeclaration on_time_declaration;
  on_time_declaration.id = id_of<ManifestId>("manifest.now");
  on_time_declaration.scope = id_of<ScopeId>("loop.a");
  on_time_declaration.medium = Medium::Air;
  on_time_declaration.declared_installed = ThermalPower::of_milliwatts(1000);
  on_time_declaration.observed_at = now;
  on_time_declaration.binding = EvidenceBinding::initial();
  on_time.manifest_declarations.push_back(on_time_declaration);
  CCA_CHECK(AccountingLedger::build(on_time, now).ok());
}

}  // namespace
