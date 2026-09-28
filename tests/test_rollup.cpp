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

// Rollups: a scope rollup sums a scope and every scope nested below it, a class
// rollup can never include another class's mass, a medium rollup can never mix
// the two media, and a contribution is counted exactly once, in its home scope.
// Every rollup here is derived from a SYNTHETIC facility.

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
                               std::string_view equipment_class, Medium medium,
                               std::int64_t installed_mw) {
  Contribution contribution;
  contribution.id = id_of<ContributionId>(id);
  contribution.home_scope = id_of<ScopeId>(home);
  contribution.equipment = id_of<EquipmentId>(std::string("equipment.") + std::string(id));
  contribution.equipment_class = id_of<EquipmentClassId>(equipment_class);
  contribution.medium = medium;
  contribution.classification = ContributionClass::Additive;
  contribution.installed =
      Measure<ThermalPower>::known(ThermalPower::of_milliwatts(installed_mw));
  contribution.service = ServiceState::InService;
  contribution.observed_at = cca_test::fixture_now();
  contribution.binding = EvidenceBinding::initial();
  return contribution;
}

/// site.alpha > zone.alpha > {loop.air (Air), loop.liquid (Liquid)} with one
/// contributing class per medium.
AccountingInput two_medium_input() {
  AccountingInput input;
  input.generations = GenerationBundle::initial();
  input.policy = cca_test::relaxed_policy();
  input.scopes.push_back(scope_node("site.alpha", ScopeKind::Site, "", Medium::Air));
  input.scopes.push_back(scope_node("zone.alpha", ScopeKind::Zone, "site.alpha", Medium::Air));
  input.scopes.push_back(scope_node("loop.air", ScopeKind::Loop, "zone.alpha", Medium::Air));
  input.scopes.push_back(scope_node("loop.liquid", ScopeKind::Loop, "zone.alpha", Medium::Liquid));
  // An empty loop, used by the "counted once" test as an apportionment target
  // that has no contributions of its own.
  input.scopes.push_back(scope_node("loop.spare", ScopeKind::Loop, "zone.alpha", Medium::Air));
  input.equipment_classes.push_back(class_node("class.crah", Medium::Air));
  input.equipment_classes.push_back(class_node("class.cdu", Medium::Liquid));
  input.contributions.push_back(
      contribution_node("unit.air1", "loop.air", "class.crah", Medium::Air, 1000));
  input.contributions.push_back(
      contribution_node("unit.air2", "loop.air", "class.crah", Medium::Air, 1000));
  input.contributions.push_back(
      contribution_node("unit.liq1", "loop.liquid", "class.cdu", Medium::Liquid, 700));
  input.contributions.push_back(
      contribution_node("unit.liq2", "loop.liquid", "class.cdu", Medium::Liquid, 700));
  input.contributions.push_back(
      contribution_node("unit.liq3", "loop.liquid", "class.cdu", Medium::Liquid, 700));
  return input;
}

Result<AccountingLedger> build(const AccountingInput& input) {
  return AccountingLedger::build(input, cca_test::fixture_now());
}

template <typename T>
void shuffle_vector(std::vector<T>& values, cca_test::SeededRandom& random) {
  for (std::size_t index = values.size(); index > 1; --index) {
    const std::size_t other = static_cast<std::size_t>(
        random.next_range(0, static_cast<std::int64_t>(index) - 1));
    std::swap(values[index - 1], values[other]);
  }
}

void check_totals_equal(const AccountingTotals& lhs, const AccountingTotals& rhs) {
  CCA_CHECK_EQ(lhs.scope_count, rhs.scope_count);
  CCA_CHECK_EQ(lhs.contribution_count, rhs.contribution_count);
  CCA_CHECK_EQ(lhs.indeterminate_contribution_count, rhs.indeterminate_contribution_count);
  CCA_CHECK_EQ(lhs.declared_installed, rhs.declared_installed);
  CCA_CHECK_EQ(lhs.installed_fully_known, rhs.installed_fully_known);
  CCA_CHECK_EQ(lhs.totals.allocatable, rhs.totals.allocatable);
  CCA_CHECK_EQ(lhs.totals.withheld, rhs.totals.withheld);
  CCA_CHECK_EQ(lhs.totals.degraded_loss, rhs.totals.degraded_loss);
  CCA_CHECK_EQ(lhs.totals.unavailable, rhs.totals.unavailable);
  CCA_CHECK_EQ(lhs.totals.indeterminate, rhs.totals.indeterminate);
  CCA_CHECK_EQ(lhs.withheld.total, rhs.withheld.total);
  for (std::size_t index = 0; index < kWithheldReasonCount; ++index) {
    const auto reason = static_cast<WithheldReason>(index);
    CCA_CHECK_EQ(lhs.withheld.of(reason), rhs.withheld.of(reason));
  }
  CCA_CHECK_EQ(lhs.residual, rhs.residual);
  CCA_CHECK_EQ(lhs.unknown_installed_count, rhs.unknown_installed_count);
}

