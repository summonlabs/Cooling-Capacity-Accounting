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

#include <iostream>

#include "example_support.hpp"

// 04 - redundancy and reserve obligations
//
// Every facility fact used here is SYNTHETIC.


namespace {

/// Builds an N+1 group of three identical chillers, split across two declared
/// and evidenced shared-fate boundaries.
[[nodiscard]] example::AccountingInput build_group(bool evidence_for_domains) {
  example::AccountingInput input = example::make_input();
  input.equipment_classes.push_back(example::make_class(
      "class.chiller", example::Medium::Liquid, example::EquipmentClassKind::Chiller));
  input.scopes.push_back(example::make_scope("site.alpha", example::ScopeKind::Site, "",
                                             example::Medium::Liquid));
  input.scopes.push_back(example::make_scope("loop.chilled", example::ScopeKind::Loop,
                                             "site.alpha", example::Medium::Liquid));

  input.evidence.push_back(example::make_evidence("evidence.domain-a", 0));
  input.evidence.push_back(example::make_evidence("evidence.domain-b", 0));

  example::IndependenceDomain domain_a;
  domain_a.id = example::id_of<example::IndependenceDomainId>("domain.a");
  domain_a.label = example::BoundedText::from_validated("plant room A");
  domain_a.scope = example::id_of<example::ScopeId>("loop.chilled");
  domain_a.topology_generation = example::TopologyGeneration::initial();
  domain_a.epoch = example::ControlPlaneEpoch::initial();
  if (evidence_for_domains) {
    domain_a.evidence = example::id_of<example::EvidenceId>("evidence.domain-a");
  }
  input.independence_domains.push_back(domain_a);
  example::IndependenceDomain domain_b = domain_a;
  domain_b.id = example::id_of<example::IndependenceDomainId>("domain.b");
  domain_b.label = example::BoundedText::from_validated("plant room B");
  if (evidence_for_domains) {
    domain_b.evidence = example::id_of<example::EvidenceId>("evidence.domain-b");
  }
  input.independence_domains.push_back(domain_b);

  example::ContributionGroup group;
  group.id = example::id_of<example::ContributionGroupId>("group.chillers");
  group.scope = example::id_of<example::ScopeId>("loop.chilled");
  group.classification = example::ContributionClass::Redundant;
  group.medium = example::Medium::Liquid;
  group.redundancy = example::RedundancyClass::NPlus1;
  group.protected_quantity =
      example::Measure<example::ThermalPower>::known(
          example::ThermalPower::of_milliwatts(500'000));
  group.protected_quantity_evidence =
      example::id_of<example::EvidenceId>("evidence.domain-a");
  group.independence_domains.push_back(domain_a.id);
  group.independence_domains.push_back(domain_b.id);
  group.binding = example::EvidenceBinding{
      example::ControlPlaneEpoch::initial(), example::TopologyGeneration::initial(),
      example::PolicyGeneration::initial(), example::EvidenceGeneration::initial()};
  input.groups.push_back(group);

  const char* const units[3] = {"unit.a1", "unit.a2", "unit.b1"};
  const char* const domains[3] = {"domain.a", "domain.a", "domain.b"};
  for (int index = 0; index < 3; ++index) {
    const std::string name = units[index];
    input.evidence.push_back(example::make_evidence("evidence." + name, 250'000));
    example::Contribution contribution = example::make_unit(
        name, "loop.chilled", "class.chiller", 250'000, "evidence." + name);
    contribution.medium = example::Medium::Liquid;
    contribution.classification = example::ContributionClass::Redundant;
    contribution.group =
        example::id_of<example::ContributionGroupId>("group.chillers");
    contribution.independence_domain =
        example::id_of<example::IndependenceDomainId>(domains[index]);
    input.contributions.push_back(contribution);
  }
  return input;
}

}  // namespace

int main() {
  std::cout << "04 - redundancy and reserve obligations (SYNTHETIC facility data)\n";
  example::AccountingInput input = build_group(true);
  EXAMPLE_ASSIGN(ledger, example::AccountingLedger::build(input, example::now()));
  example::print_scopes(ledger);
  for (const example::ReserveObligation& obligation : ledger.reserve_obligations()) {
    std::cout << "  group " << obligation.group.str() << " class "
              << example::redundancy_class_name(obligation.redundancy) << " status "
              << example::obligation_status_name(obligation.status) << " protected "
              << obligation.protected_quantity.milliwatts() << "mW required "
              << obligation.required_installed.milliwatts() << "mW obligation "
              << obligation.obligation.milliwatts() << "mW withheld "
              << obligation.withheld.milliwatts() << "mW shortfall "
              << obligation.shortfall.milliwatts() << "mW\n";
  }
  if (!example::check_closure(ledger)) {
    return 1;
  }

  EXAMPLE_ASSIGN(cell, ledger.scope_rollup(example::id_of<example::ScopeId>(
                          "loop.chilled")));
  const example::ScopeAccounting& scope = cell.scopes.front();
  std::cout << "  declared " << scope.declared_installed.milliwatts()
            << "mW allocatable " << scope.totals.allocatable.milliwatts()
            << "mW withheld " << scope.totals.withheld.milliwatts() << "mW\n";
  // 750 kW installed and 500 kW protected. The declared shared-fate boundary A
  // holds two of the three units, so losing A costs 500 kW: N+1 therefore needs
  // protected + largest domain = 1000 kW installed and the install is genuinely
  // 250 kW short. The accounting says so instead of crediting capacity that is
  // not there, and it withholds the whole 500 kW reserve from the pool.
  EXAMPLE_REQUIRE(scope.declared_installed.milliwatts() == 750'000,
                  "all three units are accounted");
  EXAMPLE_REQUIRE(scope.totals.withheld.milliwatts() == 500'000,
                  "the reserve to hold is the largest declared failure domain");
  EXAMPLE_REQUIRE(scope.totals.allocatable.milliwatts() == 250'000,
                  "only what remains after the reserve is allocatable");
  EXAMPLE_REQUIRE(scope.status == example::ClosureStatus::Closed,
                  "the identity still closes exactly over the determinate mass");
  bool saw_shortfall = false;
  for (const example::Finding& finding : ledger.findings()) {
    if (finding.code == example::FindingCode::ReserveShortfall) {
      saw_shortfall = true;
      EXAMPLE_REQUIRE(finding.amount.has_value() &&
                          finding.amount->milliwatts() == 250'000,
                      "the shortfall is preserved as an exact quantity");
    }
  }
  EXAMPLE_REQUIRE(saw_shortfall,
                  "a reserve shortfall is reported rather than absorbed");

  // Without evidence for the declarations the obligation cannot be established,
  // and the pool must not be handed out on the strength of identifiers alone.
  example::AccountingInput unevidenced = build_group(false);
  EXAMPLE_ASSIGN(unevidenced_ledger,
                 example::AccountingLedger::build(unevidenced, example::now()));
  EXAMPLE_ASSIGN(unevidenced_cell, unevidenced_ledger.scope_rollup(
                                       example::id_of<example::ScopeId>("loop.chilled")));
  const example::ScopeAccounting& unevidenced_scope = unevidenced_cell.scopes.front();
  std::cout << "  without evidence for the shared-fate declarations: allocatable "
            << unevidenced_scope.totals.allocatable.milliwatts() << "mW indeterminate "
            << unevidenced_scope.totals.indeterminate.milliwatts() << "mW\n";
  EXAMPLE_REQUIRE(unevidenced_scope.totals.allocatable.milliwatts() == 0,
                  "an unresolved reserve obligation makes the pool indeterminate");
  EXAMPLE_REQUIRE(unevidenced_scope.totals.indeterminate.milliwatts() == 750'000,
                  "the whole in-service pool becomes indeterminate");
  EXAMPLE_REQUIRE(unevidenced_scope.totals.withheld.milliwatts() == 0,
                  "nothing is withheld on the strength of an unresolved obligation");
  EXAMPLE_REQUIRE(unevidenced_scope.status == example::ClosureStatus::Indeterminate,
                  "the scope reports the unresolved obligation");
  example::print_findings(unevidenced_ledger);
  std::cout << "  OK\n";
  return 0;
}
