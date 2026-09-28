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

// The redundancy ladder and the reserve obligation it derives.
//
//   required_installed = copies * protected_quantity
//                        + sum of the largest 'failures' declared failure domains
//   obligation         = required_installed - protected_quantity
//   withheld           = min(obligation, in-service pool)
//
// Independence is never inferred: a member that does not declare a shared-fate
// boundary the group also declares, with current evidence, leaves the WHOLE
// in-service pool of the group indeterminate. Every fixture here is SYNTHETIC.

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

constexpr std::int64_t kWeekMs = 7 * 24 * 60 * 60 * 1000;

// ---------------------------------------------------------------------------
// A file-local synthetic redundancy bench.
// ---------------------------------------------------------------------------

struct DomainSpec {
  std::string id;
  /// The declaration carries an evidence reference.
  bool evidenced = true;
  /// The evidence is current at the accounting instant (false: a week old).
  bool current = true;
  /// The group lists this domain among its declared boundaries.
  bool listed = true;
};

struct MemberSpec {
  std::string id;
  std::int64_t installed_mw = 100;
  std::uint32_t priority = 0;
  /// The shared-fate boundary the member declares. Empty: none declared.
  std::string domain;
  bool in_service = true;
  /// When non-zero the member is Degraded and carries one factor derate.
  std::uint32_t derate_ppm = 0;
};

struct RedundancyCase {
  RedundancyClass redundancy = RedundancyClass::NPlus1;
  bool protected_known = true;
  std::int64_t protected_mw = 100;
  std::vector<DomainSpec> domains;
  std::vector<MemberSpec> members;
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
  input.scopes.push_back(scope_node("loop.a", ScopeKind::Loop, "zone.alpha", Medium::Air));
  EquipmentClass klass;
  klass.id = id_of<EquipmentClassId>("class.crah");
  klass.kind = EquipmentClassKind::Other;
  klass.medium = Medium::Air;
  input.equipment_classes.push_back(klass);
  return input;
}

Result<AccountingLedger> build_case(const RedundancyCase& kase) {
  AccountingInput input = base_input();
  const GenerationBundle generations = input.generations;

  ContributionGroup group;
  group.id = id_of<ContributionGroupId>("group.redundant");
  group.scope = id_of<ScopeId>("loop.a");
  group.classification = ContributionClass::Redundant;
  group.medium = Medium::Air;
  group.redundancy = kase.redundancy;
  if (kase.protected_known) {
    group.protected_quantity = Measure<ThermalPower>::known(
        ThermalPower::of_milliwatts(kase.protected_mw));
  } else {
    group.protected_quantity = Measure<ThermalPower>::unknown(
        MeasureReason::EvidenceMissing,
        BoundedText::from_validated("synthetic missing declaration"));
  }

  for (const DomainSpec& spec : kase.domains) {
    IndependenceDomain domain;
    domain.id = id_of<IndependenceDomainId>(spec.id);
    domain.scope = id_of<ScopeId>("loop.a");
    domain.topology_generation = generations.topology;
    domain.epoch = generations.epoch;
    if (spec.evidenced) {
      const std::string evidence_id = "evidence." + spec.id;
      domain.evidence = id_of<EvidenceId>(evidence_id);
      input.evidence.push_back(cca_test::make_evidence(
          evidence_id, EvidenceKind::TopologyDeclaration,
          spec.current ? cca_test::fixture_now() : cca_test::fixture_ago(kWeekMs),
          generations));
    }
    input.independence_domains.push_back(domain);
    if (spec.listed) {
      group.independence_domains.push_back(domain.id);
    }
  }
  input.groups.push_back(group);

  for (const MemberSpec& spec : kase.members) {
    Contribution contribution;
    contribution.id = id_of<ContributionId>(spec.id);
    contribution.home_scope = id_of<ScopeId>("loop.a");
    contribution.equipment = id_of<EquipmentId>("equipment." + spec.id);
    contribution.equipment_class = id_of<EquipmentClassId>("class.crah");
    contribution.medium = Medium::Air;
    contribution.classification = ContributionClass::Redundant;
    contribution.installed =
        Measure<ThermalPower>::known(ThermalPower::of_milliwatts(spec.installed_mw));
    if (spec.derate_ppm != 0U) {
      contribution.service = ServiceState::Degraded;
      contribution.derates.push_back(DerateFactor(
          id_of<DerateId>("derate." + spec.id), Ratio::of_ppm(spec.derate_ppm).value()));
    } else {
      contribution.service =
          spec.in_service ? ServiceState::InService : ServiceState::OutOfService;
    }
    contribution.priority = spec.priority;
    contribution.group = group.id;
    if (!spec.domain.empty()) {
      contribution.independence_domain = id_of<IndependenceDomainId>(spec.domain);
    }
    contribution.observed_at = cca_test::fixture_now();
    contribution.binding = EvidenceBinding::initial();
    input.contributions.push_back(contribution);
  }
  return AccountingLedger::build(input, cca_test::fixture_now());
}