AccountingTotals sum_of_scopes(const AccountingLedger& ledger) {
  AccountingTotals totals;
  for (const ScopeAccounting& scope : ledger.scopes()) {
    totals.add(scope);
  }
  return totals;
}

// ---------------------------------------------------------------------------

CCA_TEST(rollup_scope_sums_every_nested_scope) {
  cca_test::FacilitySpec spec;
  spec.zones = 2;
  spec.loops_per_zone = 2;
  spec.units_per_loop = 3;
  spec.unit_installed_mw = 250000;
  AccountingInput input = cca_test::make_facility(spec);
  CCA_ASSIGN(ledger, build(input));

  CCA_ASSIGN(site_view, ledger.scope_rollup(id_of<ScopeId>(cca_test::facility_site_id())));
  CCA_CHECK_EQ(site_view.dimension, RollupDimension::Scope);
  CCA_CHECK_EQ(site_view.root, std::string(cca_test::facility_site_id()));
  CCA_CHECK_EQ(site_view.scopes.size(), std::size_t{7});
  CCA_CHECK_EQ(site_view.totals.scope_count, std::size_t{7});
  CCA_CHECK_EQ(site_view.totals.contribution_count, std::size_t{12});
  CCA_CHECK_EQ(site_view.totals.declared_installed.milliwatts(), std::int64_t{3000000});
  CCA_CHECK_EQ(site_view.totals.totals.allocatable.milliwatts(), std::int64_t{3000000});
  CCA_CHECK_EQ(site_view.totals.totals.sum(), site_view.totals.declared_installed);
  CCA_CHECK(site_view.totals.closed_exactly);

  // The rollup total is exactly the sum of the cells it contains.
  check_totals_equal(site_view.totals, sum_of_scopes(ledger));

  // A zone rollup sums the zone and its loops, and nothing else.
  CCA_ASSIGN(zone_view, ledger.scope_rollup(id_of<ScopeId>(cca_test::facility_zone_id(0))));
  CCA_CHECK_EQ(zone_view.scopes.size(), std::size_t{3});
  CCA_CHECK_EQ(zone_view.totals.declared_installed.milliwatts(), std::int64_t{1500000});
  CCA_CHECK_EQ(zone_view.totals.contribution_count, std::size_t{6});

  CCA_ASSIGN(loop_view,
             ledger.scope_rollup(id_of<ScopeId>(cca_test::facility_loop_id(1, 1))));
  CCA_CHECK_EQ(loop_view.scopes.size(), std::size_t{1});
  CCA_CHECK_EQ(loop_view.totals.declared_installed.milliwatts(), std::int64_t{750000});
  CCA_CHECK_EQ(loop_view.totals.contribution_count, std::size_t{3});

  // Summing the child rollups reproduces the parent rollup exactly: no scope is
  // counted twice and none is dropped.
  std::int64_t zone_sum = 0;
  for (std::size_t zone = 0; zone < 2; ++zone) {
    CCA_ASSIGN(child, ledger.scope_rollup(id_of<ScopeId>(cca_test::facility_zone_id(zone))));
    zone_sum += child.totals.declared_installed.milliwatts();
    for (const ScopeAccounting& scope : child.scopes) {
      CCA_CHECK(scope.totals.verify(scope.declared_installed).ok());
    }
  }
  CCA_CHECK_EQ(zone_sum, site_view.totals.declared_installed.milliwatts());
}

