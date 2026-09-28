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

// Substitutive spare, mutual exclusion, reserve-only mass and equipment classes
// that never contribute to installed capacity. In every case the withheld mass
// carries exactly one reason and the per-reason breakdown re-sums to the
// withheld total. Every fixture here is SYNTHETIC.

#include <cstddef>
#include <cstdint>
#include <optional>
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
// A file-local synthetic bench of grouped contributions.
// ---------------------------------------------------------------------------

struct GroupSpec {
  std::string id;
  ContributionClass classification = ContributionClass::Additive;
  std::uint32_t required_concurrent = 0;
};

struct Fill {
  std::string id;
  std::string equipment_class = "class.crah";
  std::int64_t installed_mw = 100;
  std::uint32_t priority = 0;
  ServiceState service = ServiceState::InService;
  std::string group;
  ContributionClass classification = ContributionClass::Additive;
  std::uint32_t derate_ppm = 0;
};

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

EquipmentClass class_node(std::string_view id, Medium medium, bool contributes) {
  EquipmentClass klass;
  klass.id = id_of<EquipmentClassId>(id);
  klass.kind = EquipmentClassKind::Other;
  klass.medium = medium;
  klass.contributes_to_installed = contributes;
  return klass;
}

Result<AccountingLedger> build_members(const std::vector<GroupSpec>& groups,
                                       const std::vector<Fill>& fills) {
  AccountingInput input;
  input.generations = GenerationBundle::initial();
  input.policy = cca_test::relaxed_policy();
  input.scopes.push_back(scope_node("site.alpha", ScopeKind::Site, "", Medium::Air));
  input.scopes.push_back(scope_node("zone.alpha", ScopeKind::Zone, "site.alpha", Medium::Air));
  input.scopes.push_back(scope_node("loop.a", ScopeKind::Loop, "zone.alpha", Medium::Air));
  input.equipment_classes.push_back(class_node("class.crah", Medium::Air, true));
  input.equipment_classes.push_back(class_node("class.monitor", Medium::Air, false));

  for (const GroupSpec& spec : groups) {
    ContributionGroup group;
    group.id = id_of<ContributionGroupId>(spec.id);
    group.scope = id_of<ScopeId>("loop.a");
    group.classification = spec.classification;
    group.medium = Medium::Air;
    group.required_concurrent = spec.required_concurrent;
    group.redundancy = RedundancyClass::N;
    input.groups.push_back(group);
  }

  for (const Fill& fill : fills) {
    Contribution contribution;
    contribution.id = id_of<ContributionId>(fill.id);
    contribution.home_scope = id_of<ScopeId>("loop.a");
    contribution.equipment = id_of<EquipmentId>("equipment." + fill.id);
    contribution.equipment_class = id_of<EquipmentClassId>(fill.equipment_class);
    contribution.medium = Medium::Air;
    contribution.classification = fill.classification;
    contribution.installed =
        Measure<ThermalPower>::known(ThermalPower::of_milliwatts(fill.installed_mw));
    contribution.service = fill.service;
    contribution.priority = fill.priority;
    if (!fill.group.empty()) {
      contribution.group = id_of<ContributionGroupId>(fill.group);
    }
    if (fill.derate_ppm != 0U) {
      contribution.derates.push_back(DerateFactor(
          id_of<DerateId>("derate." + fill.id), Ratio::of_ppm(fill.derate_ppm).value()));
    }
    contribution.observed_at = cca_test::fixture_now();
    contribution.binding = EvidenceBinding::initial();
    input.contributions.push_back(contribution);
  }
  return AccountingLedger::build(input, cca_test::fixture_now());
}

Fill fill_of(std::string id, std::int64_t installed_mw, std::uint32_t priority,
             std::string group, ContributionClass classification) {
  Fill fill;
  fill.id = std::move(id);
  fill.installed_mw = installed_mw;
  fill.priority = priority;
  fill.group = std::move(group);
  fill.classification = classification;
  return fill;
}