DomainSpec domain_spec(std::string id) {
  DomainSpec spec;
  spec.id = std::move(id);
  return spec;
}

MemberSpec member_spec(std::string id, std::int64_t installed_mw, std::string domain,
                       std::uint32_t priority = 0) {
  MemberSpec spec;
  spec.id = std::move(id);
  spec.installed_mw = installed_mw;
  spec.domain = std::move(domain);
  spec.priority = priority;
  return spec;
}

const ReserveObligation* only_obligation(const AccountingLedger& ledger) {
  if (ledger.reserve_obligations().size() != 1U) {
    return nullptr;
  }
  return &ledger.reserve_obligations().front();
}

const ScopeAccounting* scope_of(const AccountingLedger& ledger, std::string_view id) {
  return ledger.find_scope(id_of<ScopeId>(id));
}

void check_same_obligation(const ReserveObligation& lhs, const ReserveObligation& rhs) {
  CCA_CHECK_EQ(lhs.group, rhs.group);
  CCA_CHECK_EQ(lhs.scope, rhs.scope);
  CCA_CHECK_EQ(lhs.redundancy, rhs.redundancy);
  CCA_CHECK_EQ(lhs.medium, rhs.medium);
  CCA_CHECK_EQ(lhs.status, rhs.status);
  CCA_CHECK_EQ(lhs.protected_quantity, rhs.protected_quantity);
  CCA_CHECK_EQ(lhs.required_installed, rhs.required_installed);
  CCA_CHECK_EQ(lhs.obligation, rhs.obligation);
  CCA_CHECK_EQ(lhs.withheld, rhs.withheld);
  CCA_CHECK_EQ(lhs.shortfall, rhs.shortfall);
  CCA_CHECK_EQ(lhs.ranked_domains, rhs.ranked_domains);
}

std::int64_t withheld_of(const AccountingLedger& ledger, std::string_view contribution) {
  const ContributionAccounting* record =
      ledger.find_contribution(id_of<ContributionId>(contribution));
  if (record == nullptr || record->allocations.empty()) {
    return -1;
  }
  return record->allocations.front().withheld.milliwatts();
}

std::int64_t allocated_of(const AccountingLedger& ledger, std::string_view contribution) {
  const ContributionAccounting* record =
      ledger.find_contribution(id_of<ContributionId>(contribution));
  if (record == nullptr || record->allocations.empty()) {
    return -1;
  }
  return record->allocations.front().allocatable.milliwatts();
}