CCA_TEST(rollup_class_never_includes_another_class) {
  AccountingInput input = two_medium_input();
  CCA_ASSIGN(ledger, build(input));

  CCA_ASSIGN(air_class, ledger.class_rollup(id_of<EquipmentClassId>("class.crah")));
  CCA_ASSIGN(liquid_class, ledger.class_rollup(id_of<EquipmentClassId>("class.cdu")));

  CCA_CHECK_EQ(air_class.dimension, RollupDimension::EquipmentClass);
  CCA_CHECK_EQ(air_class.root, std::string("class.crah"));
  CCA_CHECK_EQ(air_class.totals.declared_installed.milliwatts(), std::int64_t{2000});
  CCA_CHECK_EQ(air_class.totals.contribution_count, std::size_t{2});
  CCA_CHECK_EQ(air_class.scopes.size(), std::size_t{1});
  CCA_CHECK_EQ(air_class.scopes.front().scope, id_of<ScopeId>("loop.air"));
  CCA_CHECK_EQ(air_class.scopes.front().declared_installed.milliwatts(), std::int64_t{2000});

  CCA_CHECK_EQ(liquid_class.totals.declared_installed.milliwatts(), std::int64_t{2100});
  CCA_CHECK_EQ(liquid_class.totals.contribution_count, std::size_t{3});
  CCA_CHECK_EQ(liquid_class.scopes.size(), std::size_t{1});
  CCA_CHECK_EQ(liquid_class.scopes.front().scope, id_of<ScopeId>("loop.liquid"));
  CCA_CHECK_EQ(liquid_class.scopes.front().declared_installed.milliwatts(), std::int64_t{2100});

  // Neither class rollup contains a milliwatt of the other class, and together
  // they account for the whole generation.
  CCA_CHECK_EQ(air_class.totals.declared_installed.milliwatts() +
                   liquid_class.totals.declared_installed.milliwatts(),
               ledger.overall_totals().declared_installed.milliwatts());
  for (const ScopeAccounting& scope : air_class.scopes) {
    CCA_CHECK(scope.totals.verify(scope.declared_installed).ok());
    CCA_CHECK(scope.withheld.verify_total().ok());
  }

  CCA_CHECK_CODE(ledger.class_rollup(id_of<EquipmentClassId>("class.absent")),
                 ErrorCode::UnknownReference);
}

CCA_TEST(rollup_medium_never_mixes_media) {
  AccountingInput input = two_medium_input();
  CCA_ASSIGN(ledger, build(input));

  CCA_ASSIGN(air_view, ledger.medium_rollup(Medium::Air));
  CCA_ASSIGN(liquid_view, ledger.medium_rollup(Medium::Liquid));

  CCA_CHECK_EQ(air_view.dimension, RollupDimension::Medium);
  CCA_CHECK_EQ(air_view.root, std::string(medium_name(Medium::Air)));
  CCA_CHECK_EQ(air_view.root, std::string("Air"));
  CCA_CHECK_EQ(air_view.totals.declared_installed.milliwatts(), std::int64_t{2000});
  CCA_CHECK_EQ(air_view.totals.contribution_count, std::size_t{2});
  CCA_CHECK_EQ(air_view.scopes.size(), std::size_t{1});
  CCA_CHECK_EQ(air_view.scopes.front().scope, id_of<ScopeId>("loop.air"));
  CCA_CHECK_EQ(air_view.scopes.front().medium, Medium::Air);

  CCA_CHECK_EQ(liquid_view.root, std::string("Liquid"));
  CCA_CHECK_EQ(liquid_view.totals.declared_installed.milliwatts(), std::int64_t{2100});
  CCA_CHECK_EQ(liquid_view.totals.contribution_count, std::size_t{3});
  CCA_CHECK_EQ(liquid_view.scopes.front().medium, Medium::Liquid);

  CCA_CHECK_EQ(air_view.totals.declared_installed.milliwatts() +
                   liquid_view.totals.declared_installed.milliwatts(),
               ledger.overall_totals().declared_installed.milliwatts());

  // A medium with no accounted cell at all has no rollup.
  AccountingInput air_only = two_medium_input();
  air_only.contributions.erase(air_only.contributions.begin() + 2, air_only.contributions.end());
  CCA_ASSIGN(air_only_ledger, build(air_only));
  CCA_CHECK(air_only_ledger.medium_rollup(Medium::Air).ok());
  CCA_CHECK_CODE(air_only_ledger.medium_rollup(Medium::Liquid), ErrorCode::UnknownReference);
}

