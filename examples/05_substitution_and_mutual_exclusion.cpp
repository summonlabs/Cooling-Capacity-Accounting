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

// 05 - substitution and mutual exclusion
//
// Every facility fact used here is SYNTHETIC.


int main() {
  std::cout << "05 - substitution and mutual exclusion (SYNTHETIC facility data)\n";
  example::AccountingInput input = example::make_input();
  input.equipment_classes.push_back(example::make_class(
      "class.crah", example::Medium::Air,
      example::EquipmentClassKind::ComputerRoomAirHandler));
  input.scopes.push_back(example::make_scope("site.alpha", example::ScopeKind::Site, "",
                                             example::Medium::Air));
  input.scopes.push_back(example::make_scope("loop.a", example::ScopeKind::Loop,
                                             "site.alpha", example::Medium::Air));

  // Three air handlers that provide the same service: only one may be counted.
  example::ContributionGroup substitutive;
  substitutive.id = example::id_of<example::ContributionGroupId>("group.substitutive");
  substitutive.scope = example::id_of<example::ScopeId>("loop.a");
  substitutive.classification = example::ContributionClass::Substitutive;
  substitutive.medium = example::Medium::Air;
  substitutive.required_concurrent = 1;
  substitutive.redundancy = example::RedundancyClass::N;
  substitutive.binding = example::EvidenceBinding{
      example::ControlPlaneEpoch::initial(), example::TopologyGeneration::initial(),
      example::PolicyGeneration::initial(), example::EvidenceGeneration::initial()};
  input.groups.push_back(substitutive);

  for (int index = 0; index < 3; ++index) {
    const std::string name = "unit.sub." + std::to_string(index);
    input.evidence.push_back(example::make_evidence("evidence." + name, 300'000));
    example::Contribution contribution = example::make_unit(
        name, "loop.a", "class.crah", 300'000, "evidence." + name);
    contribution.classification = example::ContributionClass::Substitutive;
    contribution.group = substitutive.id;
    // The declared priority decides which member is counted; the identifier
    // breaks a tie. Lower values win, so unit.sub.0 is the primary.
    contribution.priority = static_cast<std::uint32_t>(index);
    input.contributions.push_back(contribution);
  }

  // A reserve bank that is never allocatable, whatever else is true.
  input.evidence.push_back(example::make_evidence("evidence.reserve", 200'000));
  example::Contribution reserve = example::make_unit(
      "unit.reserve", "loop.a", "class.crah", 200'000, "evidence.reserve");
  reserve.classification = example::ContributionClass::ReserveOnly;
  input.contributions.push_back(reserve);

  EXAMPLE_ASSIGN(ledger, example::AccountingLedger::build(input, example::now()));
  example::print_scopes(ledger);
  example::print_findings(ledger);
  if (!example::check_closure(ledger)) {
    return 1;
  }

  EXAMPLE_ASSIGN(cell, ledger.scope_rollup(example::id_of<example::ScopeId>("loop.a")));
  const example::ScopeAccounting& scope = cell.scopes.front();
  std::cout << "  allocatable " << scope.totals.allocatable.milliwatts()
            << "mW, withheld " << scope.totals.withheld.milliwatts()
            << "mW (substitutive spare "
            << scope.withheld.of(example::WithheldReason::SubstitutiveSpare).milliwatts()
            << "mW, reserve-only "
            << scope.withheld.of(example::WithheldReason::ReserveOnlyClass).milliwatts()
            << "mW)\n";
  EXAMPLE_REQUIRE(scope.totals.allocatable.milliwatts() == 300'000,
                  "exactly one substitutive member is counted");
  EXAMPLE_REQUIRE(scope.withheld.of(example::WithheldReason::SubstitutiveSpare)
                      .milliwatts() == 600'000,
                  "the spare members are withheld, not dropped");
  EXAMPLE_REQUIRE(scope.withheld.of(example::WithheldReason::ReserveOnlyClass)
                      .milliwatts() == 200'000,
                  "a reserve-only contribution is never allocatable");
  EXAMPLE_REQUIRE(scope.withheld.verify_total().ok(),
                  "the withheld breakdown sums to its total");
  std::cout << "  OK\n";
  return 0;
}
