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

// Apportionment: an apportioned contribution splits its installed quantity
// across share targets with floor rounding, the unapportioned remainder stays
// in the home scope, and the parts always re-sum to the whole. Every fixture
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

struct ShareSpec {
  std::string target;
  std::uint32_t ppm = 0;
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

AccountingInput base_input() {
  AccountingInput input;
  input.generations = GenerationBundle::initial();
  input.policy = cca_test::relaxed_policy();
  input.scopes.push_back(scope_node("site.alpha", ScopeKind::Site, "", Medium::Air));
  input.scopes.push_back(scope_node("zone.alpha", ScopeKind::Zone, "site.alpha", Medium::Air));
  input.scopes.push_back(scope_node("loop.home", ScopeKind::Loop, "zone.alpha", Medium::Air));
  input.scopes.push_back(scope_node("loop.a", ScopeKind::Loop, "zone.alpha", Medium::Air));
  input.scopes.push_back(scope_node("loop.b", ScopeKind::Loop, "zone.alpha", Medium::Air));
  input.scopes.push_back(scope_node("loop.c", ScopeKind::Loop, "zone.alpha", Medium::Air));
  EquipmentClass klass;
  klass.id = id_of<EquipmentClassId>("class.crah");
  klass.kind = EquipmentClassKind::Other;
  klass.medium = Medium::Air;
  input.equipment_classes.push_back(klass);
  return input;
}

/// One contribution in loop.home whose installed quantity is apportioned to the
/// declared targets. An unknown installed quantity is expressed by a zero
/// quantity plus unknown_installed.
Result<AccountingLedger> apportioned_ledger(std::int64_t installed_mw,
                                            const std::vector<ShareSpec>& shares,
                                            bool installed_known = true) {
  AccountingInput input = base_input();
  Contribution contribution;
  contribution.id = id_of<ContributionId>("unit.shared");
  contribution.home_scope = id_of<ScopeId>("loop.home");
  contribution.equipment = id_of<EquipmentId>("equipment.shared");
  contribution.equipment_class = id_of<EquipmentClassId>("class.crah");
  contribution.medium = Medium::Air;
  contribution.classification = ContributionClass::Additive;
  if (installed_known) {
    contribution.installed =
        Measure<ThermalPower>::known(ThermalPower::of_milliwatts(installed_mw));
  } else {
    contribution.installed = Measure<ThermalPower>::unknown(
        MeasureReason::NotMeasured, BoundedText::from_validated("synthetic unmeasured"));
  }
  contribution.service = ServiceState::InService;
  contribution.sharing.kind = Sharing::Kind::Apportioned;
  for (const ShareSpec& spec : shares) {
    ApportionmentShare share;
    share.target = id_of<ScopeId>(spec.target);
    share.share = Ratio::of_ppm(spec.ppm).value();
    contribution.sharing.shares.push_back(share);
  }
  contribution.observed_at = cca_test::fixture_now();
  contribution.binding = EvidenceBinding::initial();
  input.contributions.push_back(contribution);
  return AccountingLedger::build(input, cca_test::fixture_now());
}

std::int64_t declared_of(const AccountingLedger& ledger, std::string_view scope) {
  const ScopeAccounting* record = ledger.find_scope(id_of<ScopeId>(scope));
  return record == nullptr ? -1 : record->declared_installed.milliwatts();
}

const ScopeAccounting* scope_of(const AccountingLedger& ledger, std::string_view scope) {
  return ledger.find_scope(id_of<ScopeId>(scope));
}

const Finding* find_code(const std::vector<Finding>& findings, FindingCode code) {
  for (const Finding& finding : findings) {
    if (finding.code == code) {
      return &finding;
    }
  }
  return nullptr;
}

std::int64_t floor_share(std::int64_t installed_mw, std::uint32_t ppm) {
  return static_cast<std::int64_t>(
      (static_cast<unsigned long long>(installed_mw) * ppm) / 1000000ULL);
}

// ---------------------------------------------------------------------------

CCA_TEST(apportionment_splits_exactly_and_keeps_the_remainder_home) {
  CCA_ASSIGN(ledger, apportioned_ledger(1000003, {{"loop.a", 300000}, {"loop.b", 200000}}));

  // floor(1000003 * 0.3) = 300000 and floor(1000003 * 0.2) = 200000; the
  // remaining 500003 milliwatts never leave the home scope.
  CCA_CHECK_EQ(declared_of(ledger, "loop.a"), std::int64_t{300000});
  CCA_CHECK_EQ(declared_of(ledger, "loop.b"), std::int64_t{200000});
  CCA_CHECK_EQ(declared_of(ledger, "loop.home"), std::int64_t{500003});
  CCA_CHECK_EQ(declared_of(ledger, "loop.a") + declared_of(ledger, "loop.b") +
                   declared_of(ledger, "loop.home"),
               std::int64_t{1000003});

  const ScopeAccounting* home = scope_of(ledger, "loop.home");
  const ScopeAccounting* target = scope_of(ledger, "loop.a");
  CCA_CHECK(home != nullptr && target != nullptr);
  if (home == nullptr || target == nullptr) {
    return;
  }
  // The contribution is counted once, in its home scope, and the targets only
  // receive mass.
  CCA_CHECK_EQ(home->contribution_count, std::size_t{1});
  CCA_CHECK_EQ(target->contribution_count, std::size_t{0});
  CCA_CHECK_EQ(home->totals.allocatable.milliwatts(), std::int64_t{500003});
  CCA_CHECK_EQ(target->totals.allocatable.milliwatts(), std::int64_t{300000});

  const Finding* remainder = find_code(home->findings, FindingCode::UnapportionedShare);
  CCA_CHECK(remainder != nullptr);
  if (remainder != nullptr) {
    CCA_CHECK_EQ(remainder->severity, FindingSeverity::Warning);
    CCA_CHECK(remainder->amount.has_value());
    if (remainder->amount.has_value()) {
      CCA_CHECK_EQ(remainder->amount->milliwatts(), std::int64_t{500003});
    }
    CCA_CHECK(remainder->contribution == id_of<ContributionId>("unit.shared"));
  }

  // The allocation parts re-sum to the installed quantity.
  CCA_CHECK_EQ(ledger.contributions().size(), std::size_t{1});
  std::int64_t parts = 0;
  for (const ContributionAllocation& allocation : ledger.contributions().front().allocations) {
    parts += allocation.installed.milliwatts();
  }
  CCA_CHECK_EQ(parts, std::int64_t{1000003});
  CCA_CHECK_EQ(ledger.contributions().front().allocations.size(), std::size_t{3});
}

CCA_TEST(apportionment_identity_holds_in_every_target_scope) {
  CCA_ASSIGN(ledger, apportioned_ledger(999999, {{"loop.a", 333333},
                                                 {"loop.b", 250000},
                                                 {"loop.c", 1}}));
  const ClosureCheck closure = ledger.verify_closure();
  CCA_CHECK(closure.exact);
  CCA_CHECK_EQ(closure.violations.size(), std::size_t{0});
  CCA_CHECK_EQ(closure.scopes_checked, ledger.scopes().size());
  for (const ScopeAccounting& scope : ledger.scopes()) {
    CCA_CHECK(scope.totals.verify(scope.declared_installed).ok());
    CCA_CHECK(scope.withheld.verify_total().ok());
    CCA_CHECK_EQ(scope.totals.sum(), scope.declared_installed);
  }
  CCA_CHECK_EQ(declared_of(ledger, "loop.a"), floor_share(999999, 333333));
  CCA_CHECK_EQ(declared_of(ledger, "loop.b"), floor_share(999999, 250000));
  CCA_CHECK_EQ(declared_of(ledger, "loop.c"), floor_share(999999, 1));
  CCA_CHECK_EQ(ledger.overall_totals().declared_installed.milliwatts(), std::int64_t{999999});
}

CCA_TEST(apportionment_rounds_down_at_every_boundary) {
  struct Case {
    std::int64_t installed;
    std::uint32_t ppm;
  };
  const Case cases[] = {{1, 1},          {1, 1000000},   {1000000, 999999},
                        {999999, 1},     {1000003, 333333}, {7, 500000},
                        {1000000, 0},    {1000000, 1000000}};
  for (const Case& item : cases) {
    const std::int64_t expected = floor_share(item.installed, item.ppm);
    const std::int64_t remainder = item.installed - expected;
    CCA_ASSIGN(ledger, apportioned_ledger(item.installed, {{"loop.a", item.ppm}}));
    CCA_CHECK_EQ(declared_of(ledger, "loop.a"), expected);
    CCA_CHECK_EQ(declared_of(ledger, "loop.home"), remainder);
    CCA_CHECK_EQ(declared_of(ledger, "loop.a") + declared_of(ledger, "loop.home"),
                 item.installed);
    const ScopeAccounting* home = scope_of(ledger, "loop.home");
    CCA_CHECK(home != nullptr);
    if (home == nullptr) {
      return;
    }
    const Finding* finding = find_code(home->findings, FindingCode::UnapportionedShare);
    if (remainder == 0) {
      // A fully apportioned contribution leaves nothing behind and says so.
      CCA_CHECK(finding == nullptr);
    } else {
      CCA_CHECK(finding != nullptr);
      if (finding != nullptr && finding->amount.has_value()) {
        CCA_CHECK_EQ(finding->amount->milliwatts(), remainder);
      }
    }
  }
}

CCA_TEST(apportionment_rounding_is_seeded_randomised_and_exact) {
  const std::uint64_t seed = 0x5EED0007ULL;
  cca_test::note("apportionment_rounding_is_seeded_randomised_and_exact seed=" +
                 std::to_string(seed));
  cca_test::SeededRandom random(seed);
  for (int round = 0; round < 24; ++round) {
    const std::int64_t installed =
        random.next_range(0, 1000000000);
    const std::uint32_t ppm = static_cast<std::uint32_t>(random.next_range(0, 1000000));
    const std::int64_t expected = floor_share(installed, ppm);
    CCA_ASSIGN(ledger, apportioned_ledger(installed, {{"loop.a", ppm}}));
    CCA_CHECK_EQ(declared_of(ledger, "loop.a"), expected);
    CCA_CHECK_EQ(declared_of(ledger, "loop.home"), installed - expected);
  }
}

CCA_TEST(apportionment_shares_may_not_exceed_the_whole) {
  CCA_CHECK_CODE(apportioned_ledger(1000, {{"loop.a", 600000}, {"loop.b", 500000}}),
                 ErrorCode::OverApportioned);
  // Exactly the whole is allowed, and leaves no remainder.
  CCA_ASSIGN(exact, apportioned_ledger(1000, {{"loop.a", 600000}, {"loop.b", 400000}}));
  CCA_CHECK_EQ(declared_of(exact, "loop.home"), std::int64_t{0});
  CCA_CHECK_EQ(declared_of(exact, "loop.a") + declared_of(exact, "loop.b"),
               std::int64_t{1000});
}

CCA_TEST(apportionment_share_targeting_the_home_scope_is_refused) {
  CCA_CHECK_CODE(apportioned_ledger(1000, {{"loop.home", 500000}}),
                 ErrorCode::ShareTargetsHomeScope);
  CCA_CHECK_CODE(apportioned_ledger(1000, {{"loop.a", 100000}, {"loop.home", 100000}}),
                 ErrorCode::ShareTargetsHomeScope);
}

CCA_TEST(apportionment_duplicate_target_is_refused) {
  CCA_CHECK_CODE(apportioned_ledger(1000, {{"loop.a", 100000}, {"loop.a", 100000}}),
                 ErrorCode::DuplicateShareTarget);
  CCA_CHECK_CODE(apportioned_ledger(1000, {{"loop.a", 500000}, {"loop.b", 100000},
                                           {"loop.a", 100000}}),
                 ErrorCode::DuplicateShareTarget);
}

CCA_TEST(apportionment_requires_at_least_one_share) {
  CCA_CHECK_CODE(apportioned_ledger(1000, {}), ErrorCode::ApportionmentIncomplete);
}

CCA_TEST(apportionment_shares_on_an_exclusive_contribution_are_malformed) {
  // An exclusive contribution carrying shares is a malformed record.
  AccountingInput input = base_input();
  Contribution contribution;
  contribution.id = id_of<ContributionId>("unit.exclusive");
  contribution.home_scope = id_of<ScopeId>("loop.home");
  contribution.equipment = id_of<EquipmentId>("equipment.exclusive");
  contribution.equipment_class = id_of<EquipmentClassId>("class.crah");
  contribution.medium = Medium::Air;
  contribution.classification = ContributionClass::Additive;
  contribution.installed = Measure<ThermalPower>::known(ThermalPower::of_milliwatts(1000));
  contribution.service = ServiceState::InService;
  contribution.sharing.kind = Sharing::Kind::Exclusive;
  ApportionmentShare share;
  share.target = id_of<ScopeId>("loop.a");
  share.share = Ratio::of_ppm(500000).value();
  contribution.sharing.shares.push_back(share);
  contribution.observed_at = cca_test::fixture_now();
  contribution.binding = EvidenceBinding::initial();
  input.contributions.push_back(contribution);
  CCA_CHECK_CODE(AccountingLedger::build(input, cca_test::fixture_now()),
                 ErrorCode::MalformedRecord);

  // The same contribution with no shares is exclusive and accepted.
  input.contributions[0].sharing.shares.clear();
  CCA_CHECK(AccountingLedger::build(input, cca_test::fixture_now()).ok());
}

CCA_TEST(apportionment_of_an_unknown_quantity_is_refused) {
  CCA_CHECK_CODE(apportioned_ledger(0, {{"loop.a", 500000}}, false),
                 ErrorCode::UnknownMeasurement);
}

CCA_TEST(apportionment_of_a_grouped_contribution_is_refused) {
  AccountingInput input = base_input();
  ContributionGroup group;
  group.id = id_of<ContributionGroupId>("group.sub");
  group.scope = id_of<ScopeId>("loop.home");
  group.classification = ContributionClass::Substitutive;
  group.medium = Medium::Air;
  group.required_concurrent = 1;
  input.groups.push_back(group);

  Contribution contribution;
  contribution.id = id_of<ContributionId>("unit.grouped");
  contribution.home_scope = id_of<ScopeId>("loop.home");
  contribution.equipment = id_of<EquipmentId>("equipment.grouped");
  contribution.equipment_class = id_of<EquipmentClassId>("class.crah");
  contribution.medium = Medium::Air;
  contribution.classification = ContributionClass::Substitutive;
  contribution.group = group.id;
  contribution.installed = Measure<ThermalPower>::known(ThermalPower::of_milliwatts(1000));
  contribution.service = ServiceState::InService;
  contribution.sharing.kind = Sharing::Kind::Apportioned;
  ApportionmentShare share;
  share.target = id_of<ScopeId>("loop.a");
  share.share = Ratio::of_ppm(500000).value();
  contribution.sharing.shares.push_back(share);
  contribution.observed_at = cca_test::fixture_now();
  contribution.binding = EvidenceBinding::initial();
  input.contributions.push_back(contribution);
  CCA_CHECK_CODE(AccountingLedger::build(input, cca_test::fixture_now()),
                 ErrorCode::GroupRequiresExclusiveSharing);

  // A grouped contribution that is exclusive keeps the whole quantity home.
  input.contributions[0].sharing.shares.clear();
  input.contributions[0].sharing.kind = Sharing::Kind::Exclusive;
  CCA_ASSIGN(ledger, AccountingLedger::build(input, cca_test::fixture_now()));
  CCA_CHECK_EQ(declared_of(ledger, "loop.home"), std::int64_t{1000});
  CCA_CHECK_EQ(declared_of(ledger, "loop.a"), std::int64_t{0});
}

}  // namespace