CCA_TEST(rollup_counts_a_contribution_once_in_its_home_scope) {
  AccountingInput input = two_medium_input();
  // An alias names another scope that merely references the contribution.
  input.contributions[0].aliases.push_back(id_of<ScopeId>("loop.spare"));
  // A shared contribution apportions part of its mass to another scope.
  input.contributions[1].sharing.kind = Sharing::Kind::Apportioned;
  ApportionmentShare share;
  share.target = id_of<ScopeId>("loop.spare");
  share.share = Ratio::of_ppm(400000).value();
  input.contributions[1].sharing.shares.push_back(share);

  CCA_ASSIGN(ledger, build(input));
  const ScopeAccounting* home = ledger.find_scope(id_of<ScopeId>("loop.air"));
  const ScopeAccounting* target = ledger.find_scope(id_of<ScopeId>("loop.spare"));
  CCA_CHECK(home != nullptr && target != nullptr);
  if (home == nullptr || target == nullptr) {
    return;
  }
  // Counted once, in the home scope: the target scope receives mass but no
  // contribution count, and the alias changes nothing at all.
  CCA_CHECK_EQ(home->contribution_count, std::size_t{2});
  CCA_CHECK_EQ(target->contribution_count, std::size_t{0});
  CCA_CHECK_EQ(home->declared_installed.milliwatts(), std::int64_t{1600});
  CCA_CHECK_EQ(target->declared_installed.milliwatts(), std::int64_t{400});

  CCA_ASSIGN(site_view, ledger.scope_rollup(id_of<ScopeId>("site.alpha")));
  CCA_CHECK_EQ(site_view.totals.contribution_count, std::size_t{5});
  CCA_CHECK_EQ(site_view.totals.declared_installed.milliwatts(), std::int64_t{4100});
  CCA_CHECK_EQ(ledger.overall_totals().contribution_count, std::size_t{5});
  CCA_CHECK_EQ(ledger.overall_totals().declared_installed.milliwatts(), std::int64_t{4100});
  CCA_CHECK_EQ(site_view.totals.contribution_count,
               ledger.overall_totals().contribution_count);
}

CCA_TEST(rollup_unknown_scope_is_unknown_scope) {
  AccountingInput input = two_medium_input();
  CCA_ASSIGN(ledger, build(input));
  CCA_CHECK_CODE(ledger.scope_rollup(id_of<ScopeId>("loop.absent")), ErrorCode::UnknownScope);
  CCA_CHECK_CODE(ledger.scope_rollup(id_of<ScopeId>("site.absent")), ErrorCode::UnknownScope);
}

CCA_TEST(rollup_order_independent) {
  const std::uint64_t seed = 0xC0FFEE01ULL;
  cca_test::note("rollup_order_independent seed=" + std::to_string(seed));

  cca_test::FacilitySpec spec;
  spec.zones = 2;
  spec.loops_per_zone = 2;
  spec.units_per_loop = 2;
  spec.unit_installed_mw = 123456;
  AccountingInput original = cca_test::make_facility(spec);
  CCA_ASSIGN(reference, build(original));

  cca_test::SeededRandom random(seed);
  for (int round = 0; round < 8; ++round) {
    AccountingInput shuffled = original;
    shuffle_vector(shuffled.scopes, random);
    shuffle_vector(shuffled.contributions, random);
    shuffle_vector(shuffled.groups, random);
    shuffle_vector(shuffled.evidence, random);
    shuffle_vector(shuffled.independence_domains, random);
    shuffle_vector(shuffled.manifest_declarations, random);
    shuffle_vector(shuffled.equipment_classes, random);
    CCA_ASSIGN(candidate, build(shuffled));
    CCA_CHECK_EQ(candidate.digest(), reference.digest());
    CCA_CHECK_EQ(candidate.to_report(), reference.to_report());
    CCA_ASSIGN(candidate_site,
               candidate.scope_rollup(id_of<ScopeId>(cca_test::facility_site_id())));
    CCA_ASSIGN(reference_site,
               reference.scope_rollup(id_of<ScopeId>(cca_test::facility_site_id())));
    CCA_CHECK_EQ(candidate_site.digest, reference_site.digest);
    check_totals_equal(candidate_site.totals, reference_site.totals);
    CCA_ASSIGN(candidate_class,
               candidate.class_rollup(id_of<EquipmentClassId>(cca_test::facility_air_class_id())));
    CCA_ASSIGN(reference_class,
               reference.class_rollup(id_of<EquipmentClassId>(cca_test::facility_air_class_id())));
    CCA_CHECK_EQ(candidate_class.digest, reference_class.digest);
    CCA_ASSIGN(candidate_medium, candidate.medium_rollup(Medium::Air));
    CCA_ASSIGN(reference_medium, reference.medium_rollup(Medium::Air));
    CCA_CHECK_EQ(candidate_medium.digest, reference_medium.digest);
    CCA_CHECK_EQ(candidate.scopes().size(), reference.scopes().size());
    for (std::size_t index = 0; index < candidate.scopes().size(); ++index) {
      CCA_CHECK_EQ(candidate.scopes()[index].digest, reference.scopes()[index].digest);
      CCA_CHECK_EQ(candidate.scopes()[index].declared_installed,
                   reference.scopes()[index].declared_installed);
    }
  }
}