std::int64_t indeterminate_of(const AccountingLedger& ledger, std::string_view contribution) {
  const ContributionAccounting* record =
      ledger.find_contribution(id_of<ContributionId>(contribution));
  if (record == nullptr || record->allocations.empty()) {
    return -1;
  }
  return record->allocations.front().indeterminate.milliwatts();
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

// ---------------------------------------------------------------------------

CCA_TEST(redundancy_ladder_counts_copies_and_failure_domains) {
  struct LadderRow {
    RedundancyClass klass;
    std::uint32_t copies;
    std::uint32_t failures;
  };
  const LadderRow rows[] = {
      {RedundancyClass::N, 1, 0},        {RedundancyClass::NPlus1, 1, 1},
      {RedundancyClass::NPlus2, 1, 2},   {RedundancyClass::TwoN, 2, 0},
      {RedundancyClass::TwoNPlus1, 2, 1}, {RedundancyClass::TwoNPlus2, 2, 2}};
  for (const LadderRow& row : rows) {
    CCA_CHECK_EQ(redundancy_copies(row.klass), row.copies);
    CCA_CHECK_EQ(redundancy_failure_domains(row.klass), row.failures);
    CCA_CHECK(row.copies >= 1U && row.copies <= kMaxRedundancyCopies);
    CCA_CHECK(row.failures <= kMaxRedundancyFailures);
  }
}

CCA_TEST(redundancy_obligation_follows_the_ladder) {
  struct Expected {
    RedundancyClass klass;
    std::uint32_t copies;
    std::uint32_t failures;
  };
  const Expected ladder[] = {
      {RedundancyClass::N, 1, 0},         {RedundancyClass::NPlus1, 1, 1},
      {RedundancyClass::NPlus2, 1, 2},    {RedundancyClass::TwoN, 2, 0},
      {RedundancyClass::TwoNPlus1, 2, 1}, {RedundancyClass::TwoNPlus2, 2, 2}};

  for (const Expected& step : ladder) {
    RedundancyCase kase;
    kase.redundancy = step.klass;
    kase.protected_mw = 100000;
    kase.domains = {domain_spec("d.one"), domain_spec("d.two"), domain_spec("d.three")};
    kase.members = {member_spec("m.one", 100000, "d.one"),
                    member_spec("m.two", 100000, "d.two"),
                    member_spec("m.three", 100000, "d.three")};
    CCA_ASSIGN(ledger, build_case(kase));

    const ReserveObligation* obligation = only_obligation(ledger);
    CCA_CHECK(obligation != nullptr);
    if (obligation == nullptr) {
      return;
    }
    const std::int64_t failure_mass =
        static_cast<std::int64_t>(step.failures) * 100000;
    const std::int64_t required =
        static_cast<std::int64_t>(step.copies) * 100000 + failure_mass;
    const std::int64_t owed = required - 100000;
    const std::int64_t pool = 300000;
    const std::int64_t withheld = owed < pool ? owed : pool;
    const std::int64_t shortfall = required > pool ? required - pool : 0;

    CCA_CHECK_EQ(obligation->redundancy, step.klass);
    CCA_CHECK_EQ(obligation->scope, id_of<ScopeId>("loop.a"));
    CCA_CHECK_EQ(obligation->medium, Medium::Air);
    CCA_CHECK_EQ(obligation->protected_quantity.milliwatts(), std::int64_t{100000});
    CCA_CHECK_EQ(obligation->required_installed.milliwatts(), required);
    CCA_CHECK_EQ(obligation->obligation.milliwatts(), owed);
    CCA_CHECK_EQ(obligation->withheld.milliwatts(), withheld);
    CCA_CHECK_EQ(obligation->shortfall.milliwatts(), shortfall);
    if (step.failures == 0 && step.copies == 1) {
      CCA_CHECK_EQ(obligation->status, ObligationStatus::NoReserveRequired);
      CCA_CHECK_EQ(obligation->ranked_domains.size(), std::size_t{0});
    } else {
      CCA_CHECK_EQ(obligation->status, ObligationStatus::Established);
      CCA_CHECK_EQ(obligation->ranked_domains.size(), std::size_t{3});
    }

    // The reserve is withheld from the pool and nothing else moves.
    const ScopeAccounting* loop = scope_of(ledger, "loop.a");
    CCA_CHECK(loop != nullptr);
    if (loop == nullptr) {
      return;
    }
    CCA_CHECK_EQ(loop->declared_installed.milliwatts(), pool);
    CCA_CHECK_EQ(loop->totals.withheld.milliwatts(), withheld);
    CCA_CHECK_EQ(loop->totals.allocatable.milliwatts(), pool - withheld);
    CCA_CHECK_EQ(loop->totals.sum(), loop->declared_installed);
    CCA_CHECK(loop->withheld.verify_total().ok());
    CCA_CHECK(ledger.verify_closure().exact);
  }
}

CCA_TEST(redundancy_n_plus_one_withholds_the_largest_domain) {
  RedundancyCase kase;
  kase.redundancy = RedundancyClass::NPlus1;
  kase.protected_mw = 300000;
  kase.domains = {domain_spec("d.a"), domain_spec("d.b"), domain_spec("d.c")};
  // Members are ordered by priority and then by identifier: m.b, m.c, m.a.
  kase.members = {member_spec("m.a", 100000, "d.a", 2),
                  member_spec("m.b", 250000, "d.b", 0),
                  member_spec("m.c", 400000, "d.c", 1)};
  CCA_ASSIGN(ledger, build_case(kase));

  const ReserveObligation* obligation = only_obligation(ledger);
  CCA_CHECK(obligation != nullptr);
  if (obligation == nullptr) {
    return;
  }
  // required = 300000 + the largest declared domain (400000).
  CCA_CHECK_EQ(obligation->status, ObligationStatus::Established);
  CCA_CHECK_EQ(obligation->required_installed.milliwatts(), std::int64_t{700000});
  CCA_CHECK_EQ(obligation->obligation.milliwatts(), std::int64_t{400000});
  CCA_CHECK_EQ(obligation->withheld.milliwatts(), std::int64_t{400000});
  CCA_CHECK_EQ(obligation->ranked_domains.size(), std::size_t{3});
  if (obligation->ranked_domains.size() == 3U) {
    CCA_CHECK_EQ(obligation->ranked_domains[0], id_of<IndependenceDomainId>("d.c"));
    CCA_CHECK_EQ(obligation->ranked_domains[1], id_of<IndependenceDomainId>("d.b"));
    CCA_CHECK_EQ(obligation->ranked_domains[2], id_of<IndependenceDomainId>("d.a"));
  }

  // The withheld reserve is placed across the members in member order.
  CCA_CHECK_EQ(withheld_of(ledger, "m.b"), std::int64_t{250000});
  CCA_CHECK_EQ(withheld_of(ledger, "m.c"), std::int64_t{150000});
  CCA_CHECK_EQ(withheld_of(ledger, "m.a"), std::int64_t{0});
  CCA_CHECK_EQ(allocated_of(ledger, "m.b"), std::int64_t{0});
  CCA_CHECK_EQ(allocated_of(ledger, "m.c"), std::int64_t{250000});
  CCA_CHECK_EQ(allocated_of(ledger, "m.a"), std::int64_t{100000});

  const ScopeAccounting* loop = scope_of(ledger, "loop.a");
  CCA_CHECK(loop != nullptr);
  if (loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(loop->totals.withheld.milliwatts(), std::int64_t{400000});
  CCA_CHECK_EQ(loop->withheld.of(WithheldReason::ReserveObligation).milliwatts(),
               std::int64_t{400000});
  CCA_CHECK_EQ(loop->totals.sum(), loop->declared_installed);
}

CCA_TEST(redundancy_ties_are_broken_by_domain_identifier) {
  RedundancyCase kase;
  kase.redundancy = RedundancyClass::NPlus1;
  kase.protected_mw = 50;
  kase.domains = {domain_spec("d.b"), domain_spec("d.a")};
  kase.members = {member_spec("m.b", 100, "d.b"), member_spec("m.a", 100, "d.a")};
  CCA_ASSIGN(ledger, build_case(kase));
  const ReserveObligation* obligation = only_obligation(ledger);
  CCA_CHECK(obligation != nullptr);
  if (obligation == nullptr) {
    return;
  }
  CCA_CHECK_EQ(obligation->ranked_domains.size(), std::size_t{2});
  if (obligation->ranked_domains.size() == 2U) {
    CCA_CHECK_EQ(obligation->ranked_domains[0], id_of<IndependenceDomainId>("d.a"));
    CCA_CHECK_EQ(obligation->ranked_domains[1], id_of<IndependenceDomainId>("d.b"));
  }
  CCA_CHECK_EQ(obligation->required_installed.milliwatts(), std::int64_t{150});
  CCA_CHECK_EQ(obligation->obligation.milliwatts(), std::int64_t{100});
  CCA_CHECK_EQ(obligation->withheld.milliwatts(), std::int64_t{100});
}

CCA_TEST(redundancy_two_n_withholds_exactly_the_protected_quantity) {
  RedundancyCase kase;
  kase.redundancy = RedundancyClass::TwoN;
  kase.protected_mw = 100000;
  kase.domains = {domain_spec("d.one"), domain_spec("d.two")};
  kase.members = {member_spec("m.one", 100000, "d.one"), member_spec("m.two", 100000, "d.two")};
  CCA_ASSIGN(ledger, build_case(kase));
  const ReserveObligation* obligation = only_obligation(ledger);
  CCA_CHECK(obligation != nullptr);
  if (obligation == nullptr) {
    return;
  }
  CCA_CHECK_EQ(obligation->status, ObligationStatus::Established);
  CCA_CHECK_EQ(obligation->required_installed.milliwatts(), std::int64_t{200000});
  CCA_CHECK_EQ(obligation->obligation.milliwatts(), std::int64_t{100000});
  CCA_CHECK_EQ(obligation->withheld.milliwatts(), obligation->protected_quantity.milliwatts());
  CCA_CHECK_EQ(obligation->withheld.milliwatts(), std::int64_t{100000});
  CCA_CHECK(obligation->shortfall.is_zero());
  CCA_CHECK_EQ(allocated_of(ledger, "m.one") + allocated_of(ledger, "m.two"),
               std::int64_t{100000});
}

CCA_TEST(redundancy_shortfall_is_preserved_not_credited) {
  RedundancyCase kase;
  kase.redundancy = RedundancyClass::TwoN;
  kase.protected_mw = 100000;
  kase.domains = {domain_spec("d.one")};
  kase.members = {member_spec("m.one", 60000, "d.one")};
  CCA_ASSIGN(ledger, build_case(kase));

  const ReserveObligation* obligation = only_obligation(ledger);
  CCA_CHECK(obligation != nullptr);
  if (obligation == nullptr) {
    return;
  }
  CCA_CHECK_EQ(obligation->status, ObligationStatus::Established);
  CCA_CHECK_EQ(obligation->required_installed.milliwatts(), std::int64_t{200000});
  CCA_CHECK_EQ(obligation->obligation.milliwatts(), std::int64_t{100000});
  CCA_CHECK_EQ(obligation->withheld.milliwatts(), std::int64_t{60000});
  CCA_CHECK_EQ(obligation->shortfall.milliwatts(), std::int64_t{140000});

  const ScopeAccounting* loop = scope_of(ledger, "loop.a");
  CCA_CHECK(loop != nullptr);
  if (loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(loop->declared_installed.milliwatts(), std::int64_t{60000});
  CCA_CHECK_EQ(loop->totals.withheld.milliwatts(), std::int64_t{60000});
  CCA_CHECK_EQ(loop->totals.allocatable.milliwatts(), std::int64_t{0});
  CCA_CHECK_EQ(loop->totals.sum(), loop->declared_installed);
  CCA_CHECK_EQ(count_code(ledger.findings(), FindingCode::ReserveShortfall), std::size_t{1});
  for (const Finding& finding : ledger.findings()) {
    if (finding.code == FindingCode::ReserveShortfall) {
      CCA_CHECK_EQ(finding.severity, FindingSeverity::Error);
      CCA_CHECK(finding.amount.has_value());
      if (finding.amount.has_value()) {
        CCA_CHECK_EQ(finding.amount->milliwatts(), std::int64_t{140000});
      }
    }
  }
}

CCA_TEST(redundancy_independence_is_never_inferred) {
  struct Broken {
    const char* name;
    DomainSpec first;
    DomainSpec second;
    MemberSpec first_member;
    MemberSpec second_member;
    ObligationStatus expected;
  };
  const std::vector<Broken> cases{
      {"no declared boundary", domain_spec("d.one"), domain_spec("d.two"),
       member_spec("m.one", 100, "d.one"), member_spec("m.two", 100, ""),
       ObligationStatus::IndependenceNotDeclared},
      {"boundary absent from the group list", domain_spec("d.one"), domain_spec("d.hidden"),
       member_spec("m.one", 100, "d.one"), member_spec("m.two", 100, "d.hidden"),
       ObligationStatus::IndependenceNotDeclared},
      {"boundary without evidence", domain_spec("d.one"),
       []() {
         DomainSpec bare = domain_spec("d.bare");
         bare.evidenced = false;
         return bare;
       }(),
       member_spec("m.one", 100, "d.one"), member_spec("m.two", 100, "d.bare"),
       ObligationStatus::MissingIndependenceEvidence},
      {"boundary with stale evidence", domain_spec("d.one"),
       []() {
         DomainSpec stale = domain_spec("d.old");
         stale.current = false;
         return stale;
       }(),
       member_spec("m.one", 100, "d.one"), member_spec("m.two", 100, "d.old"),
       ObligationStatus::MissingIndependenceEvidence}};

  for (const Broken& broken : cases) {
    RedundancyCase kase;
    kase.redundancy = RedundancyClass::NPlus1;
    kase.protected_mw = 50;
    kase.domains = {broken.first, broken.second};
    kase.members = {broken.first_member, broken.second_member};
    if (broken.first.id == "d.hidden") {
      kase.domains[0].listed = false;
    }
    if (broken.second.id == "d.hidden") {
      kase.domains[1].listed = false;
    }
    CCA_ASSIGN(ledger, build_case(kase));
    cca_test::note(std::string("independence case: ") + broken.name);

    const ReserveObligation* obligation = only_obligation(ledger);
    CCA_CHECK(obligation != nullptr);
    if (obligation == nullptr) {
      return;
    }
    CCA_CHECK_EQ(obligation->status, broken.expected);
    CCA_CHECK(obligation->withheld.is_zero());
    CCA_CHECK(obligation->obligation.is_zero());

    // The WHOLE in-service pool of the group is indeterminate, including the
    // member whose own declaration is sound.
    CCA_CHECK_EQ(indeterminate_of(ledger, "m.one"), std::int64_t{100});
    CCA_CHECK_EQ(indeterminate_of(ledger, "m.two"), std::int64_t{100});
    CCA_CHECK_EQ(allocated_of(ledger, "m.one"), std::int64_t{0});
    CCA_CHECK_EQ(allocated_of(ledger, "m.two"), std::int64_t{0});
    const ScopeAccounting* loop = scope_of(ledger, "loop.a");
    CCA_CHECK(loop != nullptr);
    if (loop == nullptr) {
      return;
    }
    CCA_CHECK_EQ(loop->totals.indeterminate.milliwatts(), std::int64_t{200});
    CCA_CHECK_EQ(loop->totals.allocatable.milliwatts(), std::int64_t{0});
    CCA_CHECK_EQ(loop->totals.withheld.milliwatts(), std::int64_t{0});
    CCA_CHECK_EQ(loop->status, ClosureStatus::Indeterminate);
    CCA_CHECK_EQ(count_code(loop->findings, FindingCode::ReserveUnresolved), std::size_t{2});
    CCA_CHECK_EQ(count_code(ledger.findings(), FindingCode::ReserveUnresolved), std::size_t{2});
    for (const Finding& finding : ledger.findings()) {
      if (finding.code == FindingCode::ReserveUnresolved) {
        CCA_CHECK_EQ(finding.severity, FindingSeverity::Error);
        CCA_CHECK(finding.scope == id_of<ScopeId>("loop.a"));
      }
    }
  }
}

CCA_TEST(redundancy_protected_quantity_unknown) {
  // A 2N group cannot resolve its obligation without a protected quantity, and
  // the pool is indeterminate rather than guessed.
  RedundancyCase unknown_case;
  unknown_case.redundancy = RedundancyClass::TwoN;
  unknown_case.protected_known = false;
  unknown_case.domains = {domain_spec("d.one")};
  unknown_case.members = {member_spec("m.one", 100, "d.one")};
  CCA_ASSIGN(unknown_ledger, build_case(unknown_case));
  const ReserveObligation* unknown_obligation = only_obligation(unknown_ledger);
  CCA_CHECK(unknown_obligation != nullptr);
  if (unknown_obligation == nullptr) {
    return;
  }
  CCA_CHECK_EQ(unknown_obligation->status, ObligationStatus::ProtectedQuantityUnknown);
  CCA_CHECK_EQ(indeterminate_of(unknown_ledger, "m.one"), std::int64_t{100});
  CCA_CHECK_EQ(count_code(unknown_ledger.findings(), FindingCode::ReserveUnresolved),
               std::size_t{1});

  // N requires no reserve beyond the protected quantity, so an unknown
  // protected quantity leaves the pool determinate.
  RedundancyCase n_case;
  n_case.redundancy = RedundancyClass::N;
  n_case.protected_known = false;
  n_case.domains = {domain_spec("d.one")};
  n_case.members = {member_spec("m.one", 100, "d.one")};
  CCA_ASSIGN(n_ledger, build_case(n_case));
  const ReserveObligation* n_obligation = only_obligation(n_ledger);
  CCA_CHECK(n_obligation != nullptr);
  if (n_obligation == nullptr) {
    return;
  }
  CCA_CHECK_EQ(n_obligation->status, ObligationStatus::NoReserveRequired);
  CCA_CHECK(n_obligation->required_installed.is_zero());
  CCA_CHECK(n_obligation->withheld.is_zero());
  CCA_CHECK_EQ(allocated_of(n_ledger, "m.one"), std::int64_t{100});
  CCA_CHECK_EQ(count_code(n_ledger.findings(), FindingCode::ReserveUnresolved), std::size_t{0});
}

CCA_TEST(redundancy_out_of_service_members_are_not_in_the_pool) {
  RedundancyCase kase;
  kase.redundancy = RedundancyClass::NPlus1;
  kase.protected_mw = 100000;
  kase.domains = {domain_spec("d.one"), domain_spec("d.two"), domain_spec("d.three")};
  kase.members = {member_spec("m.one", 100000, "d.one"), member_spec("m.two", 100000, "d.two")};
  MemberSpec stopped = member_spec("m.three", 500000, "d.three");
  stopped.in_service = false;
  kase.members.push_back(stopped);
  CCA_ASSIGN(ledger, build_case(kase));

  const ReserveObligation* obligation = only_obligation(ledger);
  CCA_CHECK(obligation != nullptr);
  if (obligation == nullptr) {
    return;
  }
  // The stopped member's capacity is neither protected nor spendable.
  CCA_CHECK_EQ(obligation->status, ObligationStatus::Established);
  CCA_CHECK_EQ(obligation->required_installed.milliwatts(), std::int64_t{200000});
  CCA_CHECK_EQ(obligation->withheld.milliwatts(), std::int64_t{100000});
  CCA_CHECK(obligation->shortfall.is_zero());
  CCA_CHECK_EQ(withheld_of(ledger, "m.three"), std::int64_t{0});
  const ContributionAccounting* stopped_record =
      ledger.find_contribution(id_of<ContributionId>("m.three"));
  CCA_CHECK(stopped_record != nullptr);
  if (stopped_record == nullptr) {
    return;
  }
  CCA_CHECK_EQ(stopped_record->allocations.front().unavailable.milliwatts(),
               std::int64_t{500000});
  const ScopeAccounting* loop = scope_of(ledger, "loop.a");
  CCA_CHECK(loop != nullptr);
  if (loop == nullptr) {
    return;
  }
  CCA_CHECK_EQ(loop->declared_installed.milliwatts(), std::int64_t{700000});
  CCA_CHECK_EQ(loop->totals.unavailable.milliwatts(), std::int64_t{500000});
  CCA_CHECK_EQ(loop->totals.withheld.milliwatts(), std::int64_t{100000});
  CCA_CHECK_EQ(loop->totals.allocatable.milliwatts(), std::int64_t{100000});
  CCA_CHECK_EQ(loop->totals.sum(), loop->declared_installed);
}

CCA_TEST(redundancy_withheld_allocatable_and_loss_still_close) {
  // A degraded member keeps its degraded loss while the group withholds part of
  // the pool: declared == allocatable + withheld + degraded_loss.
  RedundancyCase kase;
  kase.redundancy = RedundancyClass::NPlus1;
  kase.protected_mw = 50000;
  kase.domains = {domain_spec("d.one"), domain_spec("d.two")};
  MemberSpec degraded = member_spec("m.two", 200000, "d.two");
  degraded.derate_ppm = 500000;
  kase.members = {member_spec("m.one", 100000, "d.one"), degraded};
  CCA_ASSIGN(ledger, build_case(kase));

  const ScopeAccounting* loop = scope_of(ledger, "loop.a");
  CCA_CHECK(loop != nullptr);
  if (loop == nullptr) {
    return;
  }
  const ReserveObligation* obligation = only_obligation(ledger);
  CCA_CHECK(obligation != nullptr);
  if (obligation == nullptr) {
    return;
  }
  // The degraded member retains 100000 of its 200000 and loses 100000; the
  // largest declared domain is then 100000 either way.
  CCA_CHECK_EQ(obligation->status, ObligationStatus::Established);
  CCA_CHECK_EQ(obligation->required_installed.milliwatts(), std::int64_t{150000});
  CCA_CHECK_EQ(obligation->obligation.milliwatts(), std::int64_t{100000});
  CCA_CHECK_EQ(obligation->withheld.milliwatts(), std::int64_t{100000});
  CCA_CHECK(obligation->shortfall.is_zero());

  CCA_CHECK_EQ(loop->declared_installed.milliwatts(), std::int64_t{300000});
  CCA_CHECK_EQ(loop->totals.degraded_loss.milliwatts(), std::int64_t{100000});
  CCA_CHECK_EQ(loop->totals.withheld.milliwatts(), std::int64_t{100000});
  CCA_CHECK_EQ(loop->totals.allocatable.milliwatts(), std::int64_t{100000});
  CCA_CHECK_EQ(loop->totals.sum(), loop->declared_installed);
  CCA_CHECK_EQ(loop->totals.withheld.milliwatts() + loop->totals.allocatable.milliwatts() +
                   loop->totals.degraded_loss.milliwatts(),
               loop->declared_installed.milliwatts());
  CCA_CHECK(loop->withheld.verify_total().ok());
  CCA_CHECK(ledger.verify_closure().exact);
  CCA_CHECK_EQ(loop->totals.sum(), loop->declared_installed);
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

CCA_TEST(redundancy_is_independent_of_input_order) {
  RedundancyCase kase;
  kase.redundancy = RedundancyClass::NPlus2;
  kase.protected_mw = 100000;
  kase.domains = {domain_spec("d.one"), domain_spec("d.two"), domain_spec("d.three")};
  kase.members = {member_spec("m.one", 100000, "d.one", 3),
                  member_spec("m.two", 250000, "d.two", 1),
                  member_spec("m.three", 180000, "d.three", 2)};
  CCA_ASSIGN(reference, build_case(kase));
  const ReserveObligation* reference_obligation = only_obligation(reference);
  CCA_CHECK(reference_obligation != nullptr);
  if (reference_obligation == nullptr) {
    return;
  }

  const std::vector<MemberSpec> reversed_members{kase.members[2], kase.members[1],
                                                 kase.members[0]};
  const std::vector<DomainSpec> reversed_domains{kase.domains[2], kase.domains[1],
                                                 kase.domains[0]};

  // Reordering the members changes nothing at all: the same generation digest,
  // the same obligation and the same per-member disposition.
  RedundancyCase reversed_members_case = kase;
  reversed_members_case.members = reversed_members;
  CCA_ASSIGN(reversed, build_case(reversed_members_case));
  const ReserveObligation* reversed_obligation = only_obligation(reversed);
  CCA_CHECK(reversed_obligation != nullptr);
  if (reversed_obligation == nullptr) {
    return;
  }
  CCA_CHECK_EQ(reversed.digest(), reference.digest());
  check_same_obligation(*reversed_obligation, *reference_obligation);
  for (const MemberSpec& member : kase.members) {
    CCA_CHECK_EQ(withheld_of(reversed, member.id), withheld_of(reference, member.id));
    CCA_CHECK_EQ(allocated_of(reversed, member.id), allocated_of(reference, member.id));
  }

  // Reordering the declarations they name changes nothing about the accounting
  // either. (The generation digest of two inputs that differ ONLY in the order
  // of ContributionGroup::independence_domains is NOT identical: the canonical
  // encoder sorts the top-level vectors, the derates and the shares, but not
  // the nested domain list. That divergence is reported rather than encoded
  // here as expected behaviour.)
  RedundancyCase reversed_domains_case = kase;
  reversed_domains_case.domains = reversed_domains;
  CCA_ASSIGN(domain_order, build_case(reversed_domains_case));
  const ReserveObligation* domain_obligation = only_obligation(domain_order);
  CCA_CHECK(domain_obligation != nullptr);
  if (domain_obligation == nullptr) {
    return;
  }
  check_same_obligation(*domain_obligation, *reference_obligation);
  for (const MemberSpec& member : kase.members) {
    CCA_CHECK_EQ(withheld_of(domain_order, member.id), withheld_of(reference, member.id));
    CCA_CHECK_EQ(allocated_of(domain_order, member.id), allocated_of(reference, member.id));
  }
}

}  // namespace