const ContributionAllocation* allocation_of(const AccountingLedger& ledger,
                                            std::string_view contribution) {
  const ContributionAccounting* record =
      ledger.find_contribution(id_of<ContributionId>(contribution));
  if (record == nullptr || record->allocations.empty()) {
    return nullptr;
  }
  return &record->allocations.front();
}

std::int64_t allocated_of(const AccountingLedger& ledger, std::string_view contribution) {
  const ContributionAllocation* allocation = allocation_of(ledger, contribution);
  return allocation == nullptr ? -1 : allocation->allocatable.milliwatts();
}

std::int64_t withheld_of(const AccountingLedger& ledger, std::string_view contribution) {
  const ContributionAllocation* allocation = allocation_of(ledger, contribution);
  return allocation == nullptr ? -1 : allocation->withheld.milliwatts();
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

const ScopeAccounting* sole_loop(const AccountingLedger& ledger) {
  return ledger.find_scope(id_of<ScopeId>("loop.a"));
}

// ---------------------------------------------------------------------------

CCA_TEST(mutual_substitutive_keeps_exactly_required_concurrent) {
  const std::vector<GroupSpec> groups{
      {"group.sub", ContributionClass::Substitutive, 1}};
  const std::vector<Fill> fills{
      fill_of("m.a", 100, 5, "group.sub", ContributionClass::Substitutive),
      fill_of("m.b", 200, 1, "group.sub", ContributionClass::Substitutive),
      fill_of("m.c", 300, 3, "group.sub", ContributionClass::Substitutive)};
  CCA_ASSIGN(ledger, build_members(groups, fills));

  // Member order is (priority ascending, then identifier ascending): m.b, m.c,
  // m.a. Only the first required_concurrent members stay allocatable.
  CCA_CHECK_EQ(allocated_of(ledger, "m.b"), std::int64_t{200});
  CCA_CHECK_EQ(allocated_of(ledger, "m.c"), std::int64_t{0});
  CCA_CHECK_EQ(allocated_of(ledger, "m.a"), std::int64_t{0});
  CCA_CHECK_EQ(withheld_of(ledger, "m.c"), std::int64_t{300});
  CCA_CHECK_EQ(withheld_of(ledger, "m.a"), std::int64_t{100});
  CCA_CHECK_EQ(withheld_of(ledger, "m.b"), std::int64_t{0});

  const ContributionAllocation* spare = allocation_of(ledger, "m.c");
  CCA_CHECK(spare != nullptr);
  if (spare == nullptr) {
    return;
  }
  CCA_CHECK(spare->withheld_reason.has_value());
  if (spare->withheld_reason.has_value()) {
    CCA_CHECK_EQ(spare->withheld_reason.value(), WithheldReason::SubstitutiveSpare);
  }
  CCA_CHECK_EQ(spare->withheld_by_reason.of(WithheldReason::SubstitutiveSpare).milliwatts(),
               std::int64_t{300});

  const ScopeAccounting* loop = sole_loop(ledger);
  CCA_CHECK(loop != nullptr);
  if (loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(loop->declared_installed.milliwatts(), std::int64_t{600});
  CCA_CHECK_EQ(loop->totals.allocatable.milliwatts(), std::int64_t{200});
  CCA_CHECK_EQ(loop->totals.withheld.milliwatts(), std::int64_t{400});
  CCA_CHECK_EQ(loop->withheld.of(WithheldReason::SubstitutiveSpare).milliwatts(),
               std::int64_t{400});
  CCA_CHECK(loop->withheld.verify_total().ok());
  CCA_CHECK_EQ(loop->totals.sum(), loop->declared_installed);
  CCA_CHECK_EQ(count_code(loop->findings, FindingCode::SubstitutiveSpareWithheld),
               std::size_t{2});
  CCA_CHECK_EQ(loop->withheld.of(WithheldReason::SubstitutiveSpare).milliwatts(),
               loop->totals.withheld.milliwatts());

  // required_concurrent equal to the member count withholds nothing.
  const std::vector<GroupSpec> all_required{
      {"group.sub", ContributionClass::Substitutive, 3}};
  CCA_ASSIGN(all, build_members(all_required, fills));
  const ScopeAccounting* all_loop = sole_loop(all);
  CCA_CHECK(all_loop != nullptr);
  if (all_loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(all_loop->totals.withheld.milliwatts(), std::int64_t{0});
  CCA_CHECK_EQ(all_loop->totals.allocatable.milliwatts(), std::int64_t{600});
  CCA_CHECK_EQ(count_code(all_loop->findings, FindingCode::SubstitutiveSpareWithheld),
               std::size_t{0});

  // A substitutive group must declare how many members it requires at once.
  const std::vector<GroupSpec> undeclared{
      {"group.sub", ContributionClass::Substitutive, 0}};
  CCA_CHECK_CODE(build_members(undeclared, fills), ErrorCode::PolicyViolation);
  const std::vector<GroupSpec> too_many{
      {"group.sub", ContributionClass::Substitutive, 257}};
  CCA_CHECK_CODE(build_members(too_many, fills), ErrorCode::PolicyViolation);
}

CCA_TEST(mutual_substitutive_is_independent_of_input_order) {
  const std::vector<GroupSpec> groups{
      {"group.sub", ContributionClass::Substitutive, 2}};
  const std::vector<Fill> fills{
      fill_of("m.a", 100, 4, "group.sub", ContributionClass::Substitutive),
      fill_of("m.b", 200, 2, "group.sub", ContributionClass::Substitutive),
      fill_of("m.c", 300, 0, "group.sub", ContributionClass::Substitutive),
      fill_of("m.d", 400, 0, "group.sub", ContributionClass::Substitutive)};
  CCA_ASSIGN(reference, build_members(groups, fills));

  // m.c and m.d share priority 0, so the identifier decides: m.c, m.d, m.b, m.a.
  CCA_CHECK_EQ(allocated_of(reference, "m.c"), std::int64_t{300});
  CCA_CHECK_EQ(allocated_of(reference, "m.d"), std::int64_t{400});
  CCA_CHECK_EQ(allocated_of(reference, "m.b"), std::int64_t{0});
  CCA_CHECK_EQ(allocated_of(reference, "m.a"), std::int64_t{0});
  CCA_CHECK_EQ(withheld_of(reference, "m.b"), std::int64_t{200});
  CCA_CHECK_EQ(withheld_of(reference, "m.a"), std::int64_t{100});

  const std::vector<Fill> reversed{fills[3], fills[2], fills[1], fills[0]};
  CCA_ASSIGN(candidate, build_members(groups, reversed));
  CCA_CHECK_EQ(candidate.digest(), reference.digest());
  for (const Fill& fill : fills) {
    CCA_CHECK_EQ(allocated_of(candidate, fill.id), allocated_of(reference, fill.id));
    CCA_CHECK_EQ(withheld_of(candidate, fill.id), withheld_of(reference, fill.id));
  }
}

CCA_TEST(mutual_exclusion_counts_exactly_one_member) {
  const std::vector<GroupSpec> groups{
      {"group.mutex", ContributionClass::MutuallyExclusive, 0}};
  const std::vector<Fill> fills{
      fill_of("m.a", 100, 0, "group.mutex", ContributionClass::MutuallyExclusive),
      fill_of("m.b", 200, 0, "group.mutex", ContributionClass::MutuallyExclusive),
      fill_of("m.c", 300, 0, "group.mutex", ContributionClass::MutuallyExclusive)};
  CCA_ASSIGN(ledger, build_members(groups, fills));

  CCA_CHECK_EQ(allocated_of(ledger, "m.a"), std::int64_t{100});
  CCA_CHECK_EQ(allocated_of(ledger, "m.b"), std::int64_t{0});
  CCA_CHECK_EQ(allocated_of(ledger, "m.c"), std::int64_t{0});
  CCA_CHECK_EQ(withheld_of(ledger, "m.b"), std::int64_t{200});
  CCA_CHECK_EQ(withheld_of(ledger, "m.c"), std::int64_t{300});
  const ContributionAllocation* excluded = allocation_of(ledger, "m.c");
  CCA_CHECK(excluded != nullptr);
  if (excluded == nullptr) {
    return;
  }
  CCA_CHECK(excluded->withheld_reason.has_value());
  if (excluded->withheld_reason.has_value()) {
    CCA_CHECK_EQ(excluded->withheld_reason.value(), WithheldReason::MutualExclusion);
  }

  const ScopeAccounting* loop = sole_loop(ledger);
  CCA_CHECK(loop != nullptr);
  if (loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(loop->declared_installed.milliwatts(), std::int64_t{600});
  CCA_CHECK_EQ(loop->totals.allocatable.milliwatts(), std::int64_t{100});
  CCA_CHECK_EQ(loop->totals.withheld.milliwatts(), std::int64_t{500});
  CCA_CHECK_EQ(loop->withheld.of(WithheldReason::MutualExclusion).milliwatts(),
               std::int64_t{500});
  CCA_CHECK(loop->withheld.verify_total().ok());
  CCA_CHECK_EQ(loop->totals.sum(), loop->declared_installed);
  // The selected member is named, and each withheld member is named.
  CCA_CHECK_EQ(count_code(loop->findings, FindingCode::MutualExclusionResolved),
               std::size_t{3});
  CCA_CHECK_EQ(count_code(loop->findings, FindingCode::SubstitutiveSpareWithheld),
               std::size_t{0});

  // A declared priority decides first.
  const std::vector<Fill> prioritised{
      fill_of("m.a", 100, 2, "group.mutex", ContributionClass::MutuallyExclusive),
      fill_of("m.b", 200, 0, "group.mutex", ContributionClass::MutuallyExclusive),
      fill_of("m.c", 300, 1, "group.mutex", ContributionClass::MutuallyExclusive)};
  CCA_ASSIGN(priority_ledger, build_members(groups, prioritised));
  CCA_CHECK_EQ(allocated_of(priority_ledger, "m.b"), std::int64_t{200});
  CCA_CHECK_EQ(allocated_of(priority_ledger, "m.a"), std::int64_t{0});
  CCA_CHECK_EQ(allocated_of(priority_ledger, "m.c"), std::int64_t{0});

  // A group of one counts its single member and withholds nothing.
  const std::vector<Fill> single{
      fill_of("m.only", 500, 0, "group.mutex", ContributionClass::MutuallyExclusive)};
  CCA_ASSIGN(single_ledger, build_members(groups, single));
  CCA_CHECK_EQ(allocated_of(single_ledger, "m.only"), std::int64_t{500});
  CCA_CHECK_EQ(withheld_of(single_ledger, "m.only"), std::int64_t{0});
  const ScopeAccounting* single_loop = sole_loop(single_ledger);
  CCA_CHECK(single_loop != nullptr);
  if (single_loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(single_loop->totals.withheld.milliwatts(), std::int64_t{0});
}

CCA_TEST(mutual_reserve_only_is_never_allocatable) {
  // ReserveOnly is reserve by construction: it needs no group of its own and is
  // never allocatable whatever else is true.
  const std::vector<Fill> fills{
      fill_of("m.reserve", 1000, 0, "", ContributionClass::ReserveOnly)};
  CCA_ASSIGN(ledger, build_members({}, fills));

  const ScopeAccounting* loop = sole_loop(ledger);
  CCA_CHECK(loop != nullptr);
  if (loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(loop->declared_installed.milliwatts(), std::int64_t{1000});
  CCA_CHECK_EQ(loop->totals.allocatable.milliwatts(), std::int64_t{0});
  CCA_CHECK_EQ(loop->totals.withheld.milliwatts(), std::int64_t{1000});
  CCA_CHECK_EQ(loop->withheld.of(WithheldReason::ReserveOnlyClass).milliwatts(),
               std::int64_t{1000});
  CCA_CHECK(loop->withheld.verify_total().ok());
  CCA_CHECK_EQ(count_code(loop->findings, FindingCode::ReserveOnlyWithheld), std::size_t{1});
  const ContributionAllocation* allocation = allocation_of(ledger, "m.reserve");
  CCA_CHECK(allocation != nullptr);
  if (allocation == nullptr) {
    return;
  }
  CCA_CHECK(allocation->withheld_reason.has_value());
  if (allocation->withheld_reason.has_value()) {
    CCA_CHECK_EQ(allocation->withheld_reason.value(), WithheldReason::ReserveOnlyClass);
  }

  // The class does not require a group, so naming one is refused.
  const std::vector<GroupSpec> groups{
      {"group.reserve", ContributionClass::ReserveOnly, 0}};
  std::vector<Fill> grouped = fills;
  grouped[0].group = "group.reserve";
  CCA_CHECK_CODE(build_members(groups, grouped), ErrorCode::PolicyViolation);

  // Whatever else is true: a derated reserve-only contribution withholds the
  // whole retained quantity, and only the degraded part is a loss.
  std::vector<Fill> degraded = fills;
  degraded[0].installed_mw = 1000;
  degraded[0].derate_ppm = 600000;
  degraded[0].service = ServiceState::Degraded;
  CCA_ASSIGN(degraded_ledger, build_members({}, degraded));
  const ScopeAccounting* degraded_loop = sole_loop(degraded_ledger);
  CCA_CHECK(degraded_loop != nullptr);
  if (degraded_loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(degraded_loop->declared_installed.milliwatts(), std::int64_t{1000});
  CCA_CHECK_EQ(degraded_loop->totals.degraded_loss.milliwatts(), std::int64_t{400});
  CCA_CHECK_EQ(degraded_loop->totals.withheld.milliwatts(), std::int64_t{600});
  CCA_CHECK_EQ(degraded_loop->totals.allocatable.milliwatts(), std::int64_t{0});
  CCA_CHECK_EQ(degraded_loop->withheld.of(WithheldReason::ReserveOnlyClass).milliwatts(),
               std::int64_t{600});
  CCA_CHECK_EQ(degraded_loop->totals.sum(), degraded_loop->declared_installed);
}

CCA_TEST(mutual_noncontributing_class_mass_is_withheld) {
  const std::vector<Fill> fills{
      fill_of("m.monitor", 1000, 0, "", ContributionClass::Additive)};
  std::vector<Fill> monitor = fills;
  monitor[0].equipment_class = "class.monitor";
  CCA_ASSIGN(ledger, build_members({}, monitor));

  const ScopeAccounting* loop = sole_loop(ledger);
  CCA_CHECK(loop != nullptr);
  if (loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(loop->declared_installed.milliwatts(), std::int64_t{1000});
  CCA_CHECK_EQ(loop->totals.allocatable.milliwatts(), std::int64_t{0});
  CCA_CHECK_EQ(loop->totals.withheld.milliwatts(), std::int64_t{1000});
  CCA_CHECK_EQ(loop->withheld.of(WithheldReason::NonContributingClass).milliwatts(),
               std::int64_t{1000});
  CCA_CHECK_EQ(count_code(loop->findings, FindingCode::NonContributingClass), std::size_t{1});
  const ContributionAllocation* allocation = allocation_of(ledger, "m.monitor");
  CCA_CHECK(allocation != nullptr);
  if (allocation != nullptr && allocation->withheld_reason.has_value()) {
    CCA_CHECK_EQ(allocation->withheld_reason.value(), WithheldReason::NonContributingClass);
  }

  // A derated member of a non-contributing class withholds what it retains.
  std::vector<Fill> degraded = monitor;
  degraded[0].derate_ppm = 600000;
  degraded[0].service = ServiceState::Degraded;
  CCA_ASSIGN(degraded_ledger, build_members({}, degraded));
  const ScopeAccounting* degraded_loop = sole_loop(degraded_ledger);
  CCA_CHECK(degraded_loop != nullptr);
  if (degraded_loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(degraded_loop->totals.degraded_loss.milliwatts(), std::int64_t{400});
  CCA_CHECK_EQ(degraded_loop->totals.withheld.milliwatts(), std::int64_t{600});
  CCA_CHECK_EQ(degraded_loop->withheld.of(WithheldReason::NonContributingClass).milliwatts(),
               std::int64_t{600});
  CCA_CHECK_EQ(degraded_loop->totals.allocatable.milliwatts(), std::int64_t{0});

  // The class reason is not overwritten by a group that would otherwise leave
  // the member allocatable.
  const std::vector<GroupSpec> groups{
      {"group.sub", ContributionClass::Substitutive, 1}};
  std::vector<Fill> grouped = monitor;
  grouped[0].group = "group.sub";
  grouped[0].classification = ContributionClass::Substitutive;
  CCA_ASSIGN(grouped_ledger, build_members(groups, grouped));
  const ScopeAccounting* grouped_loop = sole_loop(grouped_ledger);
  CCA_CHECK(grouped_loop != nullptr);
  if (grouped_loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(grouped_loop->totals.allocatable.milliwatts(), std::int64_t{0});
  CCA_CHECK_EQ(grouped_loop->withheld.of(WithheldReason::NonContributingClass).milliwatts(),
               std::int64_t{1000});
  CCA_CHECK_EQ(grouped_loop->withheld.of(WithheldReason::SubstitutiveSpare).milliwatts(),
               std::int64_t{0});
  CCA_CHECK_EQ(count_code(grouped_loop->findings, FindingCode::SubstitutiveSpareWithheld),
               std::size_t{0});
}

CCA_TEST(mutual_substitutive_pool_is_in_service_only) {
  const std::vector<GroupSpec> groups{
      {"group.sub", ContributionClass::Substitutive, 1}};
  std::vector<Fill> fills{
      fill_of("m.first", 100, 0, "group.sub", ContributionClass::Substitutive),
      fill_of("m.spare", 200, 1, "group.sub", ContributionClass::Substitutive),
      fill_of("m.stopped", 300, 2, "group.sub", ContributionClass::Substitutive),
      fill_of("m.unmeasured", 400, 3, "group.sub", ContributionClass::Substitutive)};
  fills[2].service = ServiceState::OutOfService;
  fills[3].service = ServiceState::Unknown;
  CCA_ASSIGN(ledger, build_members(groups, fills));

  // Only in-service members take part in the choice, and only the member beyond
  // required_concurrent is withheld as spare.
  CCA_CHECK_EQ(allocated_of(ledger, "m.first"), std::int64_t{100});
  CCA_CHECK_EQ(withheld_of(ledger, "m.spare"), std::int64_t{200});
  CCA_CHECK_EQ(withheld_of(ledger, "m.stopped"), std::int64_t{0});
  CCA_CHECK_EQ(withheld_of(ledger, "m.unmeasured"), std::int64_t{0});

  const ScopeAccounting* loop = sole_loop(ledger);
  CCA_CHECK(loop != nullptr);
  if (loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(loop->declared_installed.milliwatts(), std::int64_t{1000});
  CCA_CHECK_EQ(loop->totals.allocatable.milliwatts(), std::int64_t{100});
  CCA_CHECK_EQ(loop->totals.withheld.milliwatts(), std::int64_t{200});
  CCA_CHECK_EQ(loop->totals.unavailable.milliwatts(), std::int64_t{300});
  CCA_CHECK_EQ(loop->totals.indeterminate.milliwatts(), std::int64_t{400});
  CCA_CHECK_EQ(loop->totals.sum(), loop->declared_installed);
  CCA_CHECK_EQ(count_code(loop->findings, FindingCode::SubstitutiveSpareWithheld),
               std::size_t{1});
}

CCA_TEST(mutual_withheld_breakdown_sums_to_the_withheld_total) {
  const std::vector<GroupSpec> groups{
      {"group.sub", ContributionClass::Substitutive, 1},
      {"group.mutex", ContributionClass::MutuallyExclusive, 0}};
  std::vector<Fill> fills{
      fill_of("u.first", 100, 0, "group.sub", ContributionClass::Substitutive),
      fill_of("u.spare", 200, 1, "group.sub", ContributionClass::Substitutive),
      fill_of("u.counted", 50, 0, "group.mutex", ContributionClass::MutuallyExclusive),
      fill_of("u.excluded", 70, 1, "group.mutex", ContributionClass::MutuallyExclusive),
      fill_of("u.reserve", 300, 0, "", ContributionClass::ReserveOnly),
      fill_of("u.monitor", 400, 0, "", ContributionClass::Additive)};
  fills[5].equipment_class = "class.monitor";
  CCA_ASSIGN(ledger, build_members(groups, fills));

  const ScopeAccounting* loop = sole_loop(ledger);
  CCA_CHECK(loop != nullptr);
  if (loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(loop->declared_installed.milliwatts(), std::int64_t{1120});
  CCA_CHECK_EQ(loop->totals.allocatable.milliwatts(), std::int64_t{150});
  CCA_CHECK_EQ(loop->withheld.of(WithheldReason::SubstitutiveSpare).milliwatts(),
               std::int64_t{200});
  CCA_CHECK_EQ(loop->withheld.of(WithheldReason::MutualExclusion).milliwatts(),
               std::int64_t{70});
  CCA_CHECK_EQ(loop->withheld.of(WithheldReason::ReserveOnlyClass).milliwatts(),
               std::int64_t{300});
  CCA_CHECK_EQ(loop->withheld.of(WithheldReason::NonContributingClass).milliwatts(),
               std::int64_t{400});
  CCA_CHECK_EQ(loop->withheld.of(WithheldReason::ReserveObligation).milliwatts(),
               std::int64_t{0});
  CCA_CHECK_EQ(loop->totals.withheld.milliwatts(), std::int64_t{970});
  CCA_CHECK_EQ(loop->withheld.of(WithheldReason::SubstitutiveSpare).milliwatts() +
                   loop->withheld.of(WithheldReason::MutualExclusion).milliwatts() +
                   loop->withheld.of(WithheldReason::ReserveOnlyClass).milliwatts() +
                   loop->withheld.of(WithheldReason::NonContributingClass).milliwatts(),
               loop->withheld.total.milliwatts());
  CCA_CHECK(loop->withheld.verify_total().ok());
  CCA_CHECK_EQ(loop->totals.sum(), loop->declared_installed);
  CCA_CHECK(loop->totals.verify(loop->declared_installed).ok());
  CCA_CHECK(ledger.verify_closure().exact);
  for (const ContributionAccounting& record : ledger.contributions()) {
    for (const ContributionAllocation& allocation : record.allocations) {
      CCA_CHECK_EQ(allocation.installed.milliwatts(),
                   allocation.allocatable.milliwatts() + allocation.withheld.milliwatts() +
                       allocation.degraded_loss.milliwatts() +
                       allocation.unavailable.milliwatts() +
                       allocation.indeterminate.milliwatts());
    }
  }
}

}  // namespace