CCA_TEST(rollup_overall_totals_is_the_sum_of_every_scope) {
  cca_test::FacilitySpec spec;
  spec.zones = 1;
  spec.loops_per_zone = 2;
  spec.units_per_loop = 2;
  spec.unit_installed_mw = 300000;
  AccountingInput input = cca_test::make_facility(spec);
  // A second root site with its own contribution, so site_totals and
  // overall_totals are genuinely different questions.
  input.scopes.push_back(scope_node("site.beta", ScopeKind::Site, "", Medium::Air));
  input.contributions.push_back(
      contribution_node("unit.beta1", "site.beta", "class.crah", Medium::Air, 500));

  CCA_ASSIGN(ledger, build(input));
  const AccountingTotals overall = ledger.overall_totals();
  const AccountingTotals manual = sum_of_scopes(ledger);
  check_totals_equal(overall, manual);
  // Four units of 300000 mW plus the 500 mW contributed directly by site.beta.
  CCA_CHECK_EQ(overall.declared_installed.milliwatts(), std::int64_t{1200500});
  CCA_CHECK_EQ(overall.scope_count, ledger.scopes().size());
  CCA_CHECK(overall.closed_exactly);

  // overall_totals() is exactly the sum of the root rollups: one site's mass
  // plus the other's, with nothing counted twice.
  CCA_ASSIGN(alpha_view, ledger.scope_rollup(id_of<ScopeId>(cca_test::facility_site_id())));
  CCA_ASSIGN(beta_view, ledger.scope_rollup(id_of<ScopeId>("site.beta")));
  CCA_CHECK_EQ(alpha_view.totals.declared_installed.milliwatts() +
                   beta_view.totals.declared_installed.milliwatts(),
               overall.declared_installed.milliwatts());
  CCA_CHECK_EQ(alpha_view.totals.contribution_count + beta_view.totals.contribution_count,
               overall.contribution_count);
  CCA_CHECK_EQ(alpha_view.totals.totals.allocatable.milliwatts() +
                   beta_view.totals.totals.allocatable.milliwatts(),
               overall.totals.allocatable.milliwatts());

  // site_totals() counts the root scopes only: the nested loops are reachable
  // through each site's rollup, not through the site cell.
  const AccountingTotals sites = ledger.site_totals();
  CCA_CHECK_EQ(sites.scope_count, std::size_t{2});
  CCA_CHECK_EQ(sites.contribution_count, std::size_t{1});
  CCA_CHECK_EQ(sites.declared_installed.milliwatts(), std::int64_t{500});
  const ScopeAccounting* site_alpha =
      ledger.find_scope(id_of<ScopeId>(cca_test::facility_site_id()));
  CCA_CHECK(site_alpha != nullptr);
  if (site_alpha == nullptr) {
    return;
  }
  CCA_CHECK_EQ(site_alpha->declared_installed.milliwatts(), std::int64_t{0});
  CCA_CHECK_EQ(site_alpha->contribution_count, std::size_t{0});
}

}  // namespace
