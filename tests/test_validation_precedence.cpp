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

// Validation precedence. The documented order of the primary failure is
//
//   limits -> policy -> input size -> policy generation -> policy epoch ->
//   policy effectiveness -> duplicate identities -> scope tree -> references ->
//   group structure -> contribution content -> observation instants ->
//   total quantity ceiling
//
// Every case below violates at least two rules at once and is constructed so
// that ONLY the precedence order decides which error is reported. Every fixture
// is SYNTHETIC.

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

/// A structurally sound facility: site.alpha > zone.alpha > loop.a, one
/// equipment class, no contributions.
AccountingInput base_input() {
  AccountingInput input;
  input.generations = GenerationBundle::initial();
  input.policy = cca_test::relaxed_policy();
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

/// The primary failure of one input, failing the test when the input is
/// accepted at all.
Error primary_error(const AccountingInput& input, const Limits& limits = Limits{}) {
  const Result<AccountingLedger> result =
      AccountingLedger::build(input, cca_test::fixture_now(), limits);
  if (result.ok()) {
    CCA_FAIL("the input was accepted where a validation failure was expected");
    return Error::of(ErrorCode::Ok);
  }
  return result.error();
}

bool contains(std::string_view text, std::string_view needle) {
  return text.find(needle) != std::string_view::npos;
}

DerateFactor factor_derate(std::string_view id, std::uint32_t ppm_value) {
  return DerateFactor(id_of<DerateId>(id), Ratio::of_ppm(ppm_value).value());
}

// ---------------------------------------------------------------------------

CCA_TEST(precedence_limits_before_policy_and_structure) {
  // Violations: a zeroed limit, a policy field that is out of range, a
  // duplicate scope identifier and an unknown parent.
  AccountingInput input = base_input();
  input.scopes.push_back(input.scopes.back());
  input.scopes[1].parent = id_of<ScopeId>("zone.absent");
  input.policy.max_derate_factors = 0;

  Limits zeroed;
  zeroed.max_scopes = 0;
  CCA_CHECK_CODE(AccountingLedger::build(input, cca_test::fixture_now(), zeroed),
                 ErrorCode::InvalidArgument);

  Limits raised;
  raised.max_scopes = kMaxScopes + 1;
  CCA_CHECK_CODE(AccountingLedger::build(input, cca_test::fixture_now(), raised),
                 ErrorCode::LimitExceeded);

  // With a usable limit the policy field is the primary failure.
  CCA_CHECK_CODE(AccountingLedger::build(input, cca_test::fixture_now()),
                 ErrorCode::DerateOutOfRange);
}

CCA_TEST(precedence_input_size_before_generation_agreement) {
  // Violations: more scopes than the lowered bound allows AND a policy
  // generation that is not the accounting generation.
  AccountingInput input = base_input();
  input.policy.generation = PolicyGeneration::of(2).value();
  Limits lowered;
  lowered.max_scopes = 1;
  CCA_CHECK_CODE(AccountingLedger::build(input, cca_test::fixture_now(), lowered),
                 ErrorCode::LimitExceeded);
  CCA_CHECK_CODE(AccountingLedger::build(input, cca_test::fixture_now()),
                 ErrorCode::PolicyGenerationMismatch);
}

CCA_TEST(precedence_policy_generation_before_epoch_and_effectiveness) {
  // Violations: the policy generation, the policy epoch and the effective
  // instant are all wrong at once.
  AccountingInput input = base_input();
  input.policy.generation = PolicyGeneration::of(2).value();
  input.policy.epoch = ControlPlaneEpoch::of(2).value();
  input.policy.effective_from =
      Timestamp::of_unix_milliseconds(cca_test::fixture_now().unix_milliseconds() + 1);
  CCA_CHECK_CODE(AccountingLedger::build(input, cca_test::fixture_now()),
                 ErrorCode::PolicyGenerationMismatch);

  // Repairing the generation exposes the epoch.
  input.policy.generation = input.generations.policy;
  CCA_CHECK_CODE(AccountingLedger::build(input, cca_test::fixture_now()),
                 ErrorCode::CrossEpochAuthority);

  // Repairing the epoch exposes the effectiveness.
  input.policy.epoch = input.generations.epoch;
  CCA_CHECK_CODE(AccountingLedger::build(input, cca_test::fixture_now()),
                 ErrorCode::FutureGeneration);
}

CCA_TEST(precedence_duplicate_identity_before_scope_tree) {
  // Violations: loop.a appears twice AND zone.alpha names a parent that does
  // not exist.
  AccountingInput input = base_input();
  input.scopes.push_back(input.scopes.back());
  input.scopes[1].parent = id_of<ScopeId>("zone.absent");
  const Error error = primary_error(input);
  CCA_CHECK_EQ(error.code(), ErrorCode::DuplicateScope);
  CCA_CHECK(contains(error.subject(), "loop.a"));
}

CCA_TEST(precedence_identity_before_tree_and_references) {
  // Violations: the equipment class repeats, a zone is an orphan, and a
  // contribution names a scope that does not exist.
  AccountingInput input = base_input();
  input.equipment_classes.push_back(input.equipment_classes.front());
  input.scopes.push_back(scope_node("zone.orphan", ScopeKind::Zone, "", Medium::Air));
  input.contributions.push_back(
      contribution_node("unit.absent", "loop.absent", "class.crah", 1000));
  const Error error = primary_error(input);
  CCA_CHECK_EQ(error.code(), ErrorCode::DuplicateIdentity);
  CCA_CHECK(contains(error.subject(), "equipment class"));
}

CCA_TEST(precedence_scope_tree_before_references) {
  // Violations: an orphan zone AND a contribution whose home scope does not
  // exist.
  AccountingInput input = base_input();
  input.scopes.push_back(scope_node("zone.orphan", ScopeKind::Zone, "", Medium::Air));
  input.contributions.push_back(
      contribution_node("unit.absent", "loop.absent", "class.crah", 1000));
  const Error error = primary_error(input);
  CCA_CHECK_EQ(error.code(), ErrorCode::OrphanScope);
  CCA_CHECK(contains(error.subject(), "zone.orphan"));
}

CCA_TEST(precedence_scope_tree_before_contribution_content_and_instants) {
  // Violations: an orphan zone, a derate that mixes kinds, and a contribution
  // observed after the accounting instant.
  AccountingInput input = base_input();
  input.scopes.push_back(scope_node("zone.orphan", ScopeKind::Zone, "", Medium::Air));
  Contribution contribution = contribution_node("unit.bad", "loop.a", "class.crah", 1000,
                                                 ServiceState::Degraded);
  DerateFactor mixed = factor_derate("derate.mixed", 500000);
  mixed.absolute = ThermalPower::of_milliwatts(5);
  contribution.derates.push_back(mixed);
  contribution.observed_at =
      Timestamp::of_unix_milliseconds(cca_test::fixture_now().unix_milliseconds() + 1);
  input.contributions.push_back(contribution);
  CCA_CHECK_CODE(AccountingLedger::build(input, cca_test::fixture_now()),
                 ErrorCode::OrphanScope);
}

CCA_TEST(precedence_references_before_group_structure) {
  // Violations: a contribution names an equipment class that does not exist AND
  // a group does not state how its members combine.
  AccountingInput input = base_input();
  input.contributions.push_back(
      contribution_node("unit.one", "loop.a", "class.absent", 1000));
  ContributionGroup group;
  group.id = id_of<ContributionGroupId>("group.unspecified");
  group.scope = id_of<ScopeId>("loop.a");
  group.classification = ContributionClass::Unspecified;
  group.medium = Medium::Air;
  input.groups.push_back(group);
  const Error error = primary_error(input);
  CCA_CHECK_EQ(error.code(), ErrorCode::ClassMismatch);
  CCA_CHECK(contains(error.subject(), "unit.one"));
}

CCA_TEST(precedence_references_before_contribution_content) {
  // Violations: an unknown home scope AND a derate whose kind and payload
  // disagree.
  AccountingInput input = base_input();
  Contribution contribution = contribution_node("unit.bad", "loop.absent", "class.crah", 1000,
                                                 ServiceState::Degraded);
  DerateFactor mixed = factor_derate("derate.mixed", 500000);
  mixed.absolute = ThermalPower::of_milliwatts(5);
  contribution.derates.push_back(mixed);
  input.contributions.push_back(contribution);
  CCA_CHECK_CODE(AccountingLedger::build(input, cca_test::fixture_now()),
                 ErrorCode::UnknownScope);
  CCA_CHECK_CODE(AccountingLedger::validate_input(input, cca_test::fixture_now()),
                 ErrorCode::UnknownScope);

  // Violations: a group that does not exist AND the same derate problem.
  AccountingInput missing_group = base_input();
  Contribution grouped = contribution_node("unit.bad", "loop.a", "class.crah", 1000,
                                           ServiceState::Degraded);
  grouped.classification = ContributionClass::Substitutive;
  grouped.group = id_of<ContributionGroupId>("group.absent");
  grouped.derates.push_back(mixed);
  missing_group.contributions.push_back(grouped);
  CCA_CHECK_CODE(AccountingLedger::build(missing_group, cca_test::fixture_now()),
                 ErrorCode::ContributionGroupMissing);
}

CCA_TEST(precedence_group_structure_before_contribution_content) {
  // Violations: a group that never states how its members combine AND a
  // contribution with no equipment name.
  AccountingInput input = base_input();
  ContributionGroup group;
  group.id = id_of<ContributionGroupId>("group.unspecified");
  group.scope = id_of<ScopeId>("loop.a");
  group.classification = ContributionClass::Unspecified;
  group.medium = Medium::Air;
  input.groups.push_back(group);
  Contribution unnamed = contribution_node("unit.unnamed", "loop.a", "class.crah", 1000);
  unnamed.equipment = EquipmentId();
  input.contributions.push_back(unnamed);
  const Error error = primary_error(input);
  CCA_CHECK_EQ(error.code(), ErrorCode::ClassificationRequired);
  CCA_CHECK(contains(error.subject(), "group.unspecified"));
}

CCA_TEST(precedence_contribution_content_field_order) {
  // Classification before service state.
  AccountingInput unclassified = base_input();
  Contribution silent = contribution_node("unit.silent", "loop.a", "class.crah", 1000);
  silent.classification = ContributionClass::Unspecified;
  silent.service = ServiceState::Unspecified;
  unclassified.contributions.push_back(silent);
  CCA_CHECK_CODE(AccountingLedger::build(unclassified, cca_test::fixture_now()),
                 ErrorCode::ClassificationRequired);

  // The service state before the medium.
  AccountingInput medium = base_input();
  Contribution wrong_medium = contribution_node("unit.wrong", "loop.a", "class.crah", 1000);
  wrong_medium.service = ServiceState::Unspecified;
  wrong_medium.medium = Medium::Liquid;
  medium.contributions.push_back(wrong_medium);
  CCA_CHECK_CODE(AccountingLedger::build(medium, cca_test::fixture_now()),
                 ErrorCode::MissingRequiredField);

  // The medium before the derate rules.
  AccountingInput derate = base_input();
  Contribution both = contribution_node("unit.both", "loop.a", "class.crah", 1000,
                                        ServiceState::Degraded);
  both.medium = Medium::Liquid;
  DerateFactor mixed = factor_derate("derate.mixed", 500000);
  mixed.absolute = ThermalPower::of_milliwatts(5);
  both.derates.push_back(mixed);
  derate.contributions.push_back(both);
  CCA_CHECK_CODE(AccountingLedger::build(derate, cca_test::fixture_now()),
                 ErrorCode::MediumMismatch);

  // The derate rules before group membership.
  AccountingInput membership = base_input();
  Contribution grouped = contribution_node("unit.grouped", "loop.a", "class.crah", 1000,
                                           ServiceState::Degraded);
  grouped.classification = ContributionClass::Substitutive;
  grouped.derates.push_back(mixed);
  membership.contributions.push_back(grouped);
  CCA_CHECK_CODE(AccountingLedger::build(membership, cca_test::fixture_now()),
                 ErrorCode::DerateOutOfRange);
}

CCA_TEST(precedence_contribution_content_before_observation_instant) {
  // Violations: a malformed derate AND an observation after the accounting
  // instant.
  AccountingInput input = base_input();
  Contribution contribution = contribution_node("unit.bad", "loop.a", "class.crah", 1000,
                                                 ServiceState::Degraded);
  DerateFactor mixed = factor_derate("derate.mixed", 500000);
  mixed.absolute = ThermalPower::of_milliwatts(5);
  contribution.derates.push_back(mixed);
  contribution.observed_at =
      Timestamp::of_unix_milliseconds(cca_test::fixture_now().unix_milliseconds() + 1);
  input.contributions.push_back(contribution);
  CCA_CHECK_CODE(AccountingLedger::build(input, cca_test::fixture_now()),
                 ErrorCode::DerateOutOfRange);

  // Violations: a degraded contribution with no derate AND the same future
  // observation.
  AccountingInput bare = base_input();
  Contribution degraded = contribution_node("unit.bare", "loop.a", "class.crah", 1000,
                                            ServiceState::Degraded);
  degraded.observed_at =
      Timestamp::of_unix_milliseconds(cca_test::fixture_now().unix_milliseconds() + 1);
  bare.contributions.push_back(degraded);
  CCA_CHECK_CODE(AccountingLedger::build(bare, cca_test::fixture_now()),
                 ErrorCode::PolicyViolation);
}

CCA_TEST(precedence_observation_instants_are_checked_in_record_order) {
  // All three kinds of record are observed after the accounting instant: the
  // contribution is named first, then the evidence, then the declaration.
  const Timestamp future =
      Timestamp::of_unix_milliseconds(cca_test::fixture_now().unix_milliseconds() + 1);

  AccountingInput both = base_input();
  Contribution late = contribution_node("unit.late", "loop.a", "class.crah", 1000);
  late.observed_at = future;
  both.contributions.push_back(late);
  EvidenceRecord late_evidence = cca_test::make_evidence(
      "evidence.late", EvidenceKind::Nameplate, future, both.generations);
  both.evidence.push_back(late_evidence);
  ManifestDeclaration late_declaration;
  late_declaration.id = id_of<ManifestId>("manifest.late");
  late_declaration.scope = id_of<ScopeId>("loop.a");
  late_declaration.medium = Medium::Air;
  late_declaration.declared_installed = ThermalPower::of_milliwatts(1000);
  late_declaration.observed_at = future;
  late_declaration.binding = EvidenceBinding::initial();
  both.manifest_declarations.push_back(late_declaration);
  const Error error = primary_error(both);
  CCA_CHECK_EQ(error.code(), ErrorCode::FutureTimestamp);
  CCA_CHECK(contains(error.subject(), "contribution"));

  // Removing the contribution exposes the evidence.
  AccountingInput evidence_only = both;
  evidence_only.contributions.clear();
  const Error evidence_error = primary_error(evidence_only);
  CCA_CHECK_EQ(evidence_error.code(), ErrorCode::FutureTimestamp);
  CCA_CHECK(contains(evidence_error.subject(), "evidence"));

  // Removing the evidence exposes the declaration.
  AccountingInput declaration_only = evidence_only;
  declaration_only.evidence.clear();
  const Error declaration_error = primary_error(declaration_only);
  CCA_CHECK_EQ(declaration_error.code(), ErrorCode::FutureTimestamp);
  CCA_CHECK(contains(declaration_error.subject(), "manifest declaration"));
}

CCA_TEST(precedence_observation_instant_before_quantity_ceiling) {
  // Violations: the accounted total exceeds the domain ceiling AND one
  // contribution is observed after the accounting instant.
  AccountingInput input = base_input();
  const std::int64_t half = kMaxThermalPowerMilliwatts / 2 + 1;
  Contribution first = contribution_node("unit.first", "loop.a", "class.crah", half);
  Contribution second = contribution_node("unit.second", "loop.a", "class.crah", half);
  second.observed_at =
      Timestamp::of_unix_milliseconds(cca_test::fixture_now().unix_milliseconds() + 1);
  input.contributions.push_back(first);
  input.contributions.push_back(second);
  CCA_CHECK_CODE(AccountingLedger::build(input, cca_test::fixture_now()),
                 ErrorCode::FutureTimestamp);

  // Without the future observation the ceiling is the primary failure.
  input.contributions[1].observed_at = cca_test::fixture_now();
  CCA_CHECK_CODE(AccountingLedger::build(input, cca_test::fixture_now()),
                 ErrorCode::OutOfRange);

  // Exactly the ceiling is accepted.
  AccountingInput exact = base_input();
  exact.contributions.push_back(contribution_node("unit.first", "loop.a", "class.crah", half));
  exact.contributions.push_back(
      contribution_node("unit.second", "loop.a", "class.crah", kMaxThermalPowerMilliwatts - half));
  CCA_CHECK(AccountingLedger::build(exact, cca_test::fixture_now()).ok());
}

CCA_TEST(precedence_is_deterministic_under_input_order) {
  // The duplicate scope and the unknown parent are both present; the answer does
  // not depend on where the duplicate sits in the vector.
  AccountingInput trailing = base_input();
  trailing.scopes.push_back(trailing.scopes.front());
  trailing.scopes[1].parent = id_of<ScopeId>("zone.absent");
  AccountingInput leading = base_input();
  leading.scopes.insert(leading.scopes.begin(), leading.scopes.back());
  leading.scopes[2].parent = id_of<ScopeId>("zone.absent");
  CCA_CHECK_EQ(primary_error(trailing).code(), ErrorCode::DuplicateScope);
  CCA_CHECK_EQ(primary_error(leading).code(), ErrorCode::DuplicateScope);

  // The unknown scope reference and the malformed derate are both present.
  AccountingInput forward = base_input();
  Contribution bad = contribution_node("unit.bad", "loop.absent", "class.crah", 1000,
                                       ServiceState::Degraded);
  DerateFactor mixed = factor_derate("derate.mixed", 500000);
  mixed.absolute = ThermalPower::of_milliwatts(5);
  bad.derates.push_back(mixed);
  forward.contributions.push_back(bad);
  forward.contributions.push_back(contribution_node("unit.good", "loop.a", "class.crah", 1000));
  AccountingInput reversed = base_input();
  reversed.contributions.push_back(
      contribution_node("unit.good", "loop.a", "class.crah", 1000));
  reversed.contributions.push_back(bad);
  CCA_CHECK_EQ(primary_error(forward).code(), ErrorCode::UnknownScope);
  CCA_CHECK_EQ(primary_error(reversed).code(), ErrorCode::UnknownScope);
}

}  // namespace
